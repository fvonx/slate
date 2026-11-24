#include "pinchpan.h"

#include <QNativeGestureEvent>
#include <QQmlEngine>

void PinchPan::registerType() {
    qmlRegisterType<PinchPan>("Slate", 1, 0, "PinchPan");
}

bool PinchPan::event(QEvent *event) {
    if (event->type() != QEvent::NativeGesture)
        return QQuickItem::event(event);
    auto *gesture = static_cast<QNativeGestureEvent *>(event);
    const QPointF delta = gesture->delta();
    // A platform sends the movement either along with the zoom or as a pan of
    // its own. Three fingers and more are a swipe, which is the desktop's.
    const bool pan = gesture->gestureType() == Qt::PanNativeGesture && gesture->fingerCount() <= 2;
    if ((pan || gesture->gestureType() == Qt::ZoomNativeGesture) && !delta.isNull())
        emit moved(delta.x(), delta.y());
    // Only a pan ends here; the rest goes on to the PinchHandler.
    event->setAccepted(pan);
    return pan;
}
