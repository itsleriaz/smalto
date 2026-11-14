import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control

    property string iconName
    property bool active: false
    property string tip

    implicitWidth: Theme.buttonSize
    implicitHeight: Theme.buttonSize
    padding: 0
    flat: true
    display: AbstractButton.IconOnly
    focusPolicy: Qt.NoFocus

    icon.source: iconName ? "qrc:/icons/" + iconName + ".svg" : ""
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: !enabled ? Theme.textFaint : (active || hovered) ? Theme.text : Theme.textDim

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.active ? Theme.accentSoft
             : control.down ? Theme.pressed
             : control.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    AppToolTip {
        text: control.tip
        active: control.hovered && control.tip !== "" && !control.pressed
    }
}
