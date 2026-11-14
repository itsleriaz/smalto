import QtQuick
import QtQuick.Controls.Basic
import Smalto

// The base of the floating windows (effects, color mode, sprite size, ...):
// not modal, so the canvas stays in view and changes live; dragged by the
// strip around its title, which the content puts at its top; Esc closes it.
// The content (`contentItem`) decides what Enter does. The window places it
// (`x`, `y`) beside the tool strip, and it stays where it is dragged to.
Popup {
    id: root

    // How far below the top padding the drag strip reaches (the title's room).
    property real dragHeight: 30

    modal: false
    focus: true
    padding: 18
    closePolicy: Popup.CloseOnEscape

    background: Panel {
        solid: true
        padding: 0
        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.topPadding + root.dragHeight
            DragHandler {
                target: null
                property point start
                onActiveChanged: if (active) start = Qt.point(root.x, root.y)
                onTranslationChanged: if (active) {
                    root.x = start.x + translation.x
                    root.y = start.y + translation.y
                }
            }
            HoverHandler { cursorShape: Qt.SizeAllCursor }
        }
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
