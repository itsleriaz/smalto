import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Text button for dialogs: plain (flat), primary (accent) or danger.
Button {
    id: button

    property bool primary: false
    property bool danger: false
    property string tip

    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.fontSize
    font.weight: primary ? Font.DemiBold : Font.Normal

    contentItem: Text {
        text: button.text
        font: button.font
        color: button.danger ? Theme.danger : button.primary ? Theme.textOnAccent : Theme.textDim
        opacity: button.enabled ? 1 : 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitWidth: button.primary ? 96 : 80
        implicitHeight: 32
        radius: Theme.radiusSmall
        opacity: button.enabled ? 1 : 0.5
        color: button.primary
               ? (button.down ? Qt.darker(Theme.accent, 1.2) : button.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
               : (button.down ? Theme.pressed : button.hovered ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    AppToolTip {
        text: button.tip
        active: button.hovered && button.tip !== "" && !button.pressed
    }
}
