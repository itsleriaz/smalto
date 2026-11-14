import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Smalto

// A color that is picked by clicking it: while `armed` it has a ring a little way out (as the accent
// swatches in Preferences) and a pipette badge on its corner, meaning the next color chosen on the
// canvas or in the color panel goes to it. The owner decides what a click does (`clicked`) and what a
// right click does (`removeRequested`); the color is the item's own `color`.
Rectangle {
    id: root

    property bool armed: false
    signal clicked()
    signal removeRequested()

    implicitWidth: 26
    implicitHeight: 26
    radius: 6
    border.color: Theme.swatchBorder
    border.width: 1

    // A color with some alpha shows over a checkerboard.
    Item {
        z: -1
        anchors.fill: parent
        visible: root.color.a < 1
        Rectangle {
            anchors.fill: parent
            radius: root.radius
            color: "#d9d9d9"
        }
        Rectangle { x: 0; y: 0; width: parent.width / 2; height: parent.height / 2; color: "#8a8a8a"; radius: root.radius / 2 }
        Rectangle { x: parent.width / 2; y: parent.height / 2; width: parent.width / 2; height: parent.height / 2; color: "#8a8a8a"; radius: root.radius / 2 }
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width + 6
        height: parent.height + 6
        radius: root.radius + 3
        color: "transparent"
        border.color: Theme.text
        border.width: 1.5
        opacity: root.armed ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
    Rectangle {
        x: parent.width - width / 2 - 2
        y: parent.height - height / 2 - 2
        width: 14
        height: 14
        radius: 7
        color: Theme.panelSolid
        border.color: Theme.separator
        visible: root.armed
        ColorImage {
            anchors.centerIn: parent
            width: 9
            height: 9
            source: "qrc:/icons/pipette.svg"
            sourceSize: Qt.size(18, 18)
            color: Theme.text
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => mouse.button === Qt.RightButton ? root.removeRequested() : root.clicked()
    }
}
