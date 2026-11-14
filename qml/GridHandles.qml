pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// The document grid's handles on the canvas, while "Edit on the canvas" is on
// in the Grid popup: one at a grid corner moves the whole grid (its offset),
// the one a cell further right and down sizes the cells (Shift keeps them
// square); the sizes show while dragging. They change the same numbers as the
// popup's fields, so it all undoes the same way. Only the handles take the
// mouse; the rest passes through to the canvas.
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas

    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset
    readonly property rect grid: editor.grid
    readonly property bool shown: canvas.editGrid && canvas.showCustomGrid

    // A corner of the grid at or after the canvas's top-left (the grid repeats;
    // its offset is any of its corners).
    readonly property point corner: Qt.point(
        grid.x + Math.ceil(-grid.x / grid.width - 1e-9) * grid.width,
        grid.y + Math.ceil(-grid.y / grid.height - 1e-9) * grid.height)

    function toScreen(p: point): point {
        return Qt.point(offset.x + p.x * zoom, offset.y + p.y * zoom)
    }

    component Handle: Rectangle {
        id: handle
        required property point at // in canvas pixels
        property bool sizing: false
        property string readout
        property point startGrid
        property size startSize
        property point pressAt

        visible: root.shown
        width: 14
        height: 14
        radius: sizing ? 7 : 3
        x: root.toScreen(at).x - width / 2
        y: root.toScreen(at).y - height / 2
        color: Theme.accent
        border.color: Theme.handleBorder
        border.width: 2
        opacity: area.pressed || area.containsMouse ? 1 : 0.85
        z: sizing ? 2 : 1

        MouseArea {
            id: area
            anchors.fill: parent
            anchors.margins: -5
            hoverEnabled: true
            preventStealing: true
            cursorShape: handle.sizing ? Qt.SizeFDiagCursor : Qt.SizeAllCursor
            onPressed: mouse => {
                handle.pressAt = mapToItem(root, mouse.x, mouse.y)
                handle.startGrid = Qt.point(root.corner.x, root.corner.y)
                handle.startSize = Qt.size(root.grid.width, root.grid.height)
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                const dx = Math.round((p.x - handle.pressAt.x) / root.zoom)
                const dy = Math.round((p.y - handle.pressAt.y) / root.zoom)
                // From where the drag began (not from the grid as it is now,
                // which this very drag keeps changing).
                const size = handle.startSize
                if (handle.sizing) {
                    let w = Math.max(1, size.width + dx), h = Math.max(1, size.height + dy)
                    if (mouse.modifiers & Qt.ShiftModifier)
                        w = h = Math.max(w, h)
                    // The corner at the top-left stays where it is.
                    root.editor.setGrid(handle.startGrid.x, handle.startGrid.y, w, h)
                } else {
                    root.editor.setGrid(handle.startGrid.x + dx, handle.startGrid.y + dy, size.width, size.height)
                }
            }
            onWheel: wheel => wheel.accepted = false
        }
        Text {
            visible: area.pressed
            anchors.left: parent.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: handle.sizing ? root.grid.width + " \u00d7 " + root.grid.height : root.grid.x + ", " + root.grid.y
            color: Theme.text
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }
    }

    Handle { at: root.corner }
    Handle {
        sizing: true
        at: Qt.point(root.corner.x + root.grid.width, root.corner.y + root.grid.height)
    }
}
