#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QVariantMap>
#include <QVector>

// One thing on a board. A line keeps its start in x, y and its end as the
// signed offset w, h; a connector keeps only the two shapes it joins, and its
// ends are worked out from where they are now.
struct BoardItem {
    int id = 0;
    QString kind; // rect, ellipse, triangle, postit, icon, frame, text, line, connector
    double x = 0, y = 0, w = 0, h = 0;
    QString text;
    QString color; // a theme palette key, or empty for the foreground
    QString stroke; // how the outline is drawn: empty for solid, dashed, or dotted
    QString icon; // which icon, for an icon
    int fontSize = 24;
    double rotation = 0; // degrees, about the middle of its box
    int from = 0, to = 0;
    // Where a connector's arrow is: empty for the end it was drawn to,
    // "start" for the one it was drawn from, "none" for no arrow.
    QString arrow;
    int group = 0; // the group it is part of, or 0: grouped things are selected and arranged as one

    bool isConnector() const { return kind == QLatin1String("connector"); }
    bool isLine() const { return kind == QLatin1String("line"); }
    bool isShape() const { return !isConnector() && !isLine(); }
    bool isFrame() const { return kind == QLatin1String("frame"); }
    // The outlines that can be dashed or dotted: the drawn shapes, the frame,
    // the line, and the connector.
    bool takesStroke() const {
        return kind == QLatin1String("rect") || kind == QLatin1String("ellipse") || kind == QLatin1String("triangle")
            || isFrame() || isLine() || isConnector();
    }
    bool operator==(const BoardItem &o) const {
        return id == o.id && kind == o.kind && x == o.x && y == o.y && w == o.w && h == o.h && text == o.text
            && color == o.color && stroke == o.stroke && icon == o.icon && fontSize == o.fontSize && rotation == o.rotation && from == o.from && to == o.to && arrow == o.arrow && group == o.group;
    }
};

// The open canvas: its items as a list model for QML, the selection, undo,
// and the geometry (hit tests, where a connector meets a shape).
class Board : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(int selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY selectionChanged)
    Q_PROPERTY(QRectF selectionBounds READ selectionBounds NOTIFY selectionGeometryChanged)
    Q_PROPERTY(QRectF selectedBox READ selectedBox NOTIFY selectionGeometryChanged)
    Q_PROPERTY(double selectedRotation READ selectedRotation NOTIFY selectionGeometryChanged)
    Q_PROPERTY(QPointF selectionStart READ selectionStart NOTIFY selectionGeometryChanged)
    Q_PROPERTY(QPointF selectionEnd READ selectionEnd NOTIFY selectionGeometryChanged)
    Q_PROPERTY(QString color READ color NOTIFY colorChanged)
    Q_PROPERTY(QStringList iconNames READ iconNames CONSTANT)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY selectionChanged)
    Q_PROPERTY(QRectF groupBounds READ movableBounds NOTIFY selectionGeometryChanged)
    Q_PROPERTY(QString shownColor READ shownColor NOTIFY shownColorChanged)
    Q_PROPERTY(QString shownStroke READ shownStroke NOTIFY shownStrokeChanged)
    Q_PROPERTY(QString shownArrow READ shownArrow NOTIFY shownArrowChanged)
    Q_PROPERTY(bool connectorSelected READ connectorSelected NOTIFY selectionChanged)
    Q_PROPERTY(int arrangeCount READ arrangeCount NOTIFY groupingChanged)
    Q_PROPERTY(bool canGroup READ canGroup NOTIFY groupingChanged)
    Q_PROPERTY(bool canUngroup READ canUngroup NOTIFY groupingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(bool hasView READ hasView NOTIFY loaded)
    Q_PROPERTY(double viewX READ viewX NOTIFY loaded)
    Q_PROPERTY(double viewY READ viewY NOTIFY loaded)
    Q_PROPERTY(double viewZoom READ viewZoom NOTIFY loaded)

public:
    enum Role {
        IdRole = Qt::UserRole + 1, KindRole, XRole, YRole, WRole, HRole, LabelRole, FillRole, FontSizeRole,
        SelectedRole, X1Role, Y1Role, X2Role, Y2Role, RotationRole, IconRole, StrokeRole, ArrowRole
    };

    explicit Board(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    int selectionCount() const { return m_selected.size(); }
    int selectedId() const { return m_selected.size() == 1 ? *m_selected.begin() : 0; }
    QString selectedKind() const;
    QRectF selectionBounds() const;
    // The one selected shape as it is before it is turned, and how far it is.
    QRectF selectedBox() const;
    double selectedRotation() const;
    QPointF selectionStart() const;
    QPointF selectionEnd() const;
    QString color() const { return m_color; }
    // How many things are selected that can be moved or sized, connectors
    // aside: two or more are sized together, by the box around them.
    int groupCount() const;
    // How many things there are to line up or space out: a group counts as
    // one, and so does each thing that is in none.
    int arrangeCount() const;
    // Two or more things that aren't one group already can be grouped, and
    // anything in a group can be taken out of it.
    bool canGroup() const;
    bool canUngroup() const;
    QStringList iconNames() const;
    Q_INVOKABLE QString iconPath(const QString &name) const;
    // The color the color menu marks: that of what is selected, when it all
    // has one ("mixed" when it doesn't), or the one new shapes will take.
    QString shownColor() const;
    // The same for the outline's style: solid (empty), dashed, dotted, or
    // "mixed". Only what has an outline to style counts.
    QString shownStroke() const;
    // And for where the arrow is: that of the selected connections (empty
    // for the end, "start", "none", or "mixed"), or what the next one takes.
    QString shownArrow() const;
    // Whether a connection is among what is selected.
    bool connectorSelected() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool hasView() const { return m_hasView; }
    double viewX() const { return m_viewX; }
    double viewY() const { return m_viewY; }
    double viewZoom() const { return m_viewZoom; }

    const QVector<BoardItem> &items() const { return m_items; }
    const BoardItem *find(int id) const;
    QLineF line(const BoardItem &item) const;
    QRectF bounds(const BoardItem &item) const;

    // Adding, and everything else that is one step to the user, checkpoints
    // itself. The live ones a gesture repeats (setGeometry, moveSelection,
    // setText, setFontSize) don't: the window checkpoints once as it starts.
    Q_INVOKABLE int add(const QString &kind, double x, double y, double w, double h);
    Q_INVOKABLE int addIcon(const QString &name, double x, double y, double w, double h);
    Q_INVOKABLE int link(int from, int to);
    Q_INVOKABLE int linkToNew(int from, double x, double y);
    Q_INVOKABLE void setGeometry(int id, double x, double y, double w, double h);
    Q_INVOKABLE void moveSelection(double dx, double dy);
    // A drag of the selection takes along what is in its frames as it
    // starts, so the frames pick nothing more up on the way.
    Q_INVOKABLE void beginMove();
    Q_INVOKABLE void endMove();
    Q_INVOKABLE void setText(int id, const QString &text);
    Q_INVOKABLE void setFontSize(int id, int size);
    Q_INVOKABLE void setRotation(int id, double degrees);
    Q_INVOKABLE void rotateSelection(double degrees);
    // Sizing several things at once: remember them as they are, then put
    // them, each time the pointer moves, in a new box in place of the old.
    Q_INVOKABLE void beginScale();
    Q_INVOKABLE void scaleSelectionTo(double x, double y, double w, double h);
    // Lines the selection up on a side (left, right, top, bottom) or on its
    // middle (center, middle), or spaces three or more evenly between the
    // outermost two (spreadAcross, spreadDown).
    Q_INVOKABLE void alignSelection(const QString &edge);
    // Grouped things are selected together from then on, so they move, size,
    // line up, and change places in the pile as one. Groups among them are
    // taken into the new one; ungrouping lets every one go its own way.
    Q_INVOKABLE void groupSelection();
    Q_INVOKABLE void ungroupSelection();
    Q_INVOKABLE void bringToFront();
    Q_INVOKABLE void sendToBack();
    Q_INVOKABLE void setMeasuredSize(int id, double w, double h);
    Q_INVOKABLE void setSelectionColor(const QString &color);
    // Solid (empty), dashed, or dotted, for the selection and for what is
    // drawn next.
    Q_INVOKABLE void setSelectionStroke(const QString &stroke);
    // Where the arrow is on the selected connections, and on the next drawn.
    Q_INVOKABLE void setSelectionArrow(const QString &arrow);
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE void removeItem(int id);

    Q_INVOKABLE void selectOnly(int id);
    Q_INVOKABLE void toggle(int id);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectInRect(double x, double y, double w, double h, bool additive);
    Q_INVOKABLE bool isSelected(int id) const { return m_selected.contains(id); }

    Q_INVOKABLE int itemAt(double x, double y, double tolerance) const;
    Q_INVOKABLE int shapeAt(double x, double y, double margin) const;
    // The frame whose name, `height` tall above its top, is under a point.
    Q_INVOKABLE int frameLabelAt(double x, double y, double height) const;
    // What is in a frame: everything that lies wholly inside it.
    QSet<int> contents(int frameId) const;
    Q_INVOKABLE QVariantMap item(int id) const;
    Q_INVOKABLE QRectF boundsOf(int id) const;
    Q_INVOKABLE QRectF movableBounds() const;
    Q_INVOKABLE QVariantMap snapOffset(double x, double y, double w, double h, double threshold) const;
    Q_INVOKABLE QVariantMap snapEdges(double x, double y, bool useX, bool useY, double threshold) const;
    Q_INVOKABLE QVariantList otherRotations() const;
    Q_INVOKABLE QString outlineSide(int id, double x, double y, double reach) const;
    Q_INVOKABLE QPointF edgePoint(int id, double towardX, double towardY) const;
    Q_INVOKABLE QRectF contentBounds() const;

    Q_INVOKABLE void checkpoint();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void copy();
    Q_INVOKABLE bool paste(double x, double y);
    Q_INVOKABLE bool duplicate(double x, double y, bool atPoint);

    Q_INVOKABLE void setView(double x, double y, double zoom);

    QJsonObject toJson() const;
    void load(const QJsonObject &object);

signals:
    void countChanged();
    void selectionChanged();
    void selectionGeometryChanged();
    void colorChanged();
    void shownColorChanged();
    void shownStrokeChanged();
    void shownArrowChanged();
    void undoChanged();
    void groupingChanged();
    // A board was opened: the window takes its saved view.
    void loaded();
    // Something worth saving changed.
    void changed();

private:
    struct Snapshot {
        QVector<BoardItem> items;
        int nextId = 1;
        bool operator==(const Snapshot &o) const { return nextId == o.nextId && items == o.items; }
    };

    int row(int id) const { return m_rows.value(id, -1); }
    void reindex();
    int insert(BoardItem item);
    void removeIds(QSet<int> ids);
    void setSelection(const QSet<int> &selection);
    // The ids, and everything grouped with any of them.
    QSet<int> withGroups(const QSet<int> &ids) const;
    // A group of one is no group.
    void pruneGroups();
    void geometryChanged(const QSet<int> &ids);
    void applyMeasuredSize(int id, double w, double h);
    void reorderSelection(bool toFront);
    void settleFrames();
    QSet<int> withContents(const QSet<int> &ids) const;
    void restore(const Snapshot &snapshot);
    QJsonArray selectionJson() const;
    bool insertJson(const QJsonArray &array, const QPointF &center, bool atCenter);

    QVector<BoardItem> m_items;
    QVector<BoardItem> m_scaleItems;
    QSet<int> m_carried;
    bool m_moving = false;
    QRectF m_scaleFrom;
    QHash<int, int> m_rows;
    QSet<int> m_selected;
    int m_nextId = 1;
    QString m_color;
    QString m_stroke;
    QString m_arrow;
    QVector<Snapshot> m_undo, m_redo;
    bool m_hasView = false;
    double m_viewX = 0, m_viewY = 0, m_viewZoom = 1;
};
