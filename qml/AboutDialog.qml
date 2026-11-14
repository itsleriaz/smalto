import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// About Smalto: the version, the license and the credits
// (AboutSettings). Esc or the button closes it.
Popup {
    id: root

    modal: true
    dim: true
    focus: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel {}
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    contentItem: ColumnLayout {
        spacing: 14
        AboutSettings {
            Layout.preferredWidth: 420
            Layout.preferredHeight: Math.min(contentHeight, 460)
        }
        AppButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Close")
            primary: true
            onClicked: root.close()
        }
    }
}
