pragma ComponentBehavior: Bound

import QtQuick

// Resize grips for a frameless window: a thin strip along each side and
// corner, handing the drag to the system. Not used while maximized.
Item {
    id: root

    required property Window window
    readonly property int grip: 6
    enabled: window.visibility === Window.Windowed

    component Edge: MouseArea {
        id: edge
        required property int edges
        property int shape: Qt.ArrowCursor
        cursorShape: shape
        onPressed: root.window.startSystemResize(edge.edges)
    }

    Edge { edges: Qt.LeftEdge; shape: Qt.SizeHorCursor; x: 0; y: root.grip; width: root.grip; height: parent.height - 2 * root.grip }
    Edge { edges: Qt.RightEdge; shape: Qt.SizeHorCursor; x: parent.width - root.grip; y: root.grip; width: root.grip; height: parent.height - 2 * root.grip }
    Edge { edges: Qt.TopEdge; shape: Qt.SizeVerCursor; x: root.grip; y: 0; width: parent.width - 2 * root.grip; height: root.grip }
    Edge { edges: Qt.BottomEdge; shape: Qt.SizeVerCursor; x: root.grip; y: parent.height - root.grip; width: parent.width - 2 * root.grip; height: root.grip }
    Edge { edges: Qt.LeftEdge | Qt.TopEdge; shape: Qt.SizeFDiagCursor; x: 0; y: 0; width: root.grip; height: root.grip }
    Edge { edges: Qt.RightEdge | Qt.BottomEdge; shape: Qt.SizeFDiagCursor; x: parent.width - root.grip; y: parent.height - root.grip; width: root.grip; height: root.grip }
    Edge { edges: Qt.RightEdge | Qt.TopEdge; shape: Qt.SizeBDiagCursor; x: parent.width - root.grip; y: 0; width: root.grip; height: root.grip }
    Edge { edges: Qt.LeftEdge | Qt.BottomEdge; shape: Qt.SizeBDiagCursor; x: 0; y: parent.height - root.grip; width: root.grip; height: root.grip }
}
