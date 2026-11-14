import QtQuick
import QtQuick.Shapes
import Smalto

// The lasso being drawn (Editor.lassoPath), as a thin line on screen at any
// zoom, black under white dashes like the selection's ants (canvas marks,
// not theme colors). Takes no input.
Shape {
    id: root

    required property Editor editor
    required property CanvasView canvas

    // Canvas pixels to this item, as the canvas draws them.
    readonly property var screenPath: {
        const z = canvas.zoom
        const o = canvas.offset
        return editor.lassoPath.map(p => Qt.point(o.x + p.x * z, o.y + p.y * z))
    }

    visible: editor.lassoPath.length > 1

    ShapePath {
        strokeColor: "black"
        strokeWidth: 1
        fillColor: "transparent"
        PathPolyline { path: root.screenPath }
    }
    ShapePath {
        strokeColor: "white"
        strokeWidth: 1
        fillColor: "transparent"
        strokeStyle: ShapePath.DashLine
        dashPattern: [4, 4]
        PathPolyline { path: root.screenPath }
    }
}
