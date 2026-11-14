import QtQuick
import QtQuick.Controls.Basic
import Smalto

Button {
    id: control

    property string iconName
    property bool active: false
    // A destructive action (delete): goes red on hover. The trash icon is one unless told otherwise.
    property bool danger: iconName === "trash-2"
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
    icon.color: !enabled ? Theme.textFaint
                : (danger && hovered) ? Theme.danger
                : (active || hovered) ? Theme.text : Theme.textDim

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.active ? Theme.accentSoft
             : (control.danger && control.enabled && control.hovered)
               ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, control.down ? 0.24 : 0.14)
             : control.down ? Theme.pressed
             : control.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    AppToolTip {
        text: control.tip
        active: control.hovered && control.tip !== "" && !control.pressed
    }
}
