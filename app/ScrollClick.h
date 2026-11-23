#pragma once

#include <QObject>

class QQuickItem;

// A press on a list that is still scrolling (a flick, or a wheel / trackpad
// scroll easing out) stops the scroll first, so the press then reaches the
// row under it as usual. Without this, Qt Quick's Flickable takes that press
// to stop itself and the click is lost: picking a tag, a palette or a layer
// had to wait until the list came to rest. Installed on the application.
class ScrollClick : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Stops every scrolling Flickable under `scenePos` in `root`'s tree.
    // Returns how many were stopped (for the tests).
    static int stopScrollingAt(QQuickItem* root, QPointF scenePos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
