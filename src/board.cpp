#include "board.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMap>
#include <QMimeData>
#include <QPolygonF>
#include <cmath>

namespace {

const char clipboardFormat[] = "application/x-slate-items";
// How far a connector stops short of the shapes it joins.
const double edgeGap = 5;
const int undoLimit = 200;

const QStringList &kinds() {
    static const QStringList kinds{"rect", "ellipse", "triangle", "postit", "text", "line", "connector"};
    return kinds;
}

// Colors are stored as theme palette keys, so a board follows the theme.
const QStringList &palette() {
    static const QStringList palette{"red", "orange", "yellow", "green", "cyan", "blue", "magenta"};
    return palette;
}

QJsonObject itemToJson(const BoardItem &item) {
    QJsonObject o{{"id", item.id}, {"kind", item.kind}};
    if (item.isConnector()) {
        o["from"] = item.from;
        o["to"] = item.to;
    } else {
        o["x"] = item.x;
        o["y"] = item.y;
        o["w"] = item.w;
        o["h"] = item.h;
    }
    if (!item.text.isEmpty())
        o["text"] = item.text;
    if (!item.color.isEmpty())
        o["color"] = item.color;
    if (item.kind == QLatin1String("text"))
        o["fontSize"] = item.fontSize;
    if (item.rotation != 0)
        o["rotation"] = item.rotation;
    return o;
}

// Degrees in (-180, 180], so a full turn is no turn.
double normalized(double degrees) {
    degrees = std::fmod(degrees, 360);
    if (degrees > 180)
        degrees -= 360;
    else if (degrees <= -180)
        degrees += 360;
    return degrees;
}

// The color a thing of a kind takes for a color key. A post-it is never
// without one: it is paper, yellow unless it is something else, so it is
// stored as yellow and the menu marks it so.
QString colorFor(const QString &kind, const QString &key) {
    return key.isEmpty() && kind == QLatin1String("postit") ? QStringLiteral("yellow") : key;
}

BoardItem itemFromJson(const QJsonObject &o) {
    BoardItem item;
    item.id = o.value("id").toInt();
    item.kind = o.value("kind").toString();
    item.x = o.value("x").toDouble();
    item.y = o.value("y").toDouble();
    item.w = o.value("w").toDouble();
    item.h = o.value("h").toDouble();
    item.text = o.value("text").toString();
    item.color = o.value("color").toString();
    if (!palette().contains(item.color))
        item.color.clear();
    item.color = colorFor(item.kind, item.color);
    item.fontSize = std::clamp(o.value("fontSize").toInt(24), 8, 400);
    item.from = o.value("from").toInt();
    item.to = o.value("to").toInt();
    if (item.isShape())
        item.rotation = normalized(o.value("rotation").toDouble());
    return item;
}

double cross(const QPointF &a, const QPointF &b) {
    return a.x() * b.y() - a.y() * b.x();
}

QVector<QPointF> triangle(const BoardItem &item) {
    return {{item.x + item.w / 2, item.y}, {item.x + item.w, item.y + item.h}, {item.x, item.y + item.h}};
}

// Where connectors aim: the middle, which for a triangle is its centroid.
QPointF anchor(const BoardItem &item) {
    if (item.kind == QLatin1String("triangle"))
        return {item.x + item.w / 2, item.y + item.h * 2 / 3};
    return {item.x + item.w / 2, item.y + item.h / 2};
}

QPointF turned(const QPointF &p, const QPointF &about, double degrees) {
    const double r = degrees * M_PI / 180, c = std::cos(r), s = std::sin(r);
    const QPointF d = p - about;
    return {about.x() + d.x() * c - d.y() * s, about.y() + d.x() * s + d.y() * c};
}

QPointF middle(const BoardItem &item) {
    return {item.x + item.w / 2, item.y + item.h / 2};
}

// A shape turns about the middle of its box. Geometry is worked out on the
// shape as it was before turning, with points carried into that frame and
// the answers carried back out.
QPointF toLocal(const BoardItem &item, const QPointF &p) {
    return item.rotation == 0 ? p : turned(p, middle(item), -item.rotation);
}

QPointF toWorld(const BoardItem &item, const QPointF &p) {
    return item.rotation == 0 ? p : turned(p, middle(item), item.rotation);
}

QPointF worldAnchor(const BoardItem &item) {
    return toWorld(item, anchor(item));
}

// The point just outside a shape's outline on the way from its anchor to
// `toward`, so a connector touches the outline rather than the bounding box.
QPointF edge(const BoardItem &item, const QPointF &worldToward) {
    const QPointF toward = toLocal(item, worldToward);
    const QPointF c = anchor(item), d = toward - c;
    const double length = std::hypot(d.x(), d.y());
    if (length < 1e-6)
        return toWorld(item, c);
    const double hw = std::max(item.w / 2, 1e-6), hh = std::max(item.h / 2, 1e-6);
    double t = 1;
    if (item.kind == QLatin1String("ellipse")) {
        t = 1 / std::sqrt(d.x() * d.x() / (hw * hw) + d.y() * d.y() / (hh * hh));
    } else if (item.kind == QLatin1String("triangle")) {
        const auto v = triangle(item);
        double best = -1;
        for (int i = 0; i < 3; ++i) {
            const QPointF a = v[i], e = v[(i + 1) % 3] - a;
            const double denominator = cross(d, e);
            if (std::abs(denominator) < 1e-9)
                continue;
            const double along = cross(a - c, e) / denominator, across = cross(a - c, d) / denominator;
            if (along > 0 && across >= 0 && across <= 1 && (best < 0 || along < best))
                best = along;
        }
        t = best < 0 ? 0 : best;
    } else {
        t = 1 / std::max(std::abs(d.x()) / hw, std::abs(d.y()) / hh);
    }
    return toWorld(item, c + d * t + d / length * edgeGap);
}

double distanceToSegment(const QPointF &p, const QLineF &l) {
    const QPointF d = l.p2() - l.p1();
    const double squared = d.x() * d.x() + d.y() * d.y();
    double t = squared < 1e-9 ? 0 : QPointF::dotProduct(p - l.p1(), d) / squared;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF nearest = l.p1() + d * t;
    return std::hypot(p.x() - nearest.x(), p.y() - nearest.y());
}

} // namespace

Board::Board(QObject *parent) : QAbstractListModel(parent) {
    // What the menu marks follows the selection, the colors of what is in
    // it, and the color picked for new shapes.
    QObject::connect(this, &Board::selectionChanged, this, &Board::shownColorChanged);
    QObject::connect(this, &Board::colorChanged, this, &Board::shownColorChanged);
    QObject::connect(this, &QAbstractItemModel::modelReset, this, &Board::shownColorChanged);
    QObject::connect(this, &QAbstractItemModel::dataChanged, this,
                     [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                         if (roles.isEmpty() || roles.contains(FillRole))
                             emit shownColorChanged();
                     });
}

QString Board::shownColor() const {
    QString shown;
    bool any = false;
    for (const BoardItem &item : m_items) {
        if (!m_selected.contains(item.id) || item.isConnector())
            continue;
        if (any && item.color != shown)
            return QStringLiteral("mixed");
        shown = item.color;
        any = true;
    }
    return any ? shown : m_color;
}

int Board::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QHash<int, QByteArray> Board::roleNames() const {
    return {{IdRole, "itemId"}, {KindRole, "kind"}, {XRole, "ix"}, {YRole, "iy"}, {WRole, "iw"}, {HRole, "ih"},
            {LabelRole, "label"}, {FillRole, "fill"}, {FontSizeRole, "fontSize"}, {SelectedRole, "selected"},
            {X1Role, "x1"}, {Y1Role, "y1"}, {X2Role, "x2"}, {Y2Role, "y2"}, {RotationRole, "rot"}};
}

QVariant Board::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const BoardItem &item = m_items[index.row()];
    switch (role) {
    case IdRole: return item.id;
    case KindRole: return item.kind;
    case XRole: return item.x;
    case YRole: return item.y;
    case WRole: return item.w;
    case HRole: return item.h;
    case LabelRole: return item.text;
    case FillRole: return item.color;
    case FontSizeRole: return item.fontSize;
    case SelectedRole: return m_selected.contains(item.id);
    case X1Role: return line(item).x1();
    case Y1Role: return line(item).y1();
    case X2Role: return line(item).x2();
    case Y2Role: return line(item).y2();
    case RotationRole: return item.rotation;
    }
    return {};
}

const BoardItem *Board::find(int id) const {
    const int r = row(id);
    return r < 0 ? nullptr : &m_items[r];
}

void Board::reindex() {
    m_rows.clear();
    for (int i = 0; i < m_items.size(); ++i)
        m_rows.insert(m_items[i].id, i);
}

QLineF Board::line(const BoardItem &item) const {
    if (item.isLine())
        return {item.x, item.y, item.x + item.w, item.y + item.h};
    if (item.isConnector()) {
        const BoardItem *a = find(item.from), *b = find(item.to);
        if (a && b)
            return {edge(*a, worldAnchor(*b)), edge(*b, worldAnchor(*a))};
    }
    return {};
}

QRectF Board::bounds(const BoardItem &item) const {
    if (item.isConnector()) {
        const QLineF l = line(item);
        return QRectF(l.p1(), l.p2()).normalized();
    }
    const QRectF box = QRectF(item.x, item.y, item.w, item.h).normalized();
    if (item.rotation == 0 || !item.isShape())
        return box;
    // The upright rectangle a turned shape fits in.
    QPolygonF corners;
    for (const QPointF &corner : {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()})
        corners << toWorld(item, corner);
    return corners.boundingRect();
}

QRectF Board::boundsOf(int id) const {
    const BoardItem *item = find(id);
    return item ? bounds(*item) : QRectF();
}

QRectF Board::selectedBox() const {
    const BoardItem *item = find(selectedId());
    return item ? QRectF(item->x, item->y, item->w, item->h).normalized() : QRectF();
}

double Board::selectedRotation() const {
    const BoardItem *item = find(selectedId());
    return item ? item->rotation : 0;
}

QString Board::selectedKind() const {
    const BoardItem *item = find(selectedId());
    return item ? item->kind : QString();
}

QRectF Board::selectionBounds() const {
    QRectF all;
    bool first = true;
    for (const BoardItem &item : m_items) {
        if (!m_selected.contains(item.id))
            continue;
        // QRectF's union ignores an empty rectangle, and a level line is one.
        const QRectF b = bounds(item);
        if (first) {
            all = b;
            first = false;
        } else {
            all = QRectF(QPointF(std::min(all.left(), b.left()), std::min(all.top(), b.top())),
                         QPointF(std::max(all.right(), b.right()), std::max(all.bottom(), b.bottom())));
        }
    }
    return all;
}

QPointF Board::selectionStart() const {
    const BoardItem *item = find(selectedId());
    return item ? line(*item).p1() : QPointF();
}

QPointF Board::selectionEnd() const {
    const BoardItem *item = find(selectedId());
    return item ? line(*item).p2() : QPointF();
}

int Board::insert(BoardItem item) {
    item.id = m_nextId++;
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(item);
    m_rows.insert(item.id, m_items.size() - 1);
    endInsertRows();
    emit countChanged();
    emit changed();
    return item.id;
}

int Board::add(const QString &kind, double x, double y, double w, double h) {
    if (!kinds().contains(kind) || kind == QLatin1String("connector"))
        return 0;
    checkpoint();
    BoardItem item;
    item.kind = kind;
    item.x = x;
    item.y = y;
    item.w = w;
    item.h = h;
    item.color = colorFor(kind, m_color);
    const int id = insert(item);
    setSelection({id});
    return id;
}

int Board::link(int from, int to) {
    const BoardItem *a = find(from), *b = find(to);
    if (!a || !b || from == to || !a->isShape() || !b->isShape())
        return 0;
    for (const BoardItem &item : m_items)
        if (item.isConnector() && ((item.from == from && item.to == to) || (item.from == to && item.to == from)))
            return item.id;
    checkpoint();
    BoardItem item;
    item.kind = "connector";
    item.from = from;
    item.to = to;
    return insert(item);
}

// Dragging a connection out into empty canvas grows the board: a copy of the
// shape it came from lands there, already connected.
int Board::linkToNew(int from, double x, double y) {
    const BoardItem *source = find(from);
    if (!source || !source->isShape())
        return 0;
    checkpoint();
    BoardItem item = *source;
    item.text.clear();
    item.x = x - item.w / 2;
    item.y = y - item.h / 2;
    const int id = insert(item);
    BoardItem connector;
    connector.kind = "connector";
    connector.from = from;
    connector.to = id;
    insert(connector);
    setSelection({id});
    return id;
}

// Tells the views a set of items moved or changed size, and with them every
// connector that ends on one of them.
void Board::geometryChanged(const QSet<int> &ids) {
    static const QList<int> roles{XRole, YRole, WRole, HRole, X1Role, Y1Role, X2Role, Y2Role, RotationRole};
    for (int i = 0; i < m_items.size(); ++i) {
        const BoardItem &item = m_items[i];
        if (ids.contains(item.id) || (item.isConnector() && (ids.contains(item.from) || ids.contains(item.to))))
            emit dataChanged(index(i), index(i), roles);
    }
    emit selectionGeometryChanged();
}

void Board::setGeometry(int id, double x, double y, double w, double h) {
    const int r = row(id);
    if (r < 0 || m_items[r].isConnector())
        return;
    BoardItem &item = m_items[r];
    if (item.x == x && item.y == y && item.w == w && item.h == h)
        return;
    item.x = x;
    item.y = y;
    item.w = w;
    item.h = h;
    geometryChanged({id});
    emit changed();
}

void Board::moveSelection(double dx, double dy) {
    if (dx == 0 && dy == 0)
        return;
    QSet<int> moved;
    for (BoardItem &item : m_items) {
        if (!m_selected.contains(item.id) || item.isConnector())
            continue;
        item.x += dx;
        item.y += dy;
        moved.insert(item.id);
    }
    if (moved.isEmpty())
        return;
    geometryChanged(moved);
    emit changed();
}

void Board::setText(int id, const QString &text) {
    const int r = row(id);
    if (r < 0 || !m_items[r].isShape() || m_items[r].text == text)
        return;
    m_items[r].text = text;
    emit dataChanged(index(r), index(r), {LabelRole});
    emit changed();
}

void Board::setFontSize(int id, int size) {
    const int r = row(id);
    size = std::clamp(size, 8, 400);
    if (r < 0 || m_items[r].fontSize == size)
        return;
    m_items[r].fontSize = size;
    emit dataChanged(index(r), index(r), {FontSizeRole});
    emit changed();
}

void Board::setRotation(int id, double degrees) {
    const int r = row(id);
    degrees = normalized(degrees);
    if (r < 0 || !m_items[r].isShape() || m_items[r].rotation == degrees)
        return;
    m_items[r].rotation = degrees;
    geometryChanged({id});
    emit changed();
}

// Each shape turns about its own middle, so a group keeps its places.
void Board::rotateSelection(double degrees) {
    QSet<int> turnedIds;
    for (const BoardItem &item : m_items)
        if (m_selected.contains(item.id) && item.isShape())
            turnedIds.insert(item.id);
    if (turnedIds.isEmpty() || degrees == 0)
        return;
    checkpoint();
    for (BoardItem &item : m_items)
        if (turnedIds.contains(item.id))
            item.rotation = normalized(item.rotation + degrees);
    geometryChanged(turnedIds);
    emit changed();
}

// Lines every selected thing up on one side of the box around them all, or
// on its middle. A
// turned shape is lined up by the upright box it fits in, which is the edge
// the eye sees. Connectors follow their shapes, so they aren't moved.
void Board::alignSelection(const QString &edge) {
    QRectF all;
    int found = 0;
    for (const BoardItem &item : m_items) {
        if (!m_selected.contains(item.id) || item.isConnector())
            continue;
        // United by hand, since a level line has an empty rectangle.
        const QRectF b = bounds(item);
        all = found++ == 0 ? b
                           : QRectF(QPointF(std::min(all.left(), b.left()), std::min(all.top(), b.top())),
                                    QPointF(std::max(all.right(), b.right()), std::max(all.bottom(), b.bottom())));
    }
    if (found < 2)
        return;
    auto shift = [&](const BoardItem &item) {
        const QRectF b = bounds(item);
        if (edge == QLatin1String("left")) return QPointF(all.left() - b.left(), 0);
        if (edge == QLatin1String("right")) return QPointF(all.right() - b.right(), 0);
        if (edge == QLatin1String("top")) return QPointF(0, all.top() - b.top());
        if (edge == QLatin1String("bottom")) return QPointF(0, all.bottom() - b.bottom());
        // Centered on each other: across, on the line down the middle of
        // them all, or up and down, on the line across it.
        if (edge == QLatin1String("center")) return QPointF(all.center().x() - b.center().x(), 0);
        if (edge == QLatin1String("middle")) return QPointF(0, all.center().y() - b.center().y());
        return QPointF();
    };
    QSet<int> moved;
    for (const BoardItem &item : m_items)
        if (m_selected.contains(item.id) && !item.isConnector() && !shift(item).isNull())
            moved.insert(item.id);
    if (moved.isEmpty())
        return;
    checkpoint();
    for (BoardItem &item : m_items) {
        if (!moved.contains(item.id))
            continue;
        const QPointF by = shift(item);
        item.x += by.x();
        item.y += by.y();
    }
    geometryChanged(moved);
    emit changed();
}

// Items are drawn in the order they are kept, so the front is the end of the
// list. The selected ones go there together, in the order they already had.
// Connectors are always drawn beneath the shapes and stay where they are.
void Board::reorderSelection(bool toFront) {
    QVector<BoardItem> picked, rest;
    for (const BoardItem &item : m_items)
        (m_selected.contains(item.id) && !item.isConnector() ? picked : rest).append(item);
    const QVector<BoardItem> next = toFront ? rest + picked : picked + rest;
    if (picked.isEmpty() || next == m_items)
        return;
    checkpoint();
    beginResetModel();
    m_items = next;
    reindex();
    endResetModel();
    emit selectionGeometryChanged();
    emit changed();
}

void Board::bringToFront() {
    reorderSelection(true);
}

void Board::sendToBack() {
    reorderSelection(false);
}

// A text item is as large as its text, which only the window can measure. It
// reports back from a delegate that may be in the middle of being created, so
// the model changes on the next turn of the event loop rather than under it.
void Board::setMeasuredSize(int id, double w, double h) {
    QMetaObject::invokeMethod(this, [this, id, w, h] { applyMeasuredSize(id, w, h); }, Qt::QueuedConnection);
}

void Board::applyMeasuredSize(int id, double w, double h) {
    const int r = row(id);
    if (r < 0 || m_items[r].kind != QLatin1String("text") || (m_items[r].w == w && m_items[r].h == h))
        return;
    m_items[r].w = w;
    m_items[r].h = h;
    geometryChanged({id});
}

void Board::setSelectionColor(const QString &color) {
    const QString key = palette().contains(color) ? color : QString();
    if (key != m_color) {
        m_color = key;
        emit colorChanged();
    }
    bool any = false;
    for (const BoardItem &item : m_items)
        any = any || (m_selected.contains(item.id) && !item.isConnector() && item.color != colorFor(item.kind, key));
    if (!any)
        return;
    checkpoint();
    for (int i = 0; i < m_items.size(); ++i) {
        BoardItem &item = m_items[i];
        if (!m_selected.contains(item.id) || item.isConnector() || item.color == colorFor(item.kind, key))
            continue;
        item.color = colorFor(item.kind, key);
        emit dataChanged(index(i), index(i), {FillRole});
    }
    emit changed();
}

void Board::removeIds(QSet<int> ids) {
    // A connector goes with either of its shapes.
    for (const BoardItem &item : m_items)
        if (item.isConnector() && (ids.contains(item.from) || ids.contains(item.to)))
            ids.insert(item.id);
    bool any = false;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!ids.contains(m_items[i].id))
            continue;
        beginRemoveRows({}, i, i);
        m_items.removeAt(i);
        reindex();
        endRemoveRows();
        any = true;
    }
    if (!any)
        return;
    QSet<int> selection = m_selected;
    selection.subtract(ids);
    setSelection(selection);
    emit selectionGeometryChanged();
    emit countChanged();
    emit changed();
}

void Board::removeSelection() {
    if (m_selected.isEmpty())
        return;
    checkpoint();
    removeIds(m_selected);
}

void Board::removeItem(int id) {
    removeIds({id});
}

void Board::setSelection(const QSet<int> &selection) {
    if (selection == m_selected)
        return;
    const QSet<int> before = m_selected;
    m_selected = selection;
    for (int i = 0; i < m_items.size(); ++i)
        if (before.contains(m_items[i].id) != m_selected.contains(m_items[i].id))
            emit dataChanged(index(i), index(i), {SelectedRole});
    emit selectionChanged();
    emit selectionGeometryChanged();
}

void Board::selectOnly(int id) {
    setSelection(find(id) ? QSet<int>{id} : QSet<int>{});
}

void Board::toggle(int id) {
    if (!find(id))
        return;
    QSet<int> selection = m_selected;
    if (!selection.remove(id))
        selection.insert(id);
    setSelection(selection);
}

void Board::clearSelection() {
    setSelection({});
}

void Board::selectAll() {
    QSet<int> all;
    for (const BoardItem &item : m_items)
        all.insert(item.id);
    setSelection(all);
}

void Board::selectInRect(double x, double y, double w, double h, bool additive) {
    const QRectF area = QRectF(x, y, w, h).normalized();
    QSet<int> selection = additive ? m_selected : QSet<int>{};
    for (const BoardItem &item : m_items) {
        const QRectF b = bounds(item);
        // Compared by hand, since a level line has an empty rectangle.
        if (b.left() <= area.right() && b.right() >= area.left() && b.top() <= area.bottom()
            && b.bottom() >= area.top())
            selection.insert(item.id);
    }
    setSelection(selection);
}

// The topmost thing under a point. Connectors are drawn beneath the shapes,
// so they are only hit where nothing else is.
int Board::itemAt(double x, double y, double tolerance) const {
    const QPointF point(x, y);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = m_items.size() - 1; i >= 0; --i) {
            const BoardItem &item = m_items[i];
            if (item.isConnector() != (pass == 1))
                continue;
            bool hit = false;
            const QPointF p = toLocal(item, point);
            if (!item.isShape()) {
                hit = !line(item).isNull() && distanceToSegment(p, line(item)) <= tolerance + 2;
            } else if (item.kind == QLatin1String("ellipse")) {
                const double hw = item.w / 2 + tolerance, hh = item.h / 2 + tolerance;
                const QPointF d = p - anchor(item);
                hit = hw > 0 && hh > 0 && d.x() * d.x() / (hw * hw) + d.y() * d.y() / (hh * hh) <= 1;
            } else if (item.kind == QLatin1String("triangle")) {
                const auto v = triangle(item);
                const double a = cross(v[1] - v[0], p - v[0]), b = cross(v[2] - v[1], p - v[1]),
                             c = cross(v[0] - v[2], p - v[2]);
                hit = (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
            } else {
                hit = QRectF(item.x, item.y, item.w, item.h).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
            }
            if (hit)
                return item.id;
        }
    }
    return 0;
}

// The shape a connection could start or end on: the one under the point, or
// failing that the topmost one whose box is within `margin` of it.
int Board::shapeAt(double x, double y, double margin) const {
    const BoardItem *exact = find(itemAt(x, y, 0));
    if (exact && exact->isShape())
        return exact->id;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        const BoardItem &item = m_items[i];
        if (item.isShape() && bounds(item).adjusted(-margin, -margin, margin, margin).contains(QPointF(x, y)))
            return item.id;
    }
    return 0;
}

// The box around what a drag of the selection moves: everything selected but
// the connectors, which only follow.
QRectF Board::movableBounds() const {
    QRectF all;
    bool first = true;
    for (const BoardItem &item : m_items) {
        if (!m_selected.contains(item.id) || item.isConnector())
            continue;
        const QRectF b = bounds(item);
        all = first ? b
                    : QRectF(QPointF(std::min(all.left(), b.left()), std::min(all.top(), b.top())),
                             QPointF(std::max(all.right(), b.right()), std::max(all.bottom(), b.bottom())));
        first = false;
    }
    return all;
}

// While the selection is dragged, its sides and its middle catch on those of
// the things it passes. Given the box the drag would put it in, this is how
// far to shift it so the nearest of them line up exactly, if any is within
// `threshold`, and the guide lines that show what it lined up with: each runs
// through the edges that meet, from the far end of one thing to the far end
// of the other.
QVariantMap Board::snapOffset(double x, double y, double w, double h, double threshold) const {
    const QRectF moving(x, y, w, h);
    struct Other {
        double across[3];
        double down[3];
        QRectF box;
    };
    QVector<Other> others;
    for (const BoardItem &item : m_items) {
        if (m_selected.contains(item.id) || item.isConnector())
            continue;
        const QRectF b = bounds(item);
        others.append({{b.left(), b.center().x(), b.right()}, {b.top(), b.center().y(), b.bottom()}, b});
    }
    const double across[3] = {moving.left(), moving.center().x(), moving.right()};
    const double down[3] = {moving.top(), moving.center().y(), moving.bottom()};

    // The smallest shift that lines one of ours up with one of theirs.
    double dx = 0, dy = 0, nearestX = threshold, nearestY = threshold;
    for (const Other &other : others) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                const double byX = other.across[j] - across[i], byY = other.down[j] - down[i];
                if (std::abs(byX) <= nearestX) {
                    nearestX = std::abs(byX);
                    dx = byX;
                }
                if (std::abs(byY) <= nearestY) {
                    nearestY = std::abs(byY);
                    dy = byY;
                }
            }
        }
    }

    // One guide for each line that something now shares with the selection,
    // as long as everything on it.
    const QRectF snapped = moving.translated(dx, dy);
    QMap<qint64, QLineF> upright, level;
    auto extend = [](QMap<qint64, QLineF> &guides, double at, double from, double to, bool vertical) {
        const qint64 key = qRound64(at * 100);
        QLineF guide = guides.value(key, vertical ? QLineF(at, from, at, to) : QLineF(from, at, to, at));
        if (vertical)
            guide.setLine(at, std::min(guide.y1(), from), at, std::max(guide.y2(), to));
        else
            guide.setLine(std::min(guide.x1(), from), at, std::max(guide.x2(), to), at);
        guides.insert(key, guide);
    };
    for (const Other &other : others) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                if (dx != 0 || nearestX < threshold || threshold == 0) {
                    if (std::abs(other.across[j] - (across[i] + dx)) < 0.01)
                        extend(upright, other.across[j], std::min(snapped.top(), other.box.top()),
                               std::max(snapped.bottom(), other.box.bottom()), true);
                }
                if (dy != 0 || nearestY < threshold || threshold == 0) {
                    if (std::abs(other.down[j] - (down[i] + dy)) < 0.01)
                        extend(level, other.down[j], std::min(snapped.left(), other.box.left()),
                               std::max(snapped.right(), other.box.right()), false);
                }
            }
        }
    }
    QVariantList guides;
    for (const auto &set : {upright, level})
        for (const QLineF &guide : set)
            guides.append(QVariantMap{{"x1", guide.x1()}, {"y1", guide.y1()}, {"x2", guide.x2()},
                                      {"y2", guide.y2()}});
    return {{"dx", dx}, {"dy", dy}, {"guides", guides}};
}

// While a shape is resized, the side or corner being dragged catches on the
// sides and middles of the other things on the board, so shapes can be made
// to end where their neighbors do. Given where the drag would put that side
// (x, for a left or right one) and that one (y, for a top or bottom one),
// this is how far to shift each onto the nearest line within `threshold`, and
// the guides to show for the lines caught: each runs through everything that
// shares it, the resized shape included.
QVariantMap Board::snapEdges(double x, double y, bool useX, bool useY, double threshold) const {
    struct Other {
        double across[3];
        double down[3];
        QRectF box;
    };
    QVector<Other> others;
    for (const BoardItem &item : m_items) {
        if (m_selected.contains(item.id) || item.isConnector())
            continue;
        const QRectF b = bounds(item);
        others.append({{b.left(), b.center().x(), b.right()}, {b.top(), b.center().y(), b.bottom()}, b});
    }
    double dx = 0, dy = 0, nearestX = threshold, nearestY = threshold;
    bool caughtX = false, caughtY = false;
    for (const Other &other : others) {
        for (int j = 0; j < 3; ++j) {
            if (useX && std::abs(other.across[j] - x) <= nearestX) {
                nearestX = std::abs(other.across[j] - x);
                dx = other.across[j] - x;
                caughtX = true;
            }
            if (useY && std::abs(other.down[j] - y) <= nearestY) {
                nearestY = std::abs(other.down[j] - y);
                dy = other.down[j] - y;
                caughtY = true;
            }
        }
    }

    // The resized shape reaches from the side across from the one dragged, so
    // its own extent on a guide is taken from where it is now and from where
    // the dragged side lands.
    const QRectF own = movableBounds();
    const double atX = x + dx, atY = y + dy;
    QVariantList guides;
    if (caughtX) {
        double from = std::min(own.top(), useY ? atY : own.top()), to = std::max(own.bottom(), useY ? atY : own.bottom());
        for (const Other &other : others)
            for (int j = 0; j < 3; ++j)
                if (std::abs(other.across[j] - atX) < 0.01) {
                    from = std::min(from, other.box.top());
                    to = std::max(to, other.box.bottom());
                }
        guides.append(QVariantMap{{"x1", atX}, {"y1", from}, {"x2", atX}, {"y2", to}});
    }
    if (caughtY) {
        double from = std::min(own.left(), useX ? atX : own.left()), to = std::max(own.right(), useX ? atX : own.right());
        for (const Other &other : others)
            for (int j = 0; j < 3; ++j)
                if (std::abs(other.down[j] - atY) < 0.01) {
                    from = std::min(from, other.box.left());
                    to = std::max(to, other.box.right());
                }
        guides.append(QVariantMap{{"x1", from}, {"y1", atY}, {"x2", to}, {"y2", atY}});
    }
    return {{"dx", dx}, {"dy", dy}, {"guides", guides}};
}

// The angles the shapes that aren't selected are turned to, each once. A
// shape being turned catches on them, so it can be set at the angle of
// another.
QVariantList Board::otherRotations() const {
    QVariantList angles;
    for (const BoardItem &item : m_items)
        if (item.isShape() && item.rotation != 0 && !m_selected.contains(item.id) && !angles.contains(item.rotation))
            angles.append(item.rotation);
    return angles;
}

// Which side of a shape a point on its outline belongs to (l, r, t, or b, as
// the shape has them before it is turned), or nothing when the point is not
// within reach of the outline. A drag from there resizes the shape that way.
// A circle's outline is split where its diagonals cross it; a triangle has a
// bottom and two slanted sides, and no top.
QString Board::outlineSide(int id, double x, double y, double reach) const {
    const BoardItem *item = find(id);
    if (!item || !item->isShape() || item->kind == QLatin1String("text"))
        return {};
    // A small shape keeps a middle to be moved by.
    reach = std::min({reach, item->w / 4, item->h / 4});
    const QPointF p = toLocal(*item, {x, y});
    const double left = item->x, right = item->x + item->w, top = item->y, bottom = item->y + item->h;
    if (item->kind == QLatin1String("ellipse")) {
        const double hw = item->w / 2, hh = item->h / 2;
        if (hw <= 0 || hh <= 0)
            return {};
        const double dx = (p.x() - left - hw) / hw, dy = (p.y() - top - hh) / hh;
        if (std::abs(std::hypot(dx, dy) - 1) * std::min(hw, hh) > reach)
            return {};
        return std::abs(dx) >= std::abs(dy) ? (dx < 0 ? "l" : "r") : (dy < 0 ? "t" : "b");
    }
    if (item->kind == QLatin1String("triangle")) {
        const auto v = triangle(*item);
        if (distanceToSegment(p, {v[1], v[2]}) <= reach)
            return "b";
        if (distanceToSegment(p, {v[0], v[1]}) <= reach)
            return "r";
        if (distanceToSegment(p, {v[2], v[0]}) <= reach)
            return "l";
        return {};
    }
    const bool alongX = p.x() >= left - reach && p.x() <= right + reach;
    const bool alongY = p.y() >= top - reach && p.y() <= bottom + reach;
    if (alongY && std::abs(p.x() - left) <= reach) return "l";
    if (alongY && std::abs(p.x() - right) <= reach) return "r";
    if (alongX && std::abs(p.y() - top) <= reach) return "t";
    if (alongX && std::abs(p.y() - bottom) <= reach) return "b";
    return {};
}

QVariantMap Board::item(int id) const {
    const BoardItem *item = find(id);
    if (!item)
        return {};
    return {{"id", item->id}, {"kind", item->kind}, {"x", item->x}, {"y", item->y}, {"w", item->w},
            {"h", item->h}, {"text", item->text}, {"color", item->color}, {"fontSize", item->fontSize},
            {"rotation", item->rotation}};
}

QPointF Board::edgePoint(int id, double towardX, double towardY) const {
    const BoardItem *item = find(id);
    return item ? edge(*item, {towardX, towardY}) : QPointF(towardX, towardY);
}

QRectF Board::contentBounds() const {
    QRectF all;
    bool first = true;
    for (const BoardItem &item : m_items) {
        if (item.isConnector())
            continue;
        const QRectF b = bounds(item);
        if (first) {
            all = b;
            first = false;
        } else {
            all = QRectF(QPointF(std::min(all.left(), b.left()), std::min(all.top(), b.top())),
                         QPointF(std::max(all.right(), b.right()), std::max(all.bottom(), b.bottom())));
        }
    }
    return all;
}

// Remembers the board as it is now, as the step undo goes back to. A gesture
// that ends up changing nothing leaves a snapshot equal to the board, which
// undo steps over.
void Board::checkpoint() {
    const Snapshot now{m_items, m_nextId};
    if (!m_undo.isEmpty() && m_undo.last() == now)
        return;
    m_undo.append(now);
    if (m_undo.size() > undoLimit)
        m_undo.removeFirst();
    m_redo.clear();
    emit undoChanged();
}

void Board::restore(const Snapshot &snapshot) {
    beginResetModel();
    m_items = snapshot.items;
    m_nextId = snapshot.nextId;
    reindex();
    QSet<int> selection;
    for (int id : std::as_const(m_selected))
        if (m_rows.contains(id))
            selection.insert(id);
    m_selected = selection;
    endResetModel();
    emit countChanged();
    emit selectionChanged();
    emit selectionGeometryChanged();
    emit undoChanged();
    emit changed();
}

void Board::undo() {
    const Snapshot now{m_items, m_nextId};
    while (!m_undo.isEmpty() && m_undo.last() == now)
        m_undo.removeLast();
    if (m_undo.isEmpty()) {
        emit undoChanged();
        return;
    }
    m_redo.append(now);
    restore(m_undo.takeLast());
}

void Board::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append({m_items, m_nextId});
    restore(m_redo.takeLast());
}

// The selected items, and the connectors that run between two of them.
QJsonArray Board::selectionJson() const {
    QJsonArray array;
    for (const BoardItem &item : m_items)
        if (!item.isConnector() && m_selected.contains(item.id))
            array.append(itemToJson(item));
    for (const BoardItem &item : m_items)
        if (item.isConnector() && m_selected.contains(item.from) && m_selected.contains(item.to))
            array.append(itemToJson(item));
    return array;
}

bool Board::insertJson(const QJsonArray &array, const QPointF &center, bool atCenter) {
    // Connectors go in after everything they join.
    QVector<BoardItem> incoming, connectors;
    QRectF all;
    for (const auto &value : array) {
        const BoardItem item = itemFromJson(value.toObject());
        if (!kinds().contains(item.kind))
            continue;
        if (item.isConnector()) {
            connectors.append(item);
            continue;
        }
        const QRectF b = QRectF(item.x, item.y, item.w, item.h).normalized();
        all = incoming.isEmpty() ? b
                                 : QRectF(QPointF(std::min(all.left(), b.left()), std::min(all.top(), b.top())),
                                          QPointF(std::max(all.right(), b.right()),
                                                  std::max(all.bottom(), b.bottom())));
        incoming.append(item);
    }
    if (incoming.isEmpty())
        return false;
    incoming += connectors;
    const QPointF offset = atCenter ? center - all.center() : QPointF(24, 24);

    checkpoint();
    QHash<int, int> renamed;
    QSet<int> selection;
    for (BoardItem item : incoming) {
        if (item.isConnector()) {
            if (!renamed.contains(item.from) || !renamed.contains(item.to))
                continue;
            item.from = renamed.value(item.from);
            item.to = renamed.value(item.to);
            insert(item);
        } else {
            const int old = item.id;
            item.x += offset.x();
            item.y += offset.y();
            const int id = insert(item);
            renamed.insert(old, id);
            selection.insert(id);
        }
    }
    setSelection(selection);
    return true;
}

void Board::copy() {
    const QJsonArray array = selectionJson();
    if (array.isEmpty())
        return;
    auto *data = new QMimeData;
    data->setData(clipboardFormat, QJsonDocument(array).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(data);
}

bool Board::paste(double x, double y) {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasFormat(clipboardFormat))
        return false;
    return insertJson(QJsonDocument::fromJson(data->data(clipboardFormat)).array(), {x, y}, true);
}

bool Board::duplicate(double x, double y, bool atPoint) {
    return insertJson(selectionJson(), {x, y}, atPoint);
}

void Board::setView(double x, double y, double zoom) {
    if (m_hasView && x == m_viewX && y == m_viewY && zoom == m_viewZoom)
        return;
    m_hasView = true;
    m_viewX = x;
    m_viewY = y;
    m_viewZoom = zoom;
    emit changed();
}

QJsonObject Board::toJson() const {
    QJsonArray array;
    for (const BoardItem &item : m_items)
        array.append(itemToJson(item));
    QJsonObject o{{"version", 1}, {"nextId", m_nextId}, {"items", array}};
    if (m_hasView)
        o["view"] = QJsonObject{{"x", m_viewX}, {"y", m_viewY}, {"zoom", m_viewZoom}};
    return o;
}

void Board::load(const QJsonObject &object) {
    beginResetModel();
    m_items.clear();
    m_selected.clear();
    m_undo.clear();
    m_redo.clear();
    int highest = 0;
    QSet<int> seen;
    const QJsonArray array = object.value("items").toArray();
    for (const auto &value : array) {
        const BoardItem item = itemFromJson(value.toObject());
        if (item.id <= 0 || seen.contains(item.id) || !kinds().contains(item.kind))
            continue;
        seen.insert(item.id);
        highest = std::max(highest, item.id);
        m_items.append(item);
    }
    // A connector whose shape is missing has nothing to draw between.
    m_items.removeIf([&](const BoardItem &item) {
        return item.isConnector() && (!seen.contains(item.from) || !seen.contains(item.to));
    });
    m_nextId = std::max(object.value("nextId").toInt(1), highest + 1);
    reindex();

    const QJsonObject view = object.value("view").toObject();
    const double zoom = view.value("zoom").toDouble();
    m_hasView = zoom > 0;
    m_viewX = view.value("x").toDouble();
    m_viewY = view.value("y").toDouble();
    m_viewZoom = m_hasView ? zoom : 1;
    endResetModel();

    emit countChanged();
    emit selectionChanged();
    emit selectionGeometryChanged();
    emit undoChanged();
    emit loaded();
}
