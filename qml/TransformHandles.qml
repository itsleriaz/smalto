pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// Handles over floating pixels (a paste or a transform): drag inside to move,
// the eight square handles to scale (Shift keeps the proportions), the round
// one above to rotate (Shift snaps to 15 degrees). All math works in canvas
// pixels from the state at the start of the drag, in the box's own unrotated
// frame, so the opposite edge stays put even when the box is rotated.
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas

    visible: editor.floating

    readonly property rect box: editor.transformBox
    readonly property real angle: editor.transformAngle
    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset
    // Box center on screen.
    readonly property real centerX: offset.x + (box.x + box.width / 2) * zoom
    readonly property real centerY: offset.y + (box.y + box.height / 2) * zoom

    // State at the start of a drag.
    property rect startBox
    property real startAngle
    property point startCenter // canvas pixels
    property point pressPoint  // screen
    property real pressAtan

    function begin(mouseArea, mouse) {
        startBox = box
        startAngle = angle
        startCenter = Qt.point(box.x + box.width / 2, box.y + box.height / 2)
        pressPoint = mouseArea.mapToItem(root, mouse.x, mouse.y)
        pressAtan = Math.atan2(pressPoint.y - centerY, pressPoint.x - centerX)
    }

    // Mouse position in canvas pixels relative to the start center, in the
    // box's unrotated frame.
    function local(mouseArea, mouse) {
        const p = mouseArea.mapToItem(root, mouse.x, mouse.y)
        const sx = offset.x + startCenter.x * zoom
        const sy = offset.y + startCenter.y * zoom
        const dx = (p.x - sx) / zoom, dy = (p.y - sy) / zoom
        const r = -startAngle * Math.PI / 180
        return Qt.point(dx * Math.cos(r) - dy * Math.sin(r), dx * Math.sin(r) + dy * Math.cos(r))
    }

    function resize(mouseArea, mouse, hx, hy) {
        const p = local(mouseArea, mouse)
        let l = -startBox.width / 2, r = startBox.width / 2, t = -startBox.height / 2, b = startBox.height / 2
        if (hx < 0) l = Math.min(p.x, r - 1)
        if (hx > 0) r = Math.max(p.x, l + 1)
        if (hy < 0) t = Math.min(p.y, b - 1)
        if (hy > 0) b = Math.max(p.y, t + 1)
        if ((mouse.modifiers & Qt.ShiftModifier) && editor.transformSourceSize.width > 0) {
            // Keep the source's proportions, following the dragged side.
            const ratio = editor.transformSourceSize.height / editor.transformSourceSize.width
            if (hx !== 0) { // height follows the width
                const h = (r - l) * ratio
                if (hy < 0) t = b - h
                else if (hy > 0) b = t + h
                else { const c = (t + b) / 2; t = c - h / 2; b = c + h / 2 }
            } else { // width follows the height
                const w = (b - t) / ratio
                const c = (l + r) / 2
                l = c - w / 2
                r = c + w / 2
            }
        }
        const w = Math.max(1, Math.round(r - l)), h = Math.max(1, Math.round(b - t))
        // The middle of the new box, back in canvas space.
        const mx = (l + r) / 2, my = (t + b) / 2
        const a = startAngle * Math.PI / 180
        const cx = startCenter.x + mx * Math.cos(a) - my * Math.sin(a)
        const cy = startCenter.y + mx * Math.sin(a) + my * Math.cos(a)
        editor.setTransformBox(Qt.rect(Math.round(cx - w / 2), Math.round(cy - h / 2), w, h))
    }

    // The rotated outline, with everything that turns with it.
    Item {
        id: frame
        x: root.offset.x + root.box.x * root.zoom
        y: root.offset.y + root.box.y * root.zoom
        width: root.box.width * root.zoom
        height: root.box.height * root.zoom
        rotation: root.angle

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            color: "transparent"
            border.color: Theme.accent
            border.width: 1
        }
        // The pivot (Editor.transformPivot): a small ring.
        Rectangle {
            readonly property int pivot: root.editor.transformPivot
            x: (pivot % 3) / 2 * parent.width - width / 2
            y: Math.floor(pivot / 3) / 2 * parent.height - height / 2
            width: 9
            height: 9
            radius: 4.5
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
        }

        // Move.
        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor
            onPressed: mouse => root.begin(this, mouse)
            onPositionChanged: mouse => {
                const p = mapToItem(root, mouse.x, mouse.y)
                const dx = Math.round((p.x - root.pressPoint.x) / root.zoom)
                const dy = Math.round((p.y - root.pressPoint.y) / root.zoom)
                root.editor.setTransformBox(Qt.rect(root.startBox.x + dx, root.startBox.y + dy,
                                                    root.startBox.width, root.startBox.height))
            }
            onWheel: wheel => wheel.accepted = false
        }

        // Rotate: a round handle above the top edge.
        Rectangle {
            x: parent.width / 2 - 0.5
            y: -22
            width: 1
            height: 16
            color: Theme.accent
        }
        Rectangle {
            id: rotateHandle
            width: 12
            height: 12
            radius: 6
            x: parent.width / 2 - width / 2
            y: -28
            color: rotateArea.containsMouse || rotateArea.pressed ? Theme.accent : Theme.text
            border.color: Theme.accent
            MouseArea {
                id: rotateArea
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.CrossCursor
                onPressed: mouse => root.begin(this, mouse)
                onPositionChanged: mouse => {
                    if (!pressed) // hover only highlights
                        return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const sx = root.offset.x + root.startCenter.x * root.zoom
                    const sy = root.offset.y + root.startCenter.y * root.zoom
                    let deg = root.startAngle + (Math.atan2(p.y - sy, p.x - sx) - root.pressAtan) * 180 / Math.PI
                    if (mouse.modifiers & Qt.ShiftModifier)
                        deg = Math.round(deg / 15) * 15
                    root.editor.setTransformAngle(deg)
                }
                AppToolTip {
                    text: "Rotate · Shift: 15° steps"
                    active: rotateArea.containsMouse && !rotateArea.pressed
                }
            }
        }

        // Scale: corners and edge middles.
        Repeater {
            model: [
                { hx: -1, hy: -1, cursor: Qt.SizeFDiagCursor }, { hx: 0, hy: -1, cursor: Qt.SizeVerCursor },
                { hx: 1, hy: -1, cursor: Qt.SizeBDiagCursor }, { hx: 1, hy: 0, cursor: Qt.SizeHorCursor },
                { hx: 1, hy: 1, cursor: Qt.SizeFDiagCursor }, { hx: 0, hy: 1, cursor: Qt.SizeVerCursor },
                { hx: -1, hy: 1, cursor: Qt.SizeBDiagCursor }, { hx: -1, hy: 0, cursor: Qt.SizeHorCursor },
            ]
            delegate: Rectangle {
                id: handle
                required property var modelData
                width: 9
                height: 9
                x: (modelData.hx + 1) / 2 * frame.width - width / 2
                y: (modelData.hy + 1) / 2 * frame.height - height / 2
                color: handleArea.containsMouse || handleArea.pressed ? Theme.accent : Theme.text
                border.color: Theme.accent
                MouseArea {
                    id: handleArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: handle.modelData.cursor
                    onPressed: mouse => root.begin(this, mouse)
                    onPositionChanged: mouse => {
                        if (pressed)
                            root.resize(this, mouse, handle.modelData.hx, handle.modelData.hy)
                    }
                }
            }
        }
    }
}
