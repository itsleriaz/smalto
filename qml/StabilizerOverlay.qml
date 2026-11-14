import QtQuick
import QtQuick.Shapes
import Smalto

// While a stabilized stroke is drawn: the string from the brush to the pointer, and a dot at the
// brush, black under white like the lasso (canvas marks, not theme colors). Takes no input.
Item {
    id: root

    required property CanvasView canvas

    visible: canvas.stabilizing

    Shape {
        anchors.fill: parent
        ShapePath {
            strokeColor: "black"
            strokeWidth: 3
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: root.canvas.ropeBrush.x
            startY: root.canvas.ropeBrush.y
            PathLine { x: root.canvas.ropePointer.x; y: root.canvas.ropePointer.y }
        }
        ShapePath {
            strokeColor: "white"
            strokeWidth: 1
            fillColor: "transparent"
            startX: root.canvas.ropeBrush.x
            startY: root.canvas.ropeBrush.y
            PathLine { x: root.canvas.ropePointer.x; y: root.canvas.ropePointer.y }
        }
    }
    Rectangle {
        x: root.canvas.ropeBrush.x - 3
        y: root.canvas.ropeBrush.y - 3
        width: 6
        height: 6
        radius: 3
        color: "white"
        border.color: "black"
    }
}
