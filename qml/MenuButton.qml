import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Smalto

// A labeled button that opens a menu: the name and a small chevron (none in
// the menu bar, `chevron: false`). `active` is for the menu being open.
Button {
    id: control

    property bool active: false
    property bool chevron: true

    implicitHeight: 30
    implicitWidth: row.implicitWidth + (chevron ? 20 : 16)
    padding: 0
    flat: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.active ? Theme.accentSoft
             : control.down ? Theme.pressed
             : control.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 3
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                color: control.active || control.hovered ? Theme.text : Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            ColorImage {
                visible: control.chevron
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                source: "qrc:/icons/chevron-down.svg"
                sourceSize: Qt.size(24, 24)
                color: control.active || control.hovered ? Theme.text : Theme.textFaint
            }
        }
    }
}
