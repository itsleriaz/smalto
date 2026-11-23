pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// The file's tags as a list: click one to select its frames, double-click (or
// the menu) to edit it, and delete it alone or together with its frames.
Panel {
    id: root

    required property Editor editor

    // What the window arranges (PanelColumns), see ColorPanel.
    property real preferredHeight: 220
    readonly property real wantHeight: preferredHeight
    readonly property real minHeight: 120
    readonly property int shrinkRank: 1
    movable: true

    readonly property int rowHeight: 40
    // The tags whose name contains the search text (case-insensitive), as indices into editor.tags.
    readonly property string query: search.text.trim().toLowerCase()
    readonly property var shownTags: {
        const all = editor.tags
        const out = []
        for (let i = 0; i < all.length; ++i)
            if (query === "" || all[i].name.toLowerCase().indexOf(query) >= 0)
                out.push(i)
        return out
    }
    // Deleting a tag's frames must leave at least one frame.
    function canDeleteFrames(tag): bool {
        return tag !== undefined && tag.to - tag.from + 1 < editor.frameCount
    }
    function directionText(tag): string {
        const names = ["Forward", "Reverse", "Ping-pong", "Ping-pong reverse"]
        return names[tag.direction] + (tag.repeat > 0 ? " ×" + tag.repeat : "")
    }
    function select(index) {
        const tag = editor.tags[index]
        if (!tag)
            return
        editor.selectFrames(tag.from, tag.to)
        if (editor.currentFrame < tag.from || editor.currentFrame > tag.to)
            editor.currentFrame = tag.from
    }

    TagEditor {
        id: tagEditor
        editor: root.editor
    }

    AppMenu {
        id: tagMenu
        property int tagIndex: -1
        readonly property var tag: root.editor.tags[tagIndex]
        AppMenuItem { text: "Edit Tag…"; onTriggered: tagEditor.openFor(tagMenu.tagIndex) }
        AppMenuItem { text: "Select Frames"; onTriggered: root.select(tagMenu.tagIndex) }
        AppMenuSeparator {}
        AppMenuItem { text: "Delete Tag"; danger: true; onTriggered: root.editor.removeTag(tagMenu.tagIndex) }
        AppMenuItem {
            text: "Delete Tag and Frames"
            danger: true
            enabled: root.canDeleteFrames(tagMenu.tag)
            onTriggered: root.editor.removeTagAndFrames(tagMenu.tagIndex)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            PanelTitle {
                Layout.fillWidth: true
                text: "Tags"
            }
            IconButton {
                iconName: "plus"
                tip: "New tag from the selected frames"
                onClicked: root.editor.addTag()
            }
        }

        AppTextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Filter tags"
            visible: root.editor.tags.length > 0
            Keys.onEscapePressed: { text = ""; focus = false }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.shownTags
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            Text {
                visible: list.count === 0
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.editor.tags.length === 0 ? "No tags. Select frames, then press + to tag them."
                                                    : "No tags match \u201c" + search.text.trim() + "\u201d"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }

            delegate: Rectangle {
                id: row
                required property int modelData // index into editor.tags
                readonly property int index: modelData
                readonly property var tag: root.editor.tags[modelData]
                readonly property bool playing: root.editor.playingTag === index
                readonly property bool inTag: root.editor.currentFrame >= tag.from && root.editor.currentFrame <= tag.to

                width: list.width - (list.ScrollBar.vertical.visible ? 10 : 0)
                height: root.rowHeight
                radius: Theme.radiusSmall
                color: inTag ? Theme.accentSoft : area.containsMouse ? Theme.hover : "transparent"
                border.width: playing ? 1 : 0
                border.color: Theme.accent

                Rectangle {
                    id: swatch
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    radius: 7
                    color: row.tag.color
                }
                Column {
                    anchors.left: swatch.right
                    anchors.leftMargin: 10
                    anchors.right: buttons.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: row.tag.name
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: "Frames " + (row.tag.from + 1) + "–" + (row.tag.to + 1)
                              + " · " + root.directionText(row.tag)
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSizeSmall - 1
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            tagMenu.tagIndex = row.index
                            tagMenu.popup()
                        }
                    }
                    onClicked: mouse => { if (mouse.button === Qt.LeftButton) root.select(row.index) }
                    onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) tagEditor.openFor(row.index) }
                }

                Row {
                    id: buttons
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: area.containsMouse || editHover.hovered
                    HoverHandler { id: editHover }
                    IconButton {
                        iconName: "sliders-horizontal"
                        tip: "Edit tag"
                        onClicked: tagEditor.openFor(row.index)
                    }
                    IconButton {
                        iconName: "trash-2"
                        tip: "Delete tag (the frames stay)"
                        onClicked: root.editor.removeTag(row.index)
                    }
                }
            }
        }
    }
}
