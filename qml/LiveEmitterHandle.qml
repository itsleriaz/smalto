pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import Smalto

// The emitter of the current live layer: a crosshair on the canvas that drags directly (no other tool
// works on a live layer, so it can't steal a stroke), in whole pixels, to the middle of a pixel. The
// move tool drags it from anywhere, in the editor. A cone shows where the particles go: drag the
// handle on its middle arm for the direction (Shift: steps of 15 degrees), the ones on its edges for
// the spread. Only the crosshair and these handles take the mouse.
Item {
    id: root
    visible: mode !== 2 && editor.particleContext === 0 // none while the sub-particles are edited
    opacity: mode === 1 && hovered === 0 ? 0.28 : 1
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

    required property Editor editor
    required property CanvasView canvas
    property bool enabledHere: true // off while another canvas mode is active
    // How much of the particle controls shows: 0 all, 1 faded (full while the pointer is on one), 2 none.
    property int mode: 0
    property int hovered: 0 // how many of the controls the pointer is on
    // The field being edited (-1: none), kept by the window with the panel.
    property int field: -1
    signal fieldPicked(int index)

    Connections {
        target: root.editor
        function onCurrentLayerChanged() { root.hovered = 0 } // a control that went away can't say it left
    }

    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset
    readonly property point emitter: editor.liveEmitter

    // One arm of the crosshair: dark outline under the accent line.
    component Arm: Rectangle {
        property alias line: inner
        color: Theme.handleBorder
        Rectangle {
            id: inner
            anchors.fill: parent
            anchors.margins: 1
            color: Theme.accent
        }
    }

    readonly property real direction: editor.particleSettings.direction ?? -90
    readonly property real spread: editor.particleSettings.spread ?? 0
    readonly property real reach: 72 // the cone's length on screen, pixels
    // The source around the emitter (0 point, 1 line, 2 rectangle, 3 ellipse), in canvas pixels.
    readonly property int source: editor.particleSettings.source ?? 0
    readonly property real sourceW: editor.particleSettings.sourceWidth ?? 0
    readonly property real sourceH: editor.particleSettings.sourceHeight ?? 0
    readonly property real sourceAngle: editor.particleSettings.sourceAngle ?? 0
    // The outline of the source, turned about the emitter: a line, or the corners of a rectangle, or a
    // ring of points for an ellipse; in screen pixels from the emitter.
    readonly property var sourceOutline: {
        const hw = sourceW * zoom / 2, hh = sourceH * zoom / 2
        if (source === 1)
            return [Qt.point(-hw, 0), Qt.point(hw, 0)]
        if (source === 2)
            return [Qt.point(-hw, -hh), Qt.point(hw, -hh), Qt.point(hw, hh), Qt.point(-hw, hh), Qt.point(-hw, -hh)]
        if (source === 3) {
            const points = []
            for (let i = 0; i <= 64; ++i)
                points.push(Qt.point(Math.cos(i / 64 * 2 * Math.PI) * hw, Math.sin(i / 64 * 2 * Math.PI) * hh))
            return points
        }
        return []
    }

    // Where a handle at `degrees` sits, from the emitter.
    function rayX(degrees: real): real { return Math.cos(degrees * Math.PI / 180) * reach }
    function rayY(degrees: real): real { return Math.sin(degrees * Math.PI / 180) * reach }
    // The angle of a point (relative to the emitter), -180..180.
    function angleOf(x: real, y: real): real { return Math.atan2(y, x) * 180 / Math.PI }
    function wrapped(degrees: real): real {
        let d = degrees % 360
        return d > 180 ? d - 360 : d <= -180 ? d + 360 : d
    }

    // The cone, with the emitter at its origin.
    Item {
        id: cone
        visible: root.enabledHere && root.editor.liveLayer
        x: Math.round(root.offset.x + root.emitter.x * root.zoom)
        y: Math.round(root.offset.y + root.emitter.y * root.zoom)

        Shape {
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.handleBorder
                strokeWidth: 1
                fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                startX: 0
                startY: 0
                PathLine { x: root.rayX(root.direction - root.spread / 2); y: root.rayY(root.direction - root.spread / 2) }
                PathAngleArc {
                    centerX: 0
                    centerY: 0
                    radiusX: root.reach
                    radiusY: root.reach
                    startAngle: root.direction - root.spread / 2
                    sweepAngle: root.spread
                }
                PathLine { x: 0; y: 0 }
            }
            ShapePath {
                strokeColor: Theme.accent
                strokeWidth: 1.5
                fillColor: "transparent"
                startX: 0
                startY: 0
                PathLine { x: root.rayX(root.direction); y: root.rayY(root.direction) }
            }
        }

        // The source, turned about the emitter: its outline and the handles that size it.
        Item {
            id: sourceItem
            visible: root.source !== 0
            rotation: root.sourceAngle
            Shape {
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.handleBorder
                    strokeWidth: 4
                    fillColor: "transparent"
                    PathPolyline { path: root.sourceOutline }
                }
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 2
                    fillColor: "transparent"
                    PathPolyline { path: root.sourceOutline }
                }
            }
            // A square handle on the source: where it is dragged to size it.
            component SourceKnob: Rectangle {
                id: sk
                signal dragged(real x, real y)
                width: 11
                height: 11
                radius: 2
                color: Theme.panel
                border.color: Theme.accent
                border.width: 2
                scale: skArea.pressed || skArea.containsMouse ? 1.25 : 1
                Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
                MouseArea {
                    onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
                    id: skArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.SizeAllCursor
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const p = mapToItem(cone, mouse.x, mouse.y)
                        sk.dragged(p.x, p.y)
                    }
                    onWheel: wheel => wheel.accepted = false
                }
            }
            // Dragging the end of a line sets its length and turns it; the ends of a box or an oval
            // only change their width and height along the turned axes.
            SourceKnob {
                x: root.sourceW * root.zoom / 2 - width / 2
                y: -height / 2
                onDragged: (px, py) => {
                    if (root.source === 1) {
                        root.editor.setParticleField("sourceAngle", Math.round(root.angleOf(px, py)), true)
                        root.editor.setParticleField("sourceWidth", Math.max(1, Math.round(2 * Math.hypot(px, py) / root.zoom)), true)
                    } else {
                        const a = root.sourceAngle * Math.PI / 180
                        root.editor.setParticleField("sourceWidth", Math.max(1, Math.round(2 * Math.abs(px * Math.cos(a) + py * Math.sin(a)) / root.zoom)), true)
                    }
                }
            }
            SourceKnob {
                visible: root.source >= 2
                x: -width / 2
                y: root.sourceH * root.zoom / 2 - height / 2
                onDragged: (px, py) => {
                    const a = root.sourceAngle * Math.PI / 180
                    root.editor.setParticleField("sourceHeight", Math.max(1, Math.round(2 * Math.abs(-px * Math.sin(a) + py * Math.cos(a)) / root.zoom)), true)
                }
            }
        }

        // A round handle on the end of a ray.
        component Knob: Rectangle {
            id: knob
            property real degrees
            property bool main: false
            signal dragged(real x, real y, bool shift)
            x: root.rayX(degrees) - width / 2
            y: root.rayY(degrees) - height / 2
            width: main ? 14 : 11
            height: width
            radius: width / 2
            color: main ? Theme.accent : Theme.panel
            border.color: main ? Theme.handleBorder : Theme.accent
            border.width: 2
            scale: knobArea.pressed || knobArea.containsMouse ? 1.25 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
            MouseArea {
                onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
                id: knobArea
                anchors.fill: parent
                anchors.margins: -5
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const p = mapToItem(cone, mouse.x, mouse.y)
                    knob.dragged(p.x, p.y, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                }
                onWheel: wheel => wheel.accepted = false
            }
        }
        Knob {
            degrees: root.direction - root.spread / 2
            onDragged: (x, y) => root.editor.setParticleField("spread", Math.round(2 * Math.abs(root.wrapped(root.angleOf(x, y) - root.direction))), true)
        }
        Knob {
            degrees: root.direction + root.spread / 2
            onDragged: (x, y) => root.editor.setParticleField("spread", Math.round(2 * Math.abs(root.wrapped(root.angleOf(x, y) - root.direction))), true)
        }
        // Last, so it wins where the handles overlap (a narrow cone).
        Knob {
            main: true
            degrees: root.direction
            onDragged: (x, y, shift) => {
                const a = root.angleOf(x, y)
                root.editor.setParticleField("direction", shift ? Math.round(a / 15) * 15 : Math.round(a), true)
            }
        }
    }

    // A press on a collider's line picks it; on the picked one's line it adds a corner there and drags
    // it (one undo step). Elsewhere the press goes through. Not in a delegate: adding a corner makes the
    // corner handles again, and this drag must outlive that.
    MouseArea {
        id: lineArea
        anchors.fill: parent
        visible: root.enabledHere && root.editor.liveLayer
        preventStealing: true
        property int dragField: -1
        property int dragPoint: -1
        // The collider and segment under a screen point (within 6 px), as [field, segment], or null.
        function lineAt(x: real, y: real): var {
            const fields = root.editor.particleSettings.fields ?? []
            let best = null, bestDistance = 6
            for (let f = 0; f < fields.length; ++f) {
                if (fields[f].type !== 4)
                    continue
                const pts = fields[f].points ?? []
                for (let i = 0; i + 1 < pts.length; ++i) {
                    const ax = root.offset.x + pts[i].x * root.zoom, ay = root.offset.y + pts[i].y * root.zoom
                    const bx = root.offset.x + pts[i + 1].x * root.zoom, by = root.offset.y + pts[i + 1].y * root.zoom
                    const lx = bx - ax, ly = by - ay, length2 = lx * lx + ly * ly
                    const t = length2 > 0 ? Math.max(0, Math.min(1, ((x - ax) * lx + (y - ay) * ly) / length2)) : 0
                    const d = Math.hypot(x - (ax + lx * t), y - (ay + ly * t))
                    if (d < bestDistance) {
                        bestDistance = d
                        best = [f, i]
                    }
                }
            }
            return best
        }
        // The middle of the canvas pixel under a screen point (corners sit on pixels, like the emitter).
        function canvasAt(x: real, y: real): point {
            return Qt.point(Math.floor((x - root.offset.x) / root.zoom) + 0.5, Math.floor((y - root.offset.y) / root.zoom) + 0.5)
        }
        onPressed: mouse => {
            const hit = lineAt(mouse.x, mouse.y)
            if (!hit) {
                mouse.accepted = false
                return
            }
            if (root.field !== hit[0]) {
                root.fieldPicked(hit[0])
                return
            }
            const p = canvasAt(mouse.x, mouse.y)
            dragField = hit[0]
            dragPoint = root.editor.insertColliderPoint(hit[0], hit[1] + 1, p.x, p.y)
        }
        onPositionChanged: mouse => {
            if (!pressed || dragPoint < 0)
                return
            const p = canvasAt(mouse.x, mouse.y)
            root.editor.setColliderPoint(dragField, dragPoint, p.x, p.y, true)
        }
        onReleased: { dragField = -1; dragPoint = -1 }
        onCanceled: { dragField = -1; dragPoint = -1 }
        onWheel: wheel => wheel.accepted = false
    }

    // The fields: a handle where each acts, a ring for how far (when it has a limit); the one being
    // edited has a handle on its ring to resize it.
    Repeater {
        model: root.editor.liveLayer ? (root.editor.particleSettings.fields ?? []).length : 0
        delegate: Item {
            id: fieldItem
            required property int index
            readonly property var info: (root.editor.particleSettings.fields ?? [])[index]
            readonly property bool picked: root.field === index
            readonly property bool repels: (info?.strength ?? 0) < 0
            readonly property int kind: info?.type ?? 0 // 0 attractor / repeller, 1 vortex, 2 wind, 3 floor, 4 collider
            readonly property real reach: kind >= 2 ? 0 : (info?.radius ?? 0) * root.zoom
            readonly property real heading: info?.angle ?? 0
            // A collider's corners on the screen, from its place.
            readonly property var points: kind === 4 ? (info?.points ?? []).map(p => Qt.point((p.x - (info?.x ?? 0)) * root.zoom,
                                                                                            (p.y - (info?.y ?? 0)) * root.zoom)) : []
            // Its handle sits just above the middle of its corners, off the line.
            readonly property real handleX: points.length ? points.reduce((a, p) => a + p.x, 0) / points.length : 0
            readonly property real handleY: points.length ? Math.min(...points.map(p => p.y)) - 18 : 0
            visible: root.enabledHere && info !== undefined
            x: Math.round(root.offset.x + (info?.x ?? 0) * root.zoom)
            y: Math.round(root.offset.y + (info?.y ?? 0) * root.zoom)

            Rectangle { // a floor: the line across the canvas
                visible: fieldItem.kind === 3
                x: -fieldItem.x
                y: -1
                width: root.width
                height: 2
                color: Theme.accent
                opacity: fieldItem.picked ? 0.9 : 0.55
            }
            Shape { // a collider: its line
                visible: fieldItem.kind === 4
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.handleBorder
                    strokeWidth: 4
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    PathPolyline { path: fieldItem.points }
                }
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 2
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    PathPolyline { path: fieldItem.points }
                }
            }
            Rectangle { // from the handle to the line, so it reads as the line's
                visible: fieldItem.kind === 4 && fieldItem.points.length > 0
                x: fieldItem.handleX - 0.5
                y: fieldItem.handleY + 9
                width: 1
                height: 8
                color: Theme.accent
                opacity: 0.6
            }
            Rectangle { // how far it reaches
                visible: fieldItem.reach > 0
                x: -fieldItem.reach
                y: -fieldItem.reach
                width: fieldItem.reach * 2
                height: fieldItem.reach * 2
                radius: fieldItem.reach
                color: "transparent"
                border.color: Theme.accent
                border.width: fieldItem.picked ? 1.5 : 1
                opacity: fieldItem.picked ? 0.8 : 0.35
            }
            Rectangle { // the field itself: solid pulls, hollow pushes
                x: fieldItem.handleX - 9
                y: fieldItem.handleY - 9
                width: 18
                height: 18
                radius: 9
                color: fieldItem.repels && fieldItem.kind !== 2 ? Theme.panel : Theme.accent
                border.color: fieldItem.repels && fieldItem.kind !== 2 ? Theme.accent : Theme.handleBorder
                border.width: 2
                scale: fieldArea.pressed || fieldArea.containsMouse || fieldItem.picked ? 1.2 : 1
                Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
                Text {
                    anchors.centerIn: parent
                    text: fieldItem.kind === 4 ? "/"
                          : fieldItem.kind === 3 ? "\u2014"
                          : fieldItem.kind === 2 ? "~"
                          : fieldItem.kind === 1 ? (fieldItem.repels ? "\u21ba" : "\u21bb")
                          : fieldItem.repels ? "\u2212" : "+"
                    color: fieldItem.repels && fieldItem.kind !== 2 ? Theme.accent : Theme.textOnAccent
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
                MouseArea {
                    onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
                    id: fieldArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.SizeAllCursor
                    property point pressAt
                    property point start
                    onPressed: mouse => {
                        root.fieldPicked(fieldItem.index)
                        pressAt = mapToItem(root, mouse.x, mouse.y)
                        start = Qt.point(fieldItem.info?.x ?? 0, fieldItem.info?.y ?? 0)
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const p = mapToItem(root, mouse.x, mouse.y)
                        root.editor.setParticleFieldPosition(fieldItem.index,
                            Math.floor(start.x + (p.x - pressAt.x) / root.zoom) + 0.5,
                            Math.floor(start.y + (p.y - pressAt.y) / root.zoom) + 0.5, true)
                    }
                    onWheel: wheel => wheel.accepted = false
                }
            }
            Item { // a wind's arrow: drag its tip to turn it
                visible: fieldItem.kind === 2
                rotation: fieldItem.heading
                Rectangle {
                    x: 9
                    y: -1
                    width: 36
                    height: 2
                    color: Theme.accent
                }
                Rectangle {
                    x: 45
                    y: -5
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.panel
                    border.color: Theme.accent
                    border.width: 2
                    MouseArea {
                        onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
                        hoverEnabled: true
                        anchors.fill: parent
                        anchors.margins: -5
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const p = mapToItem(fieldItem, mouse.x, mouse.y)
                            root.editor.setParticleFieldValue(fieldItem.index, "angle",
                                Math.round(Math.atan2(p.y, p.x) * 180 / Math.PI), true)
                        }
                        onWheel: wheel => wheel.accepted = false
                    }
                }
            }
            // A picked collider's corners: drag one to move it, double-click it to remove it. The model
            // is the count, so moving a corner doesn't make the knobs again (that would end the drag).
            Repeater {
                model: fieldItem.picked ? fieldItem.points.length : 0
                delegate: Rectangle {
                    id: corner
                    required property int index
                    readonly property point at: fieldItem.points[index] ?? Qt.point(0, 0)
                    x: at.x - 5
                    y: at.y - 5
                    width: 10
                    height: 10
                    radius: 2
                    color: Theme.panel
                    border.color: Theme.accent
                    border.width: 2
                    scale: cornerArea.pressed || cornerArea.containsMouse ? 1.25 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
                    MouseArea {
                        id: cornerArea
                        onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
                        hoverEnabled: true
                        anchors.fill: parent
                        anchors.margins: -5
                        preventStealing: true
                        cursorShape: Qt.SizeAllCursor
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const p = mapToItem(root, mouse.x, mouse.y)
                            const c = lineArea.canvasAt(p.x, p.y)
                            root.editor.setColliderPoint(fieldItem.index, corner.index, c.x, c.y, true)
                        }
                        onDoubleClicked: root.editor.removeColliderPoint(fieldItem.index, corner.index)
                        onWheel: wheel => wheel.accepted = false
                    }
                }
            }
            Rectangle { // resizes the reach
                visible: fieldItem.picked && fieldItem.reach > 0
                x: fieldItem.reach - 5
                y: -5
                width: 10
                height: 10
                radius: 2
                color: Theme.panel
                border.color: Theme.accent
                border.width: 2
                MouseArea {
                    onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
                    hoverEnabled: true
                    anchors.fill: parent
                    anchors.margins: -5
                    preventStealing: true
                    cursorShape: Qt.SizeHorCursor
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const p = mapToItem(fieldItem, mouse.x, mouse.y)
                        root.editor.setParticleFieldValue(fieldItem.index, "radius",
                            Math.max(1, Math.round(Math.hypot(p.x, p.y) / root.zoom)), true)
                    }
                    onWheel: wheel => wheel.accepted = false
                }
            }
        }
    }

    Item {
        id: cross
        visible: root.enabledHere && root.editor.liveLayer
        width: 36
        height: 36
        x: Math.round(root.offset.x + root.emitter.x * root.zoom - width / 2)
        y: Math.round(root.offset.y + root.emitter.y * root.zoom - height / 2)

        Arm { x: 17; y: 3; width: 4; height: 11 }   // up
        Arm { x: 17; y: 22; width: 4; height: 11 }  // down
        Arm { x: 3; y: 17; width: 11; height: 4 }   // left
        Arm { x: 22; y: 17; width: 11; height: 4 }  // right
        Rectangle {
            anchors.centerIn: parent
            width: 4
            height: 4
            radius: 2
            color: Theme.accent
            border.color: Theme.handleBorder
        }

        MouseArea {
            onContainsMouseChanged: root.hovered += containsMouse ? 1 : -1
            id: area
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeAllCursor
            property point pressAt
            property point startEmitter
            onPressed: mouse => {
                pressAt = mapToItem(root, mouse.x, mouse.y)
                startEmitter = root.emitter
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                const x = startEmitter.x + (p.x - pressAt.x) / root.zoom
                const y = startEmitter.y + (p.y - pressAt.y) / root.zoom
                root.editor.liveEmitter = Qt.point(Math.floor(x) + 0.5, Math.floor(y) + 0.5)
            }
            onWheel: wheel => wheel.accepted = false
        }
    }
}
