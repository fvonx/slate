#pragma once

#include <QColor>
#include <QQuickItem>

// The canvas's dotted background. The dots are built once for the window's
// size and then only slid by the pan offset, so panning costs a matrix and
// not a rebuild.
class DotGrid : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal offsetX READ offsetX WRITE setOffsetX NOTIFY changed)
    Q_PROPERTY(qreal offsetY READ offsetY WRITE setOffsetY NOTIFY changed)
    Q_PROPERTY(qreal step READ step WRITE setStep NOTIFY changed)
    Q_PROPERTY(qreal dotSize READ dotSize WRITE setDotSize NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)

public:
    explicit DotGrid(QQuickItem *parent = nullptr);

    // Makes `import Slate` give QML the DotGrid type.
    static void registerType();

    qreal offsetX() const { return m_offsetX; }
    qreal offsetY() const { return m_offsetY; }
    qreal step() const { return m_step; }
    qreal dotSize() const { return m_dotSize; }
    QColor color() const { return m_color; }
    void setOffsetX(qreal value);
    void setOffsetY(qreal value);
    void setStep(qreal value);
    void setDotSize(qreal value);
    void setColor(const QColor &value);

signals:
    void changed();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    qreal m_offsetX = 0, m_offsetY = 0, m_step = 24, m_dotSize = 2;
    QColor m_color;
    bool m_rebuild = true, m_recolor = true;
};
