pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Import a folder of images as one animation: every numbered sequence in it (and in its subfolders,
// walk_1.png, walk_2.png ...) becomes a tagged range of frames, and every other image a tag of one frame.
// The list shows what was found; untick what to leave out and rename what the tags are called.
Popup {
    id: root

    required property Editor editor

    property string folder: ""
    property var clips: [] // [{name, count, files, use}]
    readonly property int chosenClips: clips.filter(c => c.use).length
    readonly property int chosenFrames: clips.reduce((n, c) => n + (c.use ? c.count : 0), 0)

    // Scans `path`; false (and a message) when there is nothing to import.
    function start(path) {
        folder = path
        const found = editor.scanImageFolder(path)
        if (found.length === 0) {
            editor.errorOccurred(qsTr("There are no images in %1.").arg(path.replace(/^.*[\\/]/, "")))
            return false
        }
        clips = found.map(c => ({ name: c.name, count: c.count, files: c.files, use: true }))
        open()
        return true
    }

    function setClip(i, key, value) {
        const next = clips.slice()
        next[i] = Object.assign({}, next[i], { [key]: value })
        clips = next
    }

    function apply() {
        const chosen = clips.filter(c => c.use && c.name.trim() !== "")
                            .map(c => ({ name: c.name.trim(), files: c.files }))
        if (chosen.length > 0 && editor.openImageClips(chosen, folder))
            close()
    }

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Panel { solid: true }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: qsTr("Import Folder as Animation")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            Layout.preferredWidth: 380
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            text: qsTr("Each animation found in “%1” becomes a tag over its frames, all in one sprite.")
                      .arg(root.folder.replace(/^.*[\\/]/, ""))
        }

        ListView {
            id: list
            Layout.preferredWidth: 380
            Layout.preferredHeight: Math.min(contentHeight, 280)
            clip: true
            spacing: 6
            model: root.clips
            ScrollBar.vertical: AppScrollBar {}

            delegate: RowLayout {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width - 12
                spacing: 8

                OptionPill {
                    text: ""
                    checked: row.modelData.use
                    onToggled: root.setClip(row.index, "use", !row.modelData.use)
                }
                AppTextField {
                    Layout.fillWidth: true
                    enabled: row.modelData.use
                    text: row.modelData.name
                    onEditingFinished: root.setClip(row.index, "name", text)
                    onAccepted: root.apply()
                }
                Text {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                    text: row.modelData.count === 1 ? qsTr("1 frame") : qsTr("%1 frames").arg(row.modelData.count)
                }
            }
        }

        Text {
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            text: qsTr("%1 tags, %2 frames").arg(root.chosenClips).arg(root.chosenFrames)
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Import")
                primary: true
                enabled: root.chosenClips > 0
                onClicked: root.apply()
            }
        }
    }
}
