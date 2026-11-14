pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Themed message dialog. `buttons` is a list of
//   { id: "save", text: "Save", kind: "primary" | "danger" | "", leading: bool }
// (leading buttons sit on the left, the rest on the right). Emits
// `clicked(id)`; Esc emits `clicked("cancel")`, Enter clicks the primary
// button.
Popup {
    id: root

    property string title
    property string text
    property var buttons: [{ id: "ok", text: qsTr("OK"), kind: "primary" }]
    signal clicked(string id)

    property bool answered: false

    function choose(id) {
        answered = true
        close()
        clicked(id)
    }

    onAboutToShow: answered = false
    onOpened: content.forceActiveFocus()
    onClosed: if (!answered) clicked("cancel")

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 22
    closePolicy: Popup.CloseOnEscape
    background: Panel { solid: true }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    contentItem: ColumnLayout {
        id: content
        spacing: 10
        focus: true

        function acceptPrimary() {
            const primary = root.buttons.find(b => b.kind === "primary")
            if (primary)
                root.choose(primary.id)
        }
        Keys.onReturnPressed: acceptPrimary()
        Keys.onEnterPressed: acceptPrimary()

        Text {
            Layout.maximumWidth: 400
            visible: root.title !== ""
            text: root.title
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            Layout.maximumWidth: 400
            Layout.preferredWidth: Math.min(400, implicitWidth)
            visible: root.text !== ""
            text: root.text
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSize
            lineHeight: 1.2
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 8

            Repeater {
                model: root.buttons.filter(b => b.leading)
                AppButton {
                    required property var modelData
                    text: modelData.text
                    primary: modelData.kind === "primary"
                    danger: modelData.kind === "danger"
                    onClicked: root.choose(modelData.id)
                }
            }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 24 }
            Repeater {
                model: root.buttons.filter(b => !b.leading)
                AppButton {
                    required property var modelData
                    text: modelData.text
                    primary: modelData.kind === "primary"
                    danger: modelData.kind === "danger"
                    onClicked: root.choose(modelData.id)
                }
            }
        }
    }
}
