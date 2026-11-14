pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// A tone curve: input along the bottom, output up the side (0..255).
// `points` is flat, x0, y0, x1, y1, ... sorted by x. Click an empty spot to
// add a point, drag one to move it, double-click it to remove it. `table`
// (256 outputs, from Editor.curveTable) is the line drawn through them.
Item {
    id: root

    property var points: [0, 0, 255, 255]
    property var table: []
    property color lineColor: Theme.accent
    signal edited(var points)

    implicitWidth: 192
    implicitHeight: 192

    readonly property real grab: 9
    property int dragging: -1

    function px(x: real): real { return x / 255 * width }
    function py(y: real): real { return height - y / 255 * height }
    function valueX(p: real): int { return Math.max(0, Math.min(255, Math.round(p / width * 255))) }
    function valueY(p: real): int { return Math.max(0, Math.min(255, Math.round((height - p) / height * 255))) }
    function count(): int { return points.length / 2 }
    function nearest(mx: real, my: real): int {
        let best = -1
        let bestDistance = grab * grab
        for (let i = 0; i < count(); ++i) {
            const dx = px(points[i * 2]) - mx, dy = py(points[i * 2 + 1]) - my
            if (dx * dx + dy * dy <= bestDistance) {
                bestDistance = dx * dx + dy * dy
                best = i
            }
        }
        return best
    }

    onPointsChanged: canvas.requestPaint()
    onTableChanged: canvas.requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { canvas.requestPaint() }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: Theme.hover
        border.color: Theme.separator
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.lineWidth = 1
            ctx.strokeStyle = Theme.separator
            ctx.beginPath()
            for (let i = 1; i < 4; ++i) {
                ctx.moveTo(Math.round(width * i / 4) + 0.5, 0)
                ctx.lineTo(Math.round(width * i / 4) + 0.5, height)
                ctx.moveTo(0, Math.round(height * i / 4) + 0.5)
                ctx.lineTo(width, Math.round(height * i / 4) + 0.5)
            }
            ctx.stroke()
            ctx.strokeStyle = Theme.textFaint
            ctx.beginPath()
            ctx.moveTo(0, height)
            ctx.lineTo(width, 0)
            ctx.stroke()
            if (root.table.length === 256) {
                ctx.strokeStyle = root.lineColor
                ctx.lineWidth = 2
                ctx.beginPath()
                for (let x = 0; x < 256; ++x) {
                    if (x === 0)
                        ctx.moveTo(root.px(x), root.py(root.table[x]))
                    else
                        ctx.lineTo(root.px(x), root.py(root.table[x]))
                }
                ctx.stroke()
            }
            ctx.fillStyle = Theme.text
            for (let i = 0; i < root.count(); ++i) {
                ctx.beginPath()
                ctx.arc(root.px(root.points[i * 2]), root.py(root.points[i * 2 + 1]), 4, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        onPressed: mouse => {
            let i = root.nearest(mouse.x, mouse.y)
            if (i < 0) { // a new point, in order
                const p = root.points.slice()
                const x = root.valueX(mouse.x)
                i = 0
                while (i < root.count() && root.points[i * 2] < x)
                    ++i
                if (i < root.count() && root.points[i * 2] === x)
                    return
                p.splice(i * 2, 0, x, root.valueY(mouse.y))
                root.points = p
                root.edited(p)
            }
            root.dragging = i
        }
        onPositionChanged: mouse => {
            const i = root.dragging
            if (i < 0)
                return
            const p = root.points.slice()
            const lo = i > 0 ? p[i * 2 - 2] + 1 : 0
            const hi = i < root.count() - 1 ? p[i * 2 + 2] - 1 : 255
            p[i * 2] = Math.max(lo, Math.min(hi, root.valueX(mouse.x)))
            p[i * 2 + 1] = root.valueY(mouse.y)
            root.points = p
            root.edited(p)
        }
        onReleased: root.dragging = -1
        onDoubleClicked: mouse => {
            const i = root.nearest(mouse.x, mouse.y)
            if (i < 0 || root.count() <= 2)
                return
            const p = root.points.slice()
            p.splice(i * 2, 2)
            root.points = p
            root.edited(p)
        }
    }
}
