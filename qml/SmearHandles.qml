pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import Smalto

// Posing the current Smear group on the canvas, on the current frame (Editor.smearState): drag inside
// the box to move the art, the round handle above it to turn it about the pivot (Shift: 15 degree
// steps), the square handles to scale it about the pivot (Shift keeps the proportions; Alt on an edge
// handle skews along that edge), the ring to move the pivot (the posed art stays where it is). Each drag
// sets the frame's key (made if there is none) and is one undo step. All the math works from the state
// at the start of the drag.
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas
    property bool enabledHere: true // off while another canvas mode is active

    readonly property var smear: editor.smearState
    visible: enabledHere && (smear.current ?? false) && editor.tool === Editor.PoseTool

    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset
    readonly property var corners: smear.corners ?? []
    readonly property rect box: smear.box ?? Qt.rect(0, 0, 0, 0)
    readonly property point pivot: smear.pivot ?? Qt.point(0, 0)
    readonly property point pivotPosed: smear.pivotPosed ?? Qt.point(0, 0)

    function screen(p: point): point { return Qt.point(offset.x + p.x * zoom, offset.y + p.y * zoom) }
    readonly property var screenCorners: corners.map(c => root.screen(c))
    // The posed point of the box as drawn at (u, v), each 0..1 across it (the box is posed by an affine
    // map, so it is the blend of the corners).
    function boxPoint(u: real, v: real): point {
        if (screenCorners.length < 4)
            return Qt.point(0, 0)
        const c = screenCorners
        const top = Qt.point(c[0].x + (c[1].x - c[0].x) * u, c[0].y + (c[1].y - c[0].y) * u)
        const bottom = Qt.point(c[3].x + (c[2].x - c[3].x) * u, c[3].y + (c[2].y - c[3].y) * u)
        return Qt.point(top.x + (bottom.x - top.x) * v, top.y + (bottom.y - top.y) * v)
    }
    function inside(x: real, y: real): bool {
        const c = screenCorners
        if (c.length < 4)
            return false
        let sign = 0
        for (let i = 0; i < 4; ++i) {
            const a = c[i], b = c[(i + 1) % 4]
            const cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x)
            if (Math.abs(cross) < 1e-9)
                continue
            const s = cross > 0 ? 1 : -1
            if (sign === 0)
                sign = s
            else if (s !== sign)
                return false
        }
        return true
    }

    // The state at the start of a drag.
    property var startPose: ({})
    property point pressAt // screen
    property point startPivot
    property var startMatrix: [1, 0, 0, 1]

    function begin(area: Item, mouse: var) {
        const p = smear.pose ?? {}
        const o = {}
        o.x = p.x ?? 0
        o.y = p.y ?? 0
        o.rotation = p.rotation ?? 0
        o.scaleX = p.scaleX ?? 1
        o.scaleY = p.scaleY ?? 1
        startPose = o
        pressAt = area.mapToItem(root, mouse.x, mouse.y)
        startPivot = pivot
        startMatrix = smear.matrix ?? [1, 0, 0, 1]
    }
    // Alt-drag on an edge handle: the top and bottom slant it sideways (skew x), the sides up and down
    // (skew y), so the handle follows the pointer. In the pose's frame without its turn: K S u = l.
    function skew(p: point, hx: int, hy: int) {
        const turn = (startPose.rotation ?? 0) * Math.PI / 180
        const c = Math.cos(turn), s = Math.sin(turn)
        const pivotOnScreen = screen(pivotPosed)
        const vx = (p.x - pivotOnScreen.x) / zoom, vy = (p.y - pivotOnScreen.y) / zoom
        const lx = c * vx + s * vy, ly = -s * vx + c * vy
        const ux = box.x + (hx + 1) / 2 * box.width - startPivot.x
        const uy = box.y + (hy + 1) / 2 * box.height - startPivot.y
        const sx = startPose.scaleX, sy = startPose.scaleY
        const f = {}
        if (hx === 0 && Math.abs(uy * sy) > 0.5)
            f.skewX = Math.max(-85, Math.min(85, Math.round(Math.atan((lx - sx * ux) / (sy * uy)) * 180 / Math.PI)))
        else if (hy === 0 && Math.abs(ux * sx) > 0.5)
            f.skewY = Math.max(-85, Math.min(85, Math.round(Math.atan((ly - sy * uy) / (sx * ux)) * 180 / Math.PI)))
        else
            return
        pose(f)
    }

    function pose(fields: var) {
        const o = {}
        for (const k in fields)
            o[k] = fields[k]
        editor.setSmearPose(o, true)
    }

    // Moving: inside the box. Not over anything else (the handles are above it); elsewhere the press goes
    // through to the canvas.
    MouseArea {
        id: moveArea
        anchors.fill: parent
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: mouse => {
            if (!root.inside(mouse.x, mouse.y)) {
                mouse.accepted = false
                return
            }
            root.begin(this, mouse)
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            const f = {}
            f.x = root.startPose.x + Math.round((mouse.x - root.pressAt.x) / root.zoom)
            f.y = root.startPose.y + Math.round((mouse.y - root.pressAt.y) / root.zoom)
            root.pose(f)
        }
        onWheel: wheel => wheel.accepted = false
    }

    // The way the pivot goes (while there are two keys or more): a dotted line, a dot a frame.
    readonly property var path: (smear.path ?? []).map(p => root.screen(p))
    readonly property var pathDots: (smear.pathDots ?? []).map(p => root.screen(p))
    Shape {
        preferredRendererType: Shape.CurveRenderer
        visible: root.path.length > 1
        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            fillColor: "transparent"
            PathPolyline { path: root.path }
        }
    }
    Repeater {
        model: root.pathDots.length
        delegate: Rectangle {
            required property int index
            readonly property point at: root.pathDots[index] ?? Qt.point(0, 0)
            x: at.x - 2.5
            y: at.y - 2.5
            width: 5
            height: 5
            radius: 2.5
            color: Theme.accent
            opacity: 0.8
        }
    }

    // The box as posed.
    Shape {
        preferredRendererType: Shape.CurveRenderer
        visible: root.screenCorners.length === 4
        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 1
            fillColor: "transparent"
            PathPolyline {
                path: root.screenCorners.length === 4 ? root.screenCorners.concat([root.screenCorners[0]]) : []
            }
        }
    }

    // Turning: a round handle out from the middle of the top edge.
    readonly property point topMid: boxPoint(0.5, 0)
    readonly property point middle: boxPoint(0.5, 0.5)
    readonly property point knob: {
        const dx = topMid.x - middle.x, dy = topMid.y - middle.y
        const length = Math.max(1, Math.hypot(dx, dy))
        return Qt.point(topMid.x + dx / length * 24, topMid.y + dy / length * 24)
    }
    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 1
            startX: root.topMid.x
            startY: root.topMid.y
            PathLine { x: root.knob.x; y: root.knob.y }
        }
    }
    Rectangle {
        width: 12
        height: 12
        radius: 6
        x: root.knob.x - width / 2
        y: root.knob.y - height / 2
        color: turnArea.containsMouse || turnArea.pressed ? Theme.accent : Theme.text
        border.color: Theme.accent
        MouseArea {
            id: turnArea
            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.CrossCursor
            property real pressAngle
            onPressed: mouse => {
                root.begin(this, mouse)
                const c = root.screen(root.pivotPosed)
                pressAngle = Math.atan2(root.pressAt.y - c.y, root.pressAt.x - c.x)
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                const c = root.screen(root.pivotPosed)
                let degrees = root.startPose.rotation + (Math.atan2(p.y - c.y, p.x - c.x) - pressAngle) * 180 / Math.PI
                degrees = (mouse.modifiers & Qt.ShiftModifier) ? Math.round(degrees / 15) * 15 : Math.round(degrees)
                const f = {}
                f.rotation = degrees
                root.pose(f)
            }
            onWheel: wheel => wheel.accepted = false
            AppToolTip {
                text: "Turn about the pivot · Shift: 15° steps"
                active: turnArea.containsMouse && !turnArea.pressed
            }
        }
    }

    // Scaling about the pivot: corners and edge middles. The pointer, from the posed pivot, taken back
    // through the pose's turn and skew, over where the handle is from the pivot as drawn, is the scale.
    Repeater {
        model: [
            { hx: -1, hy: -1 }, { hx: 0, hy: -1 }, { hx: 1, hy: -1 }, { hx: 1, hy: 0 },
            { hx: 1, hy: 1 }, { hx: 0, hy: 1 }, { hx: -1, hy: 1 }, { hx: -1, hy: 0 },
        ]
        delegate: Rectangle {
            id: handle
            required property var modelData
            readonly property point at: root.boxPoint((modelData.hx + 1) / 2, (modelData.hy + 1) / 2)
            width: 9
            height: 9
            x: at.x - width / 2
            y: at.y - height / 2
            color: scaleArea.containsMouse || scaleArea.pressed ? Theme.accent : Theme.text
            border.color: Theme.accent
            MouseArea {
                id: scaleArea
                anchors.fill: parent
                anchors.margins: -5
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.SizeAllCursor
                onPressed: mouse => root.begin(this, mouse)
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const hx = handle.modelData.hx, hy = handle.modelData.hy
                    if ((mouse.modifiers & Qt.AltModifier) && (hx === 0 || hy === 0)) {
                        root.skew(p, hx, hy)
                        return
                    }
                    const m = root.startMatrix, sx0 = root.startPose.scaleX, sy0 = root.startPose.scaleY
                    // The turn and skew alone: the matrix without its scale.
                    const a = m[0] / sx0, b = m[1] / sy0, c = m[2] / sx0, d = m[3] / sy0
                    const det = a * d - b * c
                    if (Math.abs(det) < 1e-9)
                        return
                    const pivotOnScreen = root.screen(root.pivotPosed)
                    const vx = (p.x - pivotOnScreen.x) / root.zoom, vy = (p.y - pivotOnScreen.y) / root.zoom
                    const lx = (d * vx - b * vy) / det, ly = (-c * vx + a * vy) / det
                    // Where the handle is from the pivot, as drawn.
                    const ux = root.box.x + (hx + 1) / 2 * root.box.width - root.startPivot.x
                    const uy = root.box.y + (hy + 1) / 2 * root.box.height - root.startPivot.y
                    let sx = hx !== 0 && Math.abs(ux) > 0.5 ? lx / ux : sx0
                    let sy = hy !== 0 && Math.abs(uy) > 0.5 ? ly / uy : sy0
                    if (mouse.modifiers & Qt.ShiftModifier) { // the same factor both ways, from the dragged side
                        const factor = hx !== 0 && Math.abs(ux) > 0.5 ? sx / sx0 : sy / sy0
                        sx = sx0 * factor
                        sy = sy0 * factor
                    }
                    const f = {}
                    f.scaleX = Math.round(sx * 100) / 100
                    f.scaleY = Math.round(sy * 100) / 100
                    root.pose(f)
                }
                onWheel: wheel => wheel.accepted = false
            }
        }
    }

    // The pivot: a ring with a cross, dragged in half pixels.
    Item {
        id: pivotHandle
        readonly property point at: root.screen(root.pivotPosed)
        x: at.x - 9
        y: at.y - 9
        width: 18
        height: 18
        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: "transparent"
            border.color: Theme.handleBorder
            border.width: 4
        }
        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
        }
        Rectangle { anchors.centerIn: parent; width: 2; height: 18; color: Theme.accent }
        Rectangle { anchors.centerIn: parent; width: 18; height: 2; color: Theme.accent }
        MouseArea {
            id: pivotArea
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeAllCursor
            onPressed: mouse => root.begin(this, mouse)
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                // The pivot is where the art turns as drawn; the art stays put (the editor adjusts the
                // keys), so on screen it moves by the pose's matrix: the pointer's move taken back
                // through it keeps the ring under the pointer.
                const m = root.startMatrix, det = m[0] * m[3] - m[1] * m[2]
                const sx = (p.x - root.pressAt.x) / root.zoom, sy = (p.y - root.pressAt.y) / root.zoom
                const dx = Math.abs(det) > 1e-9 ? (m[3] * sx - m[1] * sy) / det : sx
                const dy = Math.abs(det) > 1e-9 ? (-m[2] * sx + m[0] * sy) / det : sy
                root.editor.setSmearPivot(Math.round((root.startPivot.x + dx) * 2) / 2,
                                          Math.round((root.startPivot.y + dy) * 2) / 2, true)
            }
            onWheel: wheel => wheel.accepted = false
            AppToolTip {
                text: "The pivot: what it turns and scales about"
                active: pivotArea.containsMouse && !pivotArea.pressed
            }
        }
    }
}
