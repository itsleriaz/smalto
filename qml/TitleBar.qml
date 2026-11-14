pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// The window's top row: the tabs and the main bar share it, in place of the
// system title bar. On macOS the window keeps its own traffic lights (the
// client area extends under them, see `Main.qml`'s flags), so only room is
// left for them; elsewhere the window is frameless and the minimize /
// maximize / close buttons are drawn here. An empty spot drags the window, a
// double click maximizes it. Children are placed by the window, inside
// `leftInset` and `rightInset`. With `menuStrip` (the in-window menus) a thin
// strip on top holds `stripContent` (the menu titles) and the window
// buttons, and the row with the children is under it.
Rectangle {
    id: root

    required property Window window

    // Room taken by the system's buttons (macOS) / ours (elsewhere).
    readonly property bool ownButtons: Qt.platform.os !== "osx"
    // The traffic lights hide in fullscreen.
    readonly property real leftInset: ownButtons || root.window.visibility === Window.FullScreen ? 0 : 82
    readonly property real rightInset: ownButtons && !menuStrip ? buttons.width : 0

    property bool menuStrip: false
    readonly property real stripHeight: menuStrip ? 30 : 0
    default property alias content: slot.data
    property alias stripContent: strip.data

    implicitHeight: 42 + stripHeight
    color: Theme.panel

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separator
    }

    // The canvas fills the window under this row; keep its tool cursor out.
    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }

    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) root.window.startSystemMove()
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: {
            if (root.window.visibility === Window.Maximized)
                root.window.showNormal()
            else
                root.window.showMaximized()
        }
    }

    Item {
        id: strip
        visible: root.menuStrip
        x: 0
        y: 0
        width: parent.width - (root.ownButtons ? buttons.width : 0)
        height: root.stripHeight
    }
    Item {
        id: slot
        anchors.fill: parent
        anchors.topMargin: root.stripHeight
    }

    component CaptionButton: Item {
        id: button
        // "minimize", "maximize", "restore" or "close"
        required property string kind
        signal clicked()

        width: 46
        height: parent.height

        Rectangle {
            anchors.fill: parent
            color: button.kind === "close" ? "#e5383b" : Theme.hover
            opacity: area.containsMouse ? (area.pressed ? 0.8 : 1) : 0
        }
        Item {
            id: glyph
            anchors.centerIn: parent
            width: 10
            height: 10
            readonly property color ink: button.kind === "close" && area.containsMouse ? "white" : Theme.text

            Rectangle { // minimize
                visible: button.kind === "minimize"
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 1
                color: glyph.ink
            }
            Rectangle { // maximize
                visible: button.kind === "maximize"
                anchors.fill: parent
                color: "transparent"
                border.color: glyph.ink
            }
            Item { // restore: a square in front of another
                visible: button.kind === "restore"
                anchors.fill: parent
                Rectangle {
                    x: 2
                    y: 0
                    width: 8
                    height: 8
                    color: "transparent"
                    border.color: glyph.ink
                }
                Rectangle {
                    x: 0
                    y: 2
                    width: 8
                    height: 8
                    color: Theme.panel
                    border.color: glyph.ink
                }
            }
            Item { // close
                visible: button.kind === "close"
                anchors.fill: parent
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 1
                    rotation: 45
                    color: glyph.ink
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 1
                    rotation: -45
                    color: glyph.ink
                }
            }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.clicked()
        }
    }

    Row {
        id: buttons
        visible: root.ownButtons
        anchors.top: parent.top
        anchors.right: parent.right
        height: root.menuStrip ? root.stripHeight : parent.height - 1

        CaptionButton {
            kind: "minimize"
            onClicked: root.window.showMinimized()
        }
        CaptionButton {
            kind: root.window.visibility === Window.Maximized ? "restore" : "maximize"
            onClicked: root.window.visibility === Window.Maximized ? root.window.showNormal() : root.window.showMaximized()
        }
        CaptionButton {
            kind: "close"
            onClicked: root.window.close()
        }
    }
}
