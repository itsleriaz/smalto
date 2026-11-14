pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import Smalto

// The gradient being dragged (Editor.gradientLine): the line from where it starts to where it
// ends, black under white like the selection's ants, with a dot at each end (canvas marks, not
// theme colors). A radial gradient also shows the circle it reaches. Takes no input.
Shape {
    id: root

    required property Editor editor
    required property CanvasView canvas

    // Canvas pixels to this item, as the canvas draws them (the pixel's center).
    readonly property var line: {
        const l = editor.gradientLine
        if (l.length < 2)
            return []
        const z = canvas.zoom
        const o = canvas.offset
        return [Qt.point(o.x + (l[0].x + 0.5) * z, o.y + (l[0].y + 0.5) * z),
                Qt.point(o.x + (l[1].x + 0.5) * z, o.y + (l[1].y + 0.5) * z)]
    }
    readonly property real radius: line.length > 1 ? Math.hypot(line[1].x - line[0].x, line[1].y - line[0].y) : 0

    visible: line.length > 1 && editor.tool === Editor.Gradient

    ShapePath {
        strokeColor: "black"
        strokeWidth: 3
        fillColor: "transparent"
        PathPolyline { path: root.line }
    }
    ShapePath {
        strokeColor: "white"
        strokeWidth: 1
        fillColor: "transparent"
        PathPolyline { path: root.line }
    }
    // The reach of a radial gradient.
    ShapePath {
        strokeColor: "white"
        strokeWidth: 1
        fillColor: "transparent"
        strokeStyle: ShapePath.DashLine
        dashPattern: [4, 4]
        PathAngleArc {
            centerX: root.line.length > 1 ? root.line[0].x : 0
            centerY: root.line.length > 1 ? root.line[0].y : 0
            radiusX: root.editor.gradientShape === 1 ? root.radius : 0
            radiusY: root.editor.gradientShape === 1 ? root.radius : 0
            startAngle: 0
            sweepAngle: 360
        }
    }
    // The two ends. While the gradient is open they can be dragged (the other end stays).
    Repeater {
        model: root.line.length > 1 ? 2 : 0
        delegate: Item {
            id: handle
            required property int index
            x: root.line[index].x - 12
            y: root.line[index].y - 12
            width: 24
            height: 24
            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: handle.index === 0 ? "white" : "transparent"
                border.color: "white"
                border.width: 1.5
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: 5
                    color: "transparent"
                    border.color: "black"
                    border.width: 1
                    z: -1
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.editor.gradientEditable
                cursorShape: Qt.SizeAllCursor
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const x = (p.x - root.canvas.offset.x) / root.canvas.zoom - 0.5
                    const y = (p.y - root.canvas.offset.y) / root.canvas.zoom - 0.5
                    const l = root.editor.gradientLine
                    if (handle.index === 0)
                        root.editor.editGradient(x, y, l[1].x, l[1].y)
                    else
                        root.editor.editGradient(l[0].x, l[0].y, x, y)
                }
            }
        }
    }
}
