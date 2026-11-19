pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// Canvas size mode: the new canvas as a box over the old one. Drag its edges
// and corners to grow or crop, drag inside to shift it; what falls outside is
// dimmed. `bounds` is in current canvas pixels (x / y < 0 grows left / up).
// CanvasSizePanel edits the same state with fields and an anchor.
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas

    property bool active: false
    property rect bounds
    // Where the old canvas stays when width / height are typed: 0, 0.5 or 1.
    property real anchorX: 0.5
    property real anchorY: 0.5

    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset

    function start() {
        editor.commitFloating()
        bounds = Qt.rect(0, 0, editor.canvasWidth, editor.canvasHeight)
        active = true
    }
    function cancel() { active = false }
    function apply() {
        active = false
        editor.resizeCanvas(bounds)
    }

    function clampSize(v) { return Math.max(1, Math.min(editor.maxImageSize, Math.round(v))) }
    // A typed size, placed by the anchor.
    function setSize(w, h) {
        w = clampSize(w)
        h = clampSize(h)
        bounds = Qt.rect(Math.round(anchorX * (editor.canvasWidth - w)), Math.round(anchorY * (editor.canvasHeight - h)), w, h)
    }
    function setAnchor(ax, ay) {
        anchorX = ax
        anchorY = ay
        setSize(bounds.width, bounds.height)
    }

    // Undo, open or new while active: the old bounds no longer apply.
    Connections {
        target: root.editor
        function onCanvasSizeChanged() { root.active = false }
        function onDocumentChanged() { root.active = false }
    }

    visible: active

    // State at the start of a drag: the box, and where the pointer was in
    // canvas pixels (so the view can zoom out meanwhile).
    property rect startBounds
    property point pressDoc
    property bool dragging: false

    function docPoint(mouseArea, mouse) {
        const p = mouseArea.mapToItem(root, mouse.x, mouse.y)
        return Qt.point((p.x - offset.x) / zoom, (p.y - offset.y) / zoom)
    }
    function begin(mouseArea, mouse) {
        startBounds = bounds
        pressDoc = docPoint(mouseArea, mouse)
        dragging = true
    }
    function end() { dragging = false }
    // Drag distance in whole canvas pixels.
    function delta(mouseArea, mouse) {
        const p = docPoint(mouseArea, mouse)
        return Qt.point(Math.round(p.x - pressDoc.x), Math.round(p.y - pressDoc.y))
    }

    // Dragging a side past the edge of the view zooms out until the new canvas
    // fits (a step at a time; it keeps going while the pointer waits there).
    Timer {
        interval: 280
        repeat: true
        running: root.active && root.dragging
        onTriggered: {
            const m = 28
            const out = root.boxX < m || root.boxY < m || root.boxX + root.boxW > root.width - m
                        || root.boxY + root.boxH > root.height - m
            if (out && root.canvas.zoom > 1)
                root.canvas.zoomOut()
        }
    }
    // Shift: the new canvas keeps the shape it started with. A corner scales
    // from the opposite corner; an edge also scales the other side, from its
    // middle.
    function proportional(s, l, t, r, b, hx, hy) {
        const ratio = s.width / s.height
        let w = r - l, h = b - t
        if (hx !== 0 && hy !== 0) {
            const k = Math.max(w / s.width, h / s.height)
            w = s.width * k
            h = s.height * k
        } else if (hx !== 0) {
            h = w / ratio
        } else {
            w = h * ratio
        }
        // Within the size limit, in the same shape.
        const limit = editor.maxImageSize
        const k = Math.min(1, limit / w, limit / h)
        w = Math.max(1, Math.round(w * k))
        h = Math.max(1, Math.round(h * k))
        const right = s.x + s.width, bottom = s.y + s.height
        const x = hx < 0 ? right - w : hx > 0 ? s.x : Math.round(s.x + (s.width - w) / 2)
        const y = hy < 0 ? bottom - h : hy > 0 ? s.y : Math.round(s.y + (s.height - h) / 2)
        return Qt.rect(x, y, w, h)
    }
    function resize(mouseArea, mouse, hx, hy) {
        const d = delta(mouseArea, mouse)
        const s = startBounds
        let l = s.x, t = s.y, r = s.x + s.width, b = s.y + s.height
        if (hx < 0) l = Math.max(r - editor.maxImageSize, Math.min(l + d.x, r - 1))
        if (hx > 0) r = Math.min(l + editor.maxImageSize, Math.max(r + d.x, l + 1))
        if (hy < 0) t = Math.max(b - editor.maxImageSize, Math.min(t + d.y, b - 1))
        if (hy > 0) b = Math.min(t + editor.maxImageSize, Math.max(b + d.y, t + 1))
        bounds = (mouse.modifiers & Qt.ShiftModifier) ? proportional(s, l, t, r, b, hx, hy)
                                                       : Qt.rect(l, t, r - l, b - t)
    }

    // The new canvas on screen.
    readonly property real boxX: offset.x + bounds.x * zoom
    readonly property real boxY: offset.y + bounds.y * zoom
    readonly property real boxW: bounds.width * zoom
    readonly property real boxH: bounds.height * zoom

    // Takes clicks so tools don't draw meanwhile; the wheel still zooms.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onWheel: wheel => wheel.accepted = false
    }

    // Dim everything outside the new canvas.
    component Shade: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }
    Shade { x: 0; y: 0; width: root.width; height: Math.max(0, root.boxY) }
    Shade { x: 0; y: root.boxY + root.boxH; width: root.width; height: Math.max(0, root.height - y) }
    Shade { x: 0; y: root.boxY; width: Math.max(0, root.boxX); height: root.boxH }
    Shade { x: root.boxX + root.boxW; y: root.boxY; width: Math.max(0, root.width - x); height: root.boxH }

    // The old canvas, for reference.
    Rectangle {
        x: root.offset.x - 1
        y: root.offset.y - 1
        width: root.editor.canvasWidth * root.zoom + 2
        height: root.editor.canvasHeight * root.zoom + 2
        color: "transparent"
        border.color: Theme.textFaint
        border.width: 1
    }

    Item {
        id: frame
        x: root.boxX
        y: root.boxY
        width: root.boxW
        height: root.boxH

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            color: "transparent"
            border.color: Theme.accent
            border.width: 1
        }

        // Shift the new canvas over the old one.
        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor
            onPressed: mouse => root.begin(this, mouse)
            onReleased: root.end()
            onCanceled: root.end()
            onPositionChanged: mouse => {
                const d = root.delta(this, mouse)
                root.bounds = Qt.rect(root.startBounds.x + d.x, root.startBounds.y + d.y,
                                      root.startBounds.width, root.startBounds.height)
            }
            onWheel: wheel => wheel.accepted = false
        }

        // Size while dragging.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 10
            text: root.bounds.width + " × " + root.bounds.height
            color: Theme.text
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }

        // Each whole side drags, not only its middle square: a strip along it
        // (the corners are left to their own handles), lit while pointed at.
        Repeater {
            model: [
                { hx: -1, hy: 0, cursor: Qt.SizeHorCursor }, { hx: 1, hy: 0, cursor: Qt.SizeHorCursor },
                { hx: 0, hy: -1, cursor: Qt.SizeVerCursor }, { hx: 0, hy: 1, cursor: Qt.SizeVerCursor },
            ]
            delegate: Item {
                id: side
                required property var modelData
                readonly property bool vertical: modelData.hx !== 0 // a left or right side
                readonly property real inset: Math.min(8, (vertical ? frame.height : frame.width) / 4)
                x: vertical ? (modelData.hx + 1) / 2 * frame.width - 5 : inset
                y: vertical ? inset : (modelData.hy + 1) / 2 * frame.height - 5
                width: vertical ? 10 : frame.width - 2 * inset
                height: vertical ? frame.height - 2 * inset : 10

                Rectangle {
                    visible: sideArea.containsMouse || sideArea.pressed
                    anchors.centerIn: parent
                    width: side.vertical ? 3 : parent.width
                    height: side.vertical ? parent.height : 3
                    radius: 1.5
                    color: Theme.accent
                }
                MouseArea {
                    id: sideArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: side.modelData.cursor
                    onPressed: mouse => root.begin(this, mouse)
                    onReleased: root.end()
                    onCanceled: root.end()
                    onPositionChanged: mouse => {
                        if (pressed)
                            root.resize(this, mouse, side.modelData.hx, side.modelData.hy)
                    }
                }
            }
        }

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
                    onReleased: root.end()
                    onCanceled: root.end()
                    onPositionChanged: mouse => {
                        if (pressed)
                            root.resize(this, mouse, handle.modelData.hx, handle.modelData.hy)
                    }
                }
            }
        }
    }
}
