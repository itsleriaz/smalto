pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Edits one tag: name, color, direction and repeat count. Changes apply on
// Done as one undo step.
Popup {
    id: root

    required property Editor editor
    property int tagIndex: -1
    readonly property var tag: tagIndex >= 0 && tagIndex < editor.tags.length ? editor.tags[tagIndex] : null

    property color color
    property int repeat: 0
    property int fromFrame: 0
    property int toFrame: 0

    readonly property var colors: ["#6e8bff", "#ff6b6b", "#51cf66", "#fcc419", "#cc5de8", "#22b8cf", "#ff922b", "#adb5bd"]

    function openFor(index) {
        tagIndex = index
        if (!tag)
            return
        nameField.text = tag.name
        color = tag.color
        directionChoice.currentIndex = tag.direction
        repeat = tag.repeat
        fromFrame = tag.from
        toFrame = tag.to
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function apply() {
        if (tag)
            editor.setTag(tagIndex, {
                name: nameField.text.trim() || tag.name,
                color: color,
                direction: directionChoice.currentIndex,
                repeat: repeat,
                from: fromFrame,
                to: toFrame
            })
        close()
    }

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel { solid: true }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: root.tag ? "Tag · frames " + (root.tag.from + 1) + "–" + (root.tag.to + 1) : "Tag"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 10

            Label { text: "Name" }
            AppTextField {
                id: nameField
                Layout.preferredWidth: 240
                onAccepted: root.apply()
            }

            Label { text: "Color" }
            Row {
                spacing: 6
                Repeater {
                    model: root.colors
                    Rectangle {
                        required property string modelData
                        readonly property bool selected: Qt.colorEqual(modelData, root.color)
                        width: 22
                        height: 22
                        radius: 11
                        color: modelData
                        border.width: selected ? 2 : 0
                        border.color: Theme.text
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.color = parent.modelData
                        }
                    }
                }
            }

            Label { text: "Direction" }
            Segmented {
                id: directionChoice
                model: ["Forward", "Reverse", "Ping-pong", "Ping-pong rev."]
            }

            Label { text: "Frames" }
            RowLayout {
                spacing: 8
                NumberControl {
                    buttons: true
                    value: root.fromFrame + 1
                    from: 1
                    to: root.toFrame + 1
                    fieldWidth: 44
                    tip: "First frame"
                    onMoved: v => root.fromFrame = v - 1
                }
                Text { text: "to"; color: Theme.textDim; font.pixelSize: Theme.fontSize }
                NumberControl {
                    buttons: true
                    value: root.toFrame + 1
                    from: root.fromFrame + 1
                    to: root.editor.frameCount
                    fieldWidth: 44
                    tip: "Last frame"
                    onMoved: v => root.toFrame = v - 1
                }
            }

            Label { text: "Repeat" }
            NumberControl {
                buttons: true
                value: root.repeat
                from: 0
                to: 999
                suffix: "\u00d7"
                zeroText: "\u221e"
                fieldWidth: 44
                tip: "Times to play (\u221e: loop)"
                onMoved: v => root.repeat = v
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppButton {
                text: "Delete tag"
                danger: true
                onClicked: {
                    root.editor.removeTag(root.tagIndex)
                    root.close()
                }
            }
            AppButton {
                text: "Delete tag and frames"
                danger: true
                enabled: root.tag !== null && root.tag.to - root.tag.from + 1 < root.editor.frameCount
                tip: "Removes the tag and every frame in it"
                onClicked: {
                    root.editor.removeTagAndFrames(root.tagIndex)
                    root.close()
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: "Done"
                primary: true
                onClicked: root.apply()
            }
        }
    }
}
