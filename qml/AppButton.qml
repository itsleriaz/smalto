import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Text button for dialogs: plain (flat), primary (accent) or danger.
Button {
    id: button

    property bool primary: false
    property bool danger: false
    property string tip

    // Tab reaches it (a ring shows then); a click never takes focus from the canvas.
    focusPolicy: Qt.TabFocus
    font.pixelSize: Theme.fontSize
    font.weight: primary ? Font.DemiBold : Font.Normal

    readonly property color dangerSoft: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 1)

    contentItem: Text {
        text: button.text
        font: button.font
        color: button.danger ? Theme.danger : button.primary ? Theme.textOnAccent : Theme.text
        opacity: button.enabled ? 1 : 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    // Primary: solid accent. Danger: red text on a faint red fill and outline
    // (solid red would shout over the safe choices). Plain: a quiet outlined button.
    background: Rectangle {
        implicitWidth: button.primary ? 96 : 84
        implicitHeight: 32
        radius: Theme.radiusSmall
        opacity: button.enabled ? 1 : 0.5
        border.width: button.visualFocus ? 2 : button.primary ? 0 : 1
        border.color: button.visualFocus ? Theme.accent : button.danger
                      ? Qt.rgba(button.dangerSoft.r, button.dangerSoft.g, button.dangerSoft.b, button.hovered ? 0.55 : 0.32)
                      : Theme.controlBorder
        color: button.primary
               ? (button.down ? Qt.darker(Theme.accent, 1.2) : button.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
               : button.danger
                 ? Qt.rgba(button.dangerSoft.r, button.dangerSoft.g, button.dangerSoft.b, button.down ? 0.26 : button.hovered ? 0.18 : 0.10)
                 : (button.down ? Theme.pressed : button.hovered ? Theme.hover : Theme.controlFill)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }

    AppToolTip {
        text: button.tip
        active: button.hovered && button.tip !== "" && !button.pressed
    }
}
