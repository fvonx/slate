#include "boardexport.h"

#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <cmath>

#include "board.h"
#include "icons.h"

namespace {

const double margin = 40;

// One color laid over another at a strength, as the window tints a shape
// with its color over the background.
QColor tinted(const QColor &base, const QColor &over, double strength) {
    return QColor::fromRgbF(base.redF() * (1 - strength) + over.redF() * strength,
                            base.greenF() * (1 - strength) + over.greenF() * strength,
                            base.blueF() * (1 - strength) + over.blueF() * strength);
}

// Black or white, whichever reads on a post-it's paper.
QColor readable(const QColor &paper) {
    return 0.299 * paper.redF() + 0.587 * paper.greenF() + 0.114 * paper.blueF() > 0.55 ? QColor(Qt::black)
                                                                                        : QColor(Qt::white);
}

QFont fontOf(int pixelSize) {
    QFont font = QGuiApplication::font();
    font.setPixelSize(pixelSize);
    return font;
}

void paintStroke(QPainter &painter, const QLineF &line, const QColor &ink, bool arrow) {
    const double length = line.length();
    if (length <= 0)
        return;
    const QPointF along = (line.p2() - line.p1()) / length;
    painter.setPen(QPen(ink, 2, Qt::SolidLine, Qt::RoundCap));
    // An arrow's line stops inside its head, so the tip stays sharp.
    painter.drawLine(line.p1(), arrow ? line.p2() - along * 6 : line.p2());
    if (!arrow || length <= 14)
        return;
    const QPointF across(-along.y(), along.x()), back = line.p2() - along * 13;
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawPolygon(QPolygonF{line.p2(), back + across * 6, back - across * 6});
}

void paintLabel(QPainter &painter, const BoardItem &item, const QColor &color) {
    if (item.text.isEmpty())
        return;
    // Centered in the shape and wrapped to its width; in a triangle, in the
    // wide part below the middle.
    const bool triangle = item.kind == QLatin1String("triangle");
    const double width = std::max(0.0, triangle ? item.w / 2 : item.w - 16);
    const QPointF middle(item.x + item.w / 2, item.y + item.h / 2 + (triangle ? item.h / 6 : 0));
    painter.setFont(fontOf(18));
    painter.setPen(color);
    painter.drawText(QRectF(middle.x() - width / 2, middle.y() - 2000, width, 4000),
                     Qt::AlignCenter | Qt::TextWordWrap, item.text);
}

void paintShape(QPainter &painter, const BoardItem &item, const BoardLook &look) {
    const QColor ink = item.color.isEmpty() ? look.foreground : look.colors.value(item.color, look.foreground);
    const QRectF box(item.x, item.y, item.w, item.h);
    painter.save();
    painter.translate(box.center());
    painter.rotate(item.rotation);
    painter.translate(-box.center());

    if (item.kind == QLatin1String("icon")) {
        // The same outline the window draws, square in the middle of the
        // icon's box, through Qt's own SVG renderer.
        const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='"
                             + iconPath(item.icon).toLatin1() + "' fill='none' stroke='" + ink.name().toLatin1()
                             + "' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/></svg>";
        QSvgRenderer renderer(svg);
        const double side = std::min(item.w, item.h);
        renderer.render(&painter, QRectF(box.center().x() - side / 2, box.center().y() - side / 2, side, side));
    } else if (item.kind == QLatin1String("text")) {
        painter.setFont(fontOf(item.fontSize));
        painter.setPen(ink);
        painter.drawText(QRectF(item.x, item.y, 100000, 100000), Qt::AlignLeft | Qt::AlignTop, item.text);
    } else if (item.kind == QLatin1String("postit")) {
        const QColor paper = look.colors.value(item.color.isEmpty() ? QStringLiteral("yellow") : item.color,
                                               look.foreground);
        const double corner = std::min(2, look.radius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, look.light ? 46 : 115));
        painter.drawRoundedRect(box.adjusted(2, 4, 2, 5), corner, corner);
        painter.setBrush(paper);
        painter.drawRoundedRect(box, corner, corner);
        painter.setBrush(QColor(0, 0, 0, 20));
        painter.drawRoundedRect(QRectF(item.x, item.y, item.w, std::min(item.h, 14.0)), corner, corner);
        paintLabel(painter, item, readable(paper));
    } else {
        // The outline is drawn inside the shape's box, as the window draws it.
        painter.setPen(QPen(ink, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(tinted(look.background, ink, item.color.isEmpty() ? 0.06 : 0.28));
        const QRectF inner = box.adjusted(1, 1, -1, -1);
        if (item.kind == QLatin1String("ellipse")) {
            painter.drawEllipse(inner);
        } else if (item.kind == QLatin1String("triangle")) {
            painter.drawPolygon(QPolygonF{{box.center().x(), inner.top()}, inner.bottomRight(), inner.bottomLeft()});
        } else {
            const double corner = std::max(0.0, std::min({double(look.radius), item.w / 2, item.h / 2}) - 1);
            painter.drawRoundedRect(inner, corner, corner);
        }
        paintLabel(painter, item, look.foreground);
    }
    painter.restore();
}

} // namespace

QRectF exportArea(const Board &board) {
    QRectF all = board.contentBounds();
    // A turned or lone text can leave an empty rectangle; give it some room.
    if (all.width() < 1)
        all.setWidth(1);
    if (all.height() < 1)
        all.setHeight(1);
    return all.adjusted(-margin, -margin, margin, margin);
}

void paintBoard(QPainter &painter, const Board &board, const BoardLook &look) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    // Connectors lie beneath everything else; the rest is drawn in its order.
    for (const BoardItem &item : board.items())
        if (item.isConnector())
            paintStroke(painter, board.line(item), look.foreground, true);
    for (const BoardItem &item : board.items()) {
        if (item.isConnector())
            continue;
        if (item.isLine())
            paintStroke(painter, board.line(item),
                        item.color.isEmpty() ? look.foreground : look.colors.value(item.color, look.foreground),
                        false);
        else
            paintShape(painter, item, look);
    }
}

bool exportBoard(const Board &board, const BoardLook &look, const QString &format, const QString &title,
                 const QString &path) {
    const QRectF area = exportArea(board);
    // Through a QSaveFile, so a half-written file never takes a name, and a
    // folder that can't be written to fails the export rather than leaving
    // an empty file behind.
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    auto paint = [&](QPaintDevice *device) {
        QPainter painter;
        if (!painter.begin(device))
            return false;
        painter.fillRect(QRectF(QPointF(0, 0), area.size()), look.background);
        painter.translate(-area.topLeft());
        paintBoard(painter, board, look);
        return painter.end();
    };

    bool painted = false;
    if (format == QLatin1String("pdf")) {
        // One page, exactly as large as the board, a canvas pixel to a
        // ninety-sixth of an inch.
        QPdfWriter writer(&file);
        writer.setResolution(96);
        writer.setPageSize(QPageSize(area.size() * 72.0 / 96.0, QPageSize::Point));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setTitle(title);
        painted = paint(&writer);
    } else if (format == QLatin1String("svg")) {
        QSvgGenerator generator;
        generator.setOutputDevice(&file);
        generator.setSize(area.size().toSize());
        generator.setViewBox(QRectF(QPointF(0, 0), area.size()));
        generator.setTitle(title);
        painted = paint(&generator);
    }
    if (!painted) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
