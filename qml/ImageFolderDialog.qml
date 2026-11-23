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
    property var clips: [] // [{name, count, files}], as found
    // What the list shows and the user edits: {name, count, use}, in the order of `clips`. A
    // ListModel, so a change to one row leaves the list where it is scrolled.
    ListModel { id: clipModel }
    property int revision: 0 // bumped on every edit, for the totals below
    readonly property int chosenClips: { void revision; return countWhere(r => r.use) }
    readonly property int chosenFrames: {
        void revision
        let n = 0
        for (let i = 0; i < clipModel.count; ++i)
            if (clipModel.get(i).use)
                n += clipModel.get(i).count
        return n
    }
    readonly property int singleFrameClips: { void revision; return countWhere(r => r.count === 1) }

    function countWhere(test): int {
        let n = 0
        for (let i = 0; i < clipModel.count; ++i)
            if (test(clipModel.get(i)))
                ++n
        return n
    }
    function setUse(test) { // sets `use` of every row to what `test(row)` says
        for (let i = 0; i < clipModel.count; ++i)
            clipModel.setProperty(i, "use", test(clipModel.get(i)))
        ++revision
    }

    // Scans `path`; false (and a message) when there is nothing to import.
    function start(path) {
        folder = path
        const found = editor.scanImageFolder(path)
        if (found.length === 0) {
            editor.errorOccurred(qsTr("There are no images in %1.").arg(path.replace(/^.*[\\/]/, "")))
            return false
        }
        clips = found
        clipModel.clear()
        for (const c of found)
            clipModel.append({ name: c.name, count: c.count, use: true })
        ++revision
        open()
        return true
    }

    function setClip(i, key, value) {
        clipModel.setProperty(i, key, value)
        ++revision
    }

    function apply() {
        const chosen = []
        for (let i = 0; i < clipModel.count; ++i) {
            const row = clipModel.get(i)
            if (row.use && row.name.trim() !== "")
                chosen.push({ name: row.name.trim(), files: clips[i].files })
        }
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

        RowLayout {
            Layout.preferredWidth: 380
            spacing: 8
            AppButton {
                text: qsTr("Select all")
                enabled: root.chosenClips < clipModel.count
                onClicked: root.setUse(r => true)
            }
            AppButton {
                text: qsTr("Deselect all")
                enabled: root.chosenClips > 0
                onClicked: root.setUse(r => false)
            }
            AppButton {
                text: qsTr("Deselect 1-frame")
                enabled: root.singleFrameClips > 0
                onClicked: root.setUse(r => r.use && r.count !== 1)
            }
        }

        ListView {
            id: list
            Layout.preferredWidth: 380
            Layout.preferredHeight: Math.min(contentHeight, 280)
            clip: true
            spacing: 6
            model: clipModel
            ScrollBar.vertical: AppScrollBar {}

            delegate: RowLayout {
                id: row
                required property string name
                required property int count
                required property bool use
                required property int index
                width: ListView.view.width - 12
                spacing: 8

                OptionPill {
                    text: ""
                    checked: row.use
                    onToggled: root.setClip(row.index, "use", !row.use)
                }
                AppTextField {
                    Layout.fillWidth: true
                    enabled: row.use
                    text: row.name
                    onEditingFinished: root.setClip(row.index, "name", text)
                    onAccepted: root.apply()
                }
                Text {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                    text: row.count === 1 ? qsTr("1 frame") : qsTr("%1 frames").arg(row.count)
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
