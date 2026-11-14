pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Palettes from lospec.com, in the presets popup's Lospec tab: browse by tag,
// color count and order, or open one by name or link. Clicking a palette
// loads it (one undo step); each can be saved as a preset or opened on Lospec.
ColumnLayout {
    id: root

    required property Editor editor

    // The palette being pointed at (empty: none), for a preview on the sprite.
    signal previewColors(var colors)
    // Picking: a click hands the palette over (and marks it) instead of
    // loading it.
    property bool picking: false
    property string selectedName: ""
    signal picked(var colors, string name)

    property string tag: ""
    property int countMode: 0   // index into countModes
    property int colors: 8
    property int sort: 0        // index into sorts
    readonly property var countModes: [LospecClient.AnyCount, LospecClient.ExactCount, LospecClient.MaxCount]
    readonly property var sorts: [LospecClient.Downloads, LospecClient.Newest, LospecClient.Alphabetical,
                                  LospecClient.DefaultSort]

    // Tags many palettes on Lospec carry (counted on its most downloaded ones).
    readonly property var popularTags: ["gameboy", "pastel", "retro", "dark", "vibrant", "monochrome", "fantasy",
                                        "desaturated", "soft", "vintage", "warm", "hardware", "fantasyconsole",
                                        "dawnbringer", "endesga", "cute"]
    function useTag(name) {
        tag = tag === name ? "" : name
        tagField.text = tag
        browse(0)
    }

    function browse(page) {
        LospecClient.browse(tag, colors, countModes[countMode], sorts[sort], page)
    }
    // The first time the tab shows: the most popular palettes.
    function start() {
        if (LospecClient.results.length === 0 && !LospecClient.loading && LospecClient.error === "")
            browse(0)
    }

    spacing: 8

    // Scrubbing the color count asks once it settles.
    Timer {
        id: settle
        interval: 350
        onTriggered: root.browse(0)
    }

    RowLayout {
        spacing: 6
        AppTextField {
            id: tagField
            Layout.fillWidth: true
            placeholderText: "Tag, e.g. gameboy"
            onAccepted: {
                root.tag = text.trim().toLowerCase()
                root.browse(0)
            }
        }
        AppComboBox {
            implicitWidth: 104
            model: ["Popular", "Newest", "A–Z", "Default"]
            currentIndex: root.sort
            onActivated: index => {
                root.sort = index
                root.browse(0)
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: root.popularTags
            delegate: Rectangle {
                id: chip
                required property string modelData
                readonly property bool active: root.tag === modelData
                implicitWidth: chipText.implicitWidth + 14
                implicitHeight: 22
                radius: 11
                color: active ? Theme.accentSoft : chipArea.containsMouse ? Theme.pressed : Theme.hover
                border.color: active ? Theme.accent : "transparent"
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.modelData
                    color: chip.active ? Theme.text : Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                }
                MouseArea {
                    id: chipArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.useTag(chip.modelData)
                }
            }
        }
    }

    RowLayout {
        spacing: 6
        Segmented {
            model: ["Any size", "Exactly", "At most"]
            currentIndex: root.countMode
            onActivated: index => {
                root.countMode = index
                root.browse(0)
            }
        }
        NumberControl {
            visible: root.countMode !== 0
            buttons: true
            value: root.colors
            from: 1
            to: 256
            suffix: " colors"
            fieldWidth: 76
            onMoved: v => {
                root.colors = v
                settle.restart()
            }
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 60), 320)
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        model: LospecClient.results
        opacity: LospecClient.loading ? 0.5 : 1
        ScrollBar.vertical: AppScrollBar {}

        delegate: Rectangle {
            id: entry
            required property var modelData
            readonly property bool saved: PaletteLibrary.hasPreset(modelData.name)
            width: list.width
            height: content.implicitHeight + 12
            radius: Theme.radiusSmall
            color: root.picking && entry.modelData.name === root.selectedName ? Theme.accentSoft
                 : area.pressed ? Theme.pressed : area.containsMouse ? Theme.hover : "transparent"

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                onContainsMouseChanged: root.previewColors(containsMouse ? entry.modelData.colors : [])
                onClicked: root.picking ? root.picked(entry.modelData.colors, entry.modelData.name)
                                        : root.editor.setPaletteColors(entry.modelData.colors)
            }

            ColumnLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                RowLayout {
                    spacing: 6
                    Text {
                        text: entry.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: entry.modelData.count
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        font.features: { "tnum": 1 }
                    }
                }
                Text {
                    visible: text !== ""
                    text: [entry.modelData.author ? "by " + entry.modelData.author : "",
                           entry.modelData.downloads ? entry.modelData.downloads + " downloads" : ""]
                          .filter(s => s !== "").join(" · ")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                // A strip of the colors, wrapping for big palettes.
                Flow {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: entry.modelData.colors
                        delegate: Rectangle {
                            required property color modelData
                            width: 8
                            height: 8
                            color: modelData
                        }
                    }
                }
            }

            // Save as a preset and open on Lospec (shown on hover).
            Row {
                visible: area.containsMouse || saveButton.hovered || linkButton.hovered
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 2
                spacing: 0
                IconButton {
                    id: saveButton
                    implicitWidth: 22
                    implicitHeight: 22
                    iconName: entry.saved ? "check" : "save"
                    tip: entry.saved ? "Saved as a preset" : "Save as a preset"
                    enabled: !entry.saved
                    onClicked: PaletteLibrary.savePreset(entry.modelData.name, entry.modelData.colors)
                }
                IconButton {
                    id: linkButton
                    implicitWidth: 22
                    implicitHeight: 22
                    iconName: "external-link"
                    tip: "Open on Lospec"
                    onClicked: Qt.openUrlExternally(LospecClient.pageUrl(entry.modelData.slug))
                }
            }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: list.count === 0 || LospecClient.error !== ""
            text: LospecClient.loading ? "Loading…"
                : LospecClient.error !== "" ? LospecClient.error
                : "No palettes found"
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
        }
    }

    RowLayout {
        spacing: 2
        IconButton {
            iconName: "chevron-left"
            tip: "Previous page"
            enabled: LospecClient.page > 0 && !LospecClient.loading && LospecClient.pageCount > 1
            onClicked: root.browse(LospecClient.page - 1)
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: (LospecClient.offline ? "Offline, saved results \u00b7 " : "")
                  + (LospecClient.pageCount > 1 ? "Page " + (LospecClient.page + 1) + " of " + LospecClient.pageCount
                                                : "")
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }
        AppButton {
            visible: LospecClient.error !== "" && !LospecClient.loading
            text: "Retry"
            onClicked: LospecClient.retry()
        }
        IconButton {
            iconName: "chevron-right"
            tip: "Next page"
            enabled: LospecClient.page + 1 < LospecClient.pageCount && !LospecClient.loading
            onClicked: root.browse(LospecClient.page + 1)
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.separator
    }

    RowLayout {
        spacing: 6
        AppTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "Palette name or Lospec link"
            onAccepted: if (text.trim() !== "") LospecClient.fetch(text)
        }
        AppButton {
            text: "Open"
            enabled: nameField.text.trim() !== ""
            onClicked: LospecClient.fetch(nameField.text)
        }
    }
}
