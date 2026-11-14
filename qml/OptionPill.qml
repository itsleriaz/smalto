pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// A pill in the options strips. With `indicator` (the default) it is an on /
// off option: a hollow circle when off, a ticked one on a solid accent fill
// when on, so its state shows even at rest. Without, it is a plain action
// (an outlined pill that does something when pressed).
Rectangle {
    id: root

    property alias text: label.text
    property bool checked: false
    property bool indicator: true
    property string tip
    signal toggled()

    implicitWidth: row.implicitWidth + (indicator ? 20 : 24)
    implicitHeight: 28
    radius: height / 2
    color: indicator && checked ? Theme.accent
         : area.pressed ? Theme.pressed
         : area.containsMouse ? Theme.hover : Theme.controlFill
    border.color: indicator && checked ? Theme.accent : Theme.controlBorder
    border.width: 1
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            visible: root.indicator
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            radius: 7
            color: root.checked ? Theme.textOnAccent : "transparent"
            border.color: root.checked ? Theme.textOnAccent : Theme.textFaint
            border.width: 1.5

            IconButton {
                anchors.centerIn: parent
                visible: root.checked
                implicitWidth: 12
                implicitHeight: 12
                iconName: "check"
                icon.width: 10
                icon.height: 10
                icon.color: Theme.accent
                hoverEnabled: false
                background: null
            }
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            color: root.indicator && root.checked ? Theme.textOnAccent : root.indicator ? Theme.textDim : Theme.text
            font.pixelSize: Theme.fontSize
            font.weight: root.indicator && root.checked ? Font.DemiBold : Font.Normal
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.toggled()
    }
    AppToolTip {
        text: root.tip
        active: area.containsMouse && root.tip !== ""
    }
}
