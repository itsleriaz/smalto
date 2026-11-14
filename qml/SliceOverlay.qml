pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// The slices at the current frame (Editor.slices), drawn over the canvas in
// their own colors: the bounds as a 1 px outline with the name above it, a
// 9-slice center as dashed lines, the pivot as a small cross.
// With the slice tool: drag on empty canvas to draw a new slice, click one
// to pick it, drag inside it to move it, drag its handles to resize it; a
// click on empty canvas deselects. A drag shows its outline and changes the
// slice once, when let go (one undo step; the delegates are rebuilt when
// the slices change).
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas

    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset
    readonly property bool editing: editor.tool === Editor.SliceTool

    // A drag in progress: the slice (-1: a new one) and where it would go.
    property int dragIndex: -2
    property rect dragRect
    // A 9-slice center or pivot being dragged on the picked slice.
    property int centerDrag: -1 // the slice
    property rect dragCenter
    property int pivotDrag: -1
    property point dragPivot

    // Canvas pixel edge (not pixel) nearest a point of `item`.
    function edgeAt(item: Item, x: real, y: real): point {
        const p = item.mapToItem(root, x, y)
        return Qt.point(Math.round((p.x - offset.x) / zoom), Math.round((p.y - offset.y) / zoom))
    }

    visible: editing || (canvas.showSlices && editor.slices.length > 0)

    // Canvas pixel under a point of `item`.
    function pixelAt(item: Item, x: real, y: real): point {
        const p = item.mapToItem(root, x, y)
        return Qt.point(Math.floor((p.x - offset.x) / zoom), Math.floor((p.y - offset.y) / zoom))
    }
    function screenRect(r: rect): rect {
        return Qt.rect(Math.round(offset.x + r.x * zoom), Math.round(offset.y + r.y * zoom),
                       Math.round(r.width * zoom), Math.round(r.height * zoom))
    }

    // Drawing a new slice, on empty canvas (under the slices).
    MouseArea {
        anchors.fill: parent
        enabled: root.editing
        property point start
        onPressed: mouse => {
            start = root.pixelAt(this, mouse.x, mouse.y)
            root.dragIndex = -2
        }
        onPositionChanged: mouse => {
            const p = root.pixelAt(this, mouse.x, mouse.y)
            if (root.dragIndex === -2 && p.x === start.x && p.y === start.y)
                return
            root.dragIndex = -1
            root.dragRect = root.editor.snapRect(Qt.rect(Math.min(start.x, p.x), Math.min(start.y, p.y),
                                    Math.abs(p.x - start.x) + 1, Math.abs(p.y - start.y) + 1))
        }
        onReleased: {
            if (root.dragIndex === -1)
                root.editor.addSlice(root.dragRect)
            else
                root.editor.currentSlice = -1
            root.dragIndex = -2
        }
        onCanceled: root.dragIndex = -2
    }

    Repeater {
        model: root.editor.slices
        delegate: Item {
            id: slice
            required property var modelData
            readonly property int sliceIndex: modelData.index
            readonly property bool current: root.editing && sliceIndex === root.editor.currentSlice
            readonly property bool dragged: root.dragIndex === sliceIndex
            readonly property rect b: dragged ? root.dragRect : modelData.bounds
            readonly property rect s: root.screenRect(b)
            readonly property color tint: modelData.color

            x: s.x
            y: s.y
            width: s.width
            height: s.height

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(slice.tint.r, slice.tint.g, slice.tint.b, slice.current ? 0.16 : 0.08)
                border.color: slice.tint
                border.width: slice.current ? 2 : 1
            }

            readonly property rect center: root.centerDrag === sliceIndex ? root.dragCenter : modelData.center
            readonly property point pivot: root.pivotDrag === sliceIndex ? root.dragPivot : modelData.pivot

            // 9-slice center: the four lines that cut the slice. On the
            // picked slice they can be dragged (left, right, top, bottom).
            Repeater {
                model: slice.modelData.nineSlice ? 4 : 0
                delegate: Rectangle {
                    id: line
                    required property int index
                    z: 1 // over the move area
                    readonly property rect c: slice.center
                    readonly property bool vertical: index < 2
                    x: vertical ? Math.round((index === 0 ? c.x : c.x + c.width) * root.zoom) : 0
                    y: vertical ? 0 : Math.round((index === 2 ? c.y : c.y + c.height) * root.zoom)
                    width: vertical ? 1 : slice.width
                    height: vertical ? slice.height : 1
                    color: Qt.rgba(slice.tint.r, slice.tint.g, slice.tint.b, 0.6)
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        enabled: slice.current
                        cursorShape: line.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
                        onPositionChanged: mouse => {
                            const e = root.edgeAt(this, mouse.x, mouse.y)
                            const b = slice.modelData.bounds
                            const c = slice.modelData.center
                            let l = c.x, t = c.y, r = c.x + c.width, bt = c.y + c.height
                            if (line.index === 0) l = Math.max(0, Math.min(r - 1, e.x - b.x))
                            if (line.index === 1) r = Math.min(b.width, Math.max(l + 1, e.x - b.x))
                            if (line.index === 2) t = Math.max(0, Math.min(bt - 1, e.y - b.y))
                            if (line.index === 3) bt = Math.min(b.height, Math.max(t + 1, e.y - b.y))
                            root.centerDrag = slice.sliceIndex
                            root.dragCenter = Qt.rect(l, t, r - l, bt - t)
                        }
                        onReleased: {
                            if (root.centerDrag === slice.sliceIndex)
                                root.editor.setSliceCenter(slice.sliceIndex, root.dragCenter)
                            root.centerDrag = -1
                        }
                        onCanceled: root.centerDrag = -1
                    }
                }
            }

            // Pivot; on the picked slice it can be dragged.
            Item {
                z: 2
                visible: slice.modelData.hasPivot
                x: Math.round((slice.pivot.x + 0.5) * root.zoom)
                y: Math.round((slice.pivot.y + 0.5) * root.zoom)
                Rectangle { x: -5; y: 0; width: 11; height: 1; color: slice.tint }
                Rectangle { x: 0; y: -5; width: 1; height: 11; color: slice.tint }
                Rectangle {
                    visible: slice.current
                    x: -4
                    y: -4
                    width: 9
                    height: 9
                    radius: 4.5
                    color: "transparent"
                    border.color: slice.tint
                    border.width: 2
                }
                MouseArea {
                    x: -7
                    y: -7
                    width: 15
                    height: 15
                    enabled: slice.current
                    cursorShape: Qt.CrossCursor
                    onPositionChanged: mouse => {
                        const p = root.pixelAt(this, mouse.x, mouse.y)
                        const b = slice.modelData.bounds
                        root.pivotDrag = slice.sliceIndex
                        root.dragPivot = Qt.point(p.x - b.x, p.y - b.y)
                    }
                    onReleased: {
                        if (root.pivotDrag === slice.sliceIndex)
                            root.editor.setSlicePivot(slice.sliceIndex, root.dragPivot)
                        root.pivotDrag = -1
                    }
                    onCanceled: root.pivotDrag = -1
                }
            }

            // Name, just above the top-left corner.
            Rectangle {
                y: -height
                width: nameText.implicitWidth + 8
                height: nameText.implicitHeight + 2
                color: slice.tint
                visible: slice.modelData.name !== ""
                Text {
                    id: nameText
                    anchors.centerIn: parent
                    text: slice.modelData.name
                    // Readable on the slice's own color.
                    color: slice.tint.hslLightness > 0.6 ? "#141417" : "white"
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            // Slice tool: pick it, and drag to move it.
            MouseArea {
                anchors.fill: parent
                enabled: root.editing
                cursorShape: slice.current ? Qt.SizeAllCursor : Qt.PointingHandCursor
                property point start
                onPressed: mouse => {
                    root.editor.currentSlice = slice.sliceIndex
                    start = root.pixelAt(this, mouse.x, mouse.y)
                    root.dragRect = slice.modelData.bounds
                }
                onPositionChanged: mouse => {
                    const p = root.pixelAt(this, mouse.x, mouse.y)
                    const b = slice.modelData.bounds
                    root.dragIndex = slice.sliceIndex
                    const o = root.editor.snapMove(Qt.point(b.x, b.y), Qt.point(p.x - start.x, p.y - start.y))
                    root.dragRect = Qt.rect(b.x + o.x, b.y + o.y, b.width, b.height)
                }
                onReleased: {
                    if (root.dragIndex === slice.sliceIndex)
                        root.editor.setSliceBounds(slice.sliceIndex, root.dragRect)
                    root.dragIndex = -2
                }
                onCanceled: root.dragIndex = -2
            }

            // Resize handles on the picked slice: corners and edge middles.
            Repeater {
                model: slice.current ? 8 : 0
                delegate: Rectangle {
                    id: handle
                    required property int index
                    z: 3 // over the center lines and pivot
                    // -1 / 0 / 1 across and down: which edges this one moves.
                    readonly property int hx: [-1, 0, 1, 1, 1, 0, -1, -1][index]
                    readonly property int hy: [-1, -1, -1, 0, 1, 1, 1, 0][index]
                    x: (hx + 1) / 2 * slice.width - width / 2
                    y: (hy + 1) / 2 * slice.height - height / 2
                    width: 9
                    height: 9
                    color: "white"
                    border.color: slice.tint
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -3
                        cursorShape: handle.hx === 0 ? Qt.SizeVerCursor : handle.hy === 0 ? Qt.SizeHorCursor
                                     : handle.hx === handle.hy ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        property point start
                        onPressed: mouse => start = root.pixelAt(this, mouse.x, mouse.y)
                        onPositionChanged: mouse => {
                            const p = root.pixelAt(this, mouse.x, mouse.y)
                            const b = slice.modelData.bounds
                            const dx = p.x - start.x, dy = p.y - start.y
                            let l = b.x, t = b.y, r = b.x + b.width, bt = b.y + b.height
                            if (handle.hx < 0) l = Math.min(r - 1, root.editor.snapEdge(l + dx, false))
                            if (handle.hx > 0) r = Math.max(l + 1, root.editor.snapEdge(r + dx, false))
                            if (handle.hy < 0) t = Math.min(bt - 1, root.editor.snapEdge(t + dy, true))
                            if (handle.hy > 0) bt = Math.max(t + 1, root.editor.snapEdge(bt + dy, true))
                            root.dragIndex = slice.sliceIndex
                            root.dragRect = Qt.rect(l, t, r - l, bt - t)
                        }
                        onReleased: {
                            if (root.dragIndex === slice.sliceIndex)
                                root.editor.setSliceBounds(slice.sliceIndex, root.dragRect)
                            root.dragIndex = -2
                        }
                        onCanceled: root.dragIndex = -2
                    }
                }
            }
        }
    }

    // A new slice being drawn.
    Rectangle {
        visible: root.dragIndex === -1
        readonly property rect s: root.screenRect(root.dragRect)
        x: s.x
        y: s.y
        width: s.width
        height: s.height
        color: Qt.rgba(0, 0, 1, 0.08)
        border.color: Theme.accent
        border.width: 1
    }
}
