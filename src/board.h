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
    QString kind; // rect, ellipse, triangle, postit, icon, text, line, connector
    double x = 0, y = 0, w = 0, h = 0;
    QString text;
    QString color; // a theme palette key, or empty for the foreground
    QString icon; // which icon, for an icon
    int fontSize = 24;
    double rotation = 0; // degrees, about the middle of its box
    int from = 0, to = 0;

    bool isConnector() const { return kind == QLatin1String("connector"); }
    bool isLine() const { return kind == QLatin1String("line"); }
    bool isShape() const { return !isConnector() && !isLine(); }
    bool operator==(const BoardItem &o) const {
        return id == o.id && kind == o.kind && x == o.x && y == o.y && w == o.w && h == o.h && text == o.text
            && color == o.color && icon == o.icon && fontSize == o.fontSize && rotation == o.rotation && from == o.from && to == o.to;
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
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(bool hasView READ hasView NOTIFY loaded)
    Q_PROPERTY(double viewX READ viewX NOTIFY loaded)
    Q_PROPERTY(double viewY READ viewY NOTIFY loaded)
    Q_PROPERTY(double viewZoom READ viewZoom NOTIFY loaded)

public:
    enum Role {
        IdRole = Qt::UserRole + 1, KindRole, XRole, YRole, WRole, HRole, LabelRole, FillRole, FontSizeRole,
        SelectedRole, X1Role, Y1Role, X2Role, Y2Role, RotationRole, IconRole
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
    QStringList iconNames() const;
    Q_INVOKABLE QString iconPath(const QString &name) const;
    // The color the color menu marks: that of what is selected, when it all
    // has one ("mixed" when it doesn't), or the one new shapes will take.
    QString shownColor() const;
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
    Q_INVOKABLE void setText(int id, const QString &text);
    Q_INVOKABLE void setFontSize(int id, int size);
    Q_INVOKABLE void setRotation(int id, double degrees);
    Q_INVOKABLE void rotateSelection(double degrees);
    // Sizing several things at once: remember them as they are, then put
    // them, each time the pointer moves, in a new box in place of the old.
    Q_INVOKABLE void beginScale();
    Q_INVOKABLE void scaleSelectionTo(double x, double y, double w, double h);
    Q_INVOKABLE void alignSelection(const QString &edge);
    Q_INVOKABLE void bringToFront();
    Q_INVOKABLE void sendToBack();
    Q_INVOKABLE void setMeasuredSize(int id, double w, double h);
    Q_INVOKABLE void setSelectionColor(const QString &color);
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
    void undoChanged();
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
    void geometryChanged(const QSet<int> &ids);
    void applyMeasuredSize(int id, double w, double h);
    void reorderSelection(bool toFront);
    void restore(const Snapshot &snapshot);
    QJsonArray selectionJson() const;
    bool insertJson(const QJsonArray &array, const QPointF &center, bool atCenter);

    QVector<BoardItem> m_items;
    QVector<BoardItem> m_scaleItems;
    QRectF m_scaleFrom;
    QHash<int, int> m_rows;
    QSet<int> m_selected;
    int m_nextId = 1;
    QString m_color;
    QVector<Snapshot> m_undo, m_redo;
    bool m_hasView = false;
    double m_viewX = 0, m_viewY = 0, m_viewZoom = 1;
};
