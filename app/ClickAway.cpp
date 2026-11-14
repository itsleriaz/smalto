#include "ClickAway.h"

#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

bool ClickAway::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::MouseButtonPress)
        return false;
    auto* window = qobject_cast<QQuickWindow*>(watched);
    if (!window)
        return false;
    QQuickItem* focus = window->activeFocusItem();
    // A text field: the same test as Main.qml's `typing`.
    if (!focus || !focus->property("cursorPosition").isValid())
        return false;
    const QPointF pos = static_cast<QMouseEvent*>(event)->scenePosition();
    if (!focus->contains(focus->mapFromScene(pos)))
        focus->setFocus(false); // active focus goes back to its scope (window or popup)
    return false; // the press goes on as usual
}
