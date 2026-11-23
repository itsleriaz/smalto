#include "ScrollClick.h"

#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

int ScrollClick::stopScrollingAt(QQuickItem* root, QPointF scenePos)
{
    if (!root || !root->isVisible())
        return 0;
    int stopped = 0;
    // QQuickFlickable (ListView, GridView, Flickable...) is private API: go by
    // its meta-object instead of its header.
    if (root->inherits("QQuickFlickable") && root->property("moving").toBool()
        && root->contains(root->mapFromScene(scenePos))) {
        QMetaObject::invokeMethod(root, "cancelFlick");
        ++stopped;
    }
    for (QQuickItem* child : root->childItems())
        stopped += stopScrollingAt(child, scenePos);
    return stopped;
}

bool ScrollClick::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::MouseButtonPress)
        return false;
    if (auto* window = qobject_cast<QQuickWindow*>(watched))
        stopScrollingAt(window->contentItem(), static_cast<QMouseEvent*>(event)->scenePosition());
    return false; // the press goes on as usual, to a list at rest
}
