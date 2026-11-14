import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// The duration of the selected frames (or the current one), from the Frame
// menu. A number you can scrub, type or scroll; Enter or the button closes it.
Popup {
    id: root

    required property Editor editor
    readonly property int frames: editor.rangeType === Editor.FrameRange ? editor.range.width : 1

    modal: true
    dim: true
    focus: true
    anchors.centerIn: Overlay.overlay
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel { solid: true }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    onOpened: durationField.startEditing()

    contentItem: ColumnLayout {
        spacing: 14
        Text {
            text: root.frames > 1 ? qsTr("Duration of %1 frames").arg(root.frames) : qsTr("Frame duration")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        NumberControl {
            id: durationField
            value: root.editor.frameDuration
            from: 1
            to: 65535
            step: 10
            bigStep: 100
            suffix: " ms"
            fieldWidth: 90
            onMoved: v => root.editor.setFramesDuration(v)
            onEditingChanged: if (!editing) root.close()
        }
    }
}
