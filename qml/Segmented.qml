pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// One-of-many choice: a shared track with the chosen one raised.
Rectangle {
    id: root

    property var model: []     // labels
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 30
    radius: Theme.radiusSmall
    color: Theme.controlTrack

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: root.model
            delegate: Rectangle {
                id: pill
                required property string modelData
                required property int index
                readonly property bool current: index === root.currentIndex

                width: Math.max(44, label.implicitWidth + 20)
                height: root.height - 4
                radius: Theme.radiusSmall - 2
                color: current ? Theme.controlRaised : area.containsMouse ? Theme.hover : "transparent"
                border.color: current ? Theme.controlBorder : "transparent"
                border.width: 1
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: pill.modelData
                    color: pill.current ? Theme.text : Theme.textDim
                    font.pixelSize: Theme.fontSize
                    font.weight: pill.current ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.currentIndex = pill.index
                        root.activated(pill.index)
                    }
                }
            }
        }
    }
}
