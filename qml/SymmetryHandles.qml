pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// Handles just outside the canvas for moving the symmetry axes: one above
// the canvas moves the crossing point left / right, one at its left side
// moves it up / down, in half-pixel steps. Shown while a drawing tool is
// active and an axis that the handle moves is on (a diagonal needs both).
// Only the handles take the mouse; the rest passes through to the canvas.
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas
    property bool enabledHere: true // off while another canvas mode is active

    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset
    readonly property point center: editor.symmetryCenter
    readonly property bool drawing: editor.usesBrush(editor.tool) || editor.tool === Editor.Fill
    readonly property bool diagonal: editor.symmetryRising || editor.symmetryFalling
    readonly property bool radial: editor.symmetryRadial >= 2
    readonly property bool showX: enabledHere && drawing && (editor.symmetryHorizontal || diagonal || radial)
    readonly property bool showY: enabledHere && drawing && (editor.symmetryVertical || diagonal || radial)

    // A handle: a small tab pointing at the canvas, its axis's position while dragged.
    component Handle: Rectangle {
        id: handle
        property bool horizontal // moves along x
        property real startCenter
        property real pressAt

        width: horizontal ? 12 : 16
        height: horizontal ? 16 : 12
        radius: 4
        color: Theme.accent
        opacity: area.pressed || area.containsMouse ? 1 : 0.75
        border.color: Theme.handleBorder
        border.width: 1

        MouseArea {
            id: area
            anchors.fill: parent
            anchors.margins: -4 // a bit easier to grab
            hoverEnabled: true
            preventStealing: true
            cursorShape: handle.horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: mouse => {
                const p = mapToItem(root, mouse.x, mouse.y)
                handle.pressAt = handle.horizontal ? p.x : p.y
                handle.startCenter = handle.horizontal ? root.center.x : root.center.y
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                const moved = ((handle.horizontal ? p.x : p.y) - handle.pressAt) / root.zoom
                const v = Math.round((handle.startCenter + moved) * 2) / 2
                root.editor.symmetryCenter = handle.horizontal ? Qt.point(v, root.center.y) : Qt.point(root.center.x, v)
            }
            onWheel: wheel => wheel.accepted = false
        }

        Text {
            visible: area.pressed
            anchors.left: handle.horizontal ? parent.right : undefined
            anchors.top: handle.horizontal ? undefined : parent.bottom
            anchors.leftMargin: 6
            anchors.topMargin: 4
            anchors.verticalCenter: handle.horizontal ? parent.verticalCenter : undefined
            anchors.horizontalCenter: handle.horizontal ? undefined : parent.horizontalCenter
            text: handle.horizontal ? root.center.x : root.center.y
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }
    }

    // The spokes of a radial symmetry: a faint line from the crossing point at each copy's angle.
    Item {
        visible: root.enabledHere && root.drawing && root.radial
        x: root.offset.x
        y: root.offset.y
        width: root.editor.canvasWidth * root.zoom
        height: root.editor.canvasHeight * root.zoom
        clip: true
        Repeater {
            model: root.radial ? root.editor.symmetryRadial : 0
            delegate: Rectangle {
                required property int index
                x: root.center.x * root.zoom
                y: root.center.y * root.zoom - 0.5
                width: Math.hypot(root.editor.canvasWidth, root.editor.canvasHeight) * root.zoom
                height: 1
                color: Theme.accent
                opacity: 0.55
                transformOrigin: Item.Left
                rotation: 360 * index / root.editor.symmetryRadial
            }
        }
    }
    Handle {
        horizontal: true
        visible: root.showX
        x: root.offset.x + root.center.x * root.zoom - width / 2
        y: root.offset.y - height - 6
    }
    Handle {
        horizontal: false
        visible: root.showY
        x: root.offset.x - width - 6
        y: root.offset.y + root.center.y * root.zoom - height / 2
    }
}
