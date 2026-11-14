import QtQuick
import QtQuick.Effects

// Floating rounded panel. Swallows pointer input so it never reaches the canvas.
Item {
    id: root

    default property alias content: container.data
    property int padding: 8
    property real radius: Theme.radius
    // Docked chrome: no shadow, no rounded card. `docked` keeps the fill and
    // a hairline on the inner edge; `bare` draws nothing (the parent paints).
    property bool docked: false
    property bool bare: false
    // With `docked`: the edge that meets the rest of the window gets a hairline
    // (Qt.TopEdge or Qt.BottomEdge; 0 for none).
    property int dockEdge: 0
    // Cuts what overflows the panel (a panel squeezed below its contents).
    property bool clipContent: false
    // A side panel the window arranges: a grip at the top edge drags it to
    // another place or the other side (see PanelColumns). The pointer's
    // travel since the press, and where it is in the window, come with `moveBy`.
    property bool movable: false
    signal moveStarted()
    signal moveBy(real dx, real dy, real sceneX, real sceneY)
    signal moveEnded()

    RectangularShadow {
        visible: !root.docked && !root.bare
        anchors.fill: background
        radius: background.radius
        offset.y: 8
        blur: 28
        spread: 0
        color: Theme.shadow
    }

    Rectangle {
        id: background
        anchors.fill: parent
        radius: root.docked ? 0 : root.radius
        visible: !root.bare
        color: Theme.panel
        border.color: Theme.panelBorder
        border.width: root.docked ? 0 : 1
    }

    Rectangle {
        visible: root.docked && root.dockEdge !== 0
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.dockEdge === Qt.TopEdge ? 0 : parent.height - 1
        height: 1
        color: Theme.separator
    }

    MouseArea {
        enabled: !root.bare
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    Item {
        visible: root.movable
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        z: 30
        width: 64
        height: 14

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 4
            width: 28
            height: 4
            radius: 2
            color: moveDrag.active ? Theme.accent : Theme.textFaint
            opacity: moveDrag.active || moveHover.hovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        HoverHandler {
            id: moveHover
            cursorShape: moveDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        }
        DragHandler {
            id: moveDrag
            target: null
            onActiveChanged: if (active) root.moveStarted(); else root.moveEnded()
            onCentroidChanged: if (active) {
                root.moveBy(centroid.scenePosition.x - centroid.scenePressPosition.x,
                            centroid.scenePosition.y - centroid.scenePressPosition.y,
                            centroid.scenePosition.x, centroid.scenePosition.y)
            }
        }
        AppToolTip {
            text: qsTr("Drag to move this panel")
            active: moveHover.hovered && !moveDrag.active
        }
    }

    Item {
        id: container
        anchors.fill: parent
        anchors.margins: root.padding
        clip: root.clipContent
    }
}
