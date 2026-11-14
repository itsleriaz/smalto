pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import Smalto

// A custom easing (core/Easing): the curve from (0, 0) to (1, 1) with its two handles, dragged to shape
// it. Time runs right, the way along runs up; a handle can go past the top or the bottom (an overshoot)
// but not past the sides. Reports each drag step with `edited`, the whole drag continuous.
Item {
    id: root

    property real x1: 1 / 3
    property real y1: 1 / 3
    property real x2: 2 / 3
    property real y2: 2 / 3
    signal edited(real x1, real y1, real x2, real y2, bool continuous)

    implicitWidth: 160
    implicitHeight: 112

    // What the box shows of y: a quarter past each end.
    readonly property real low: -0.25
    readonly property real high: 1.25
    readonly property real pad: 8
    function px(x: real): real { return pad + x * (width - 2 * pad) }
    function py(y: real): real { return pad + (high - y) / (high - low) * (height - 2 * pad) }
    function curveAt(sx: real, sy: real): point {
        const x = Math.max(0, Math.min(1, (sx - pad) / (width - 2 * pad)))
        const y = Math.max(low, Math.min(high, high - (sy - pad) / (height - 2 * pad) * (high - low)))
        return Qt.point(Math.round(x * 100) / 100, Math.round(y * 100) / 100)
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: Theme.controlTrack
        border.color: Theme.controlBorder
    }
    // Where 0 and 1 are, and the straight line between (linear).
    Rectangle { x: root.px(0); y: root.py(1); width: root.px(1) - root.px(0); height: 1; color: Theme.separator }
    Rectangle { x: root.px(0); y: root.py(0); width: root.px(1) - root.px(0); height: 1; color: Theme.separator }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.separator
            strokeWidth: 1
            fillColor: "transparent"
            startX: root.px(0)
            startY: root.py(0)
            PathLine { x: root.px(1); y: root.py(1) }
        }
        ShapePath { // the handles' stems
            strokeColor: Theme.textFaint
            strokeWidth: 1
            fillColor: "transparent"
            startX: root.px(0)
            startY: root.py(0)
            PathLine { x: root.px(root.x1); y: root.py(root.y1) }
            PathMove { x: root.px(1); y: root.py(1) }
            PathLine { x: root.px(root.x2); y: root.py(root.y2) }
        }
        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 2
            fillColor: "transparent"
            startX: root.px(0)
            startY: root.py(0)
            PathCubic {
                control1X: root.px(root.x1)
                control1Y: root.py(root.y1)
                control2X: root.px(root.x2)
                control2Y: root.py(root.y2)
                x: root.px(1)
                y: root.py(1)
            }
        }
    }

    component Knob: Rectangle {
        id: knob
        property real cx
        property real cy
        signal moved(point at)
        x: root.px(cx) - width / 2
        y: root.py(cy) - height / 2
        width: 11
        height: 11
        radius: 5.5
        color: knobArea.pressed || knobArea.containsMouse ? Theme.accent : Theme.panel
        border.color: Theme.accent
        border.width: 2
        MouseArea {
            id: knobArea
            anchors.fill: parent
            anchors.margins: -5
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeAllCursor
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                knob.moved(root.curveAt(p.x, p.y))
            }
        }
    }
    Knob {
        cx: root.x1
        cy: root.y1
        onMoved: at => root.edited(at.x, at.y, root.x2, root.y2, true)
    }
    Knob {
        cx: root.x2
        cy: root.y2
        onMoved: at => root.edited(root.x1, root.y1, at.x, at.y, true)
    }
}
