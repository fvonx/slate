#pragma once

#include <QQuickItem>

// The movement of two fingers during a touchpad pinch. A touchpad decides for
// itself whether two fingers scroll or pinch, and PinchHandler takes only the
// zoom from a pinch, so a pan the touchpad took for one would be lost. This
// item reports how far the fingers moved, and leaves the zoom to the handler.
class PinchPan : public QQuickItem {
    Q_OBJECT

public:
    using QQuickItem::QQuickItem;

    // Makes `import Slate` give QML the PinchPan type.
    static void registerType();

signals:
    void moved(qreal dx, qreal dy);

protected:
    bool event(QEvent *event) override;
};
