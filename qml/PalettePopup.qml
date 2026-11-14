pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Palette presets and files: pick a preset (built-in or your own) to replace
// the palette, remove your own, save the current palette as a preset, or ask
// the window to load / save a palette file.
// The Lospec tab browses palettes on lospec.com (LospecBrowser).
Popup {
    id: root

    required property Editor editor

    signal loadFileRequested()
    signal saveFileRequested()
    // Picking: a click hands over the palette instead of loading it (the
    // color mode dialog's "Choose"); no saving, files or previews then.
    // A click only selects (the owner shows it); Done hands it over, Cancel /
    // Esc gives it up. The window is dragged by its title, so it can sit
    // where it hides the least.
    property bool picking: false
    property string selectedName: ""
    property var selectedColors: []
    signal selected(var colors, string name)
    signal picked(var colors, string name)
    signal cancelled()
    property bool done: false
    function select(colors: var, name: string) {
        selectedColors = colors
        selectedName = name
        selected(colors, name)
    }

    property bool naming: false // the "save as preset" field is showing
    property int tab: 0         // 0 presets, 1 Lospec
    readonly property string presetName: nameField.text.trim()

    // The sprite in the palette being pointed at (an Image source), if any.
    property string previewSource: ""
    function showPreview(colors: var) {
        previewSource = colors.length > 0 && !picking && editor.colorMode === "indexed" ? editor.palettePreview(colors, 96) : ""
    }
    onClosed: {
        previewSource = ""
        if (picking && !done)
            cancelled()
    }

    // The preset being renamed (its file); empty when saving the palette as a new one.
    property string renaming: ""

    function savePreset() {
        if (presetName === "")
            return
        if (renaming !== "") {
            if (PaletteLibrary.renamePreset(renaming, presetName))
                naming = false
            return
        }
        if (editor.savePalette(PaletteLibrary.presetPath(presetName), presetName))
            PaletteLibrary.reload()
        naming = false
    }

    onAboutToShow: {
        done = false
        selectedColors = []
        selectedName = ""
    }
    onOpened: {
        if (tab === 1)
            lospec.start()
        naming = false
        renaming = ""
        PaletteLibrary.reload() // pick up files added outside the app
    }

    width: tab === 1 ? 380 : 300
    padding: 10
    // Picking stays open while you look at the canvas and the dialog.
    focus: true
    closePolicy: picking ? Popup.CloseOnEscape : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel {
        solid: true
        padding: 0
        // The top strip, behind the title: drag it to move the window.
        Item {
            visible: root.picking
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.topPadding + 30
            DragHandler {
                target: null
                property point start
                onActiveChanged: if (active) start = Qt.point(root.x, root.y)
                onTranslationChanged: if (active) {
                    root.x = start.x + translation.x
                    root.y = start.y + translation.y
                }
            }
            HoverHandler { cursorShape: Qt.SizeAllCursor }
        }
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            visible: root.picking
            text: qsTr("Choose a palette")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            Layout.preferredHeight: 22
        }

        Segmented {
            model: ["Presets", "Lospec"]
            currentIndex: root.tab
            onActivated: index => {
                root.tab = index
                if (index === 1)
                    lospec.start()
            }
        }

        LospecBrowser {
            id: lospec
            visible: root.tab === 1
            Layout.fillWidth: true
            editor: root.editor
            picking: root.picking
            selectedName: root.selectedName
            onPreviewColors: colors => root.showPreview(colors)
            onPicked: (colors, name) => root.select(colors, name)
        }

        ColumnLayout {
            visible: root.tab === 0
            Layout.fillWidth: true
            spacing: 8

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 340)
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                model: PaletteLibrary.presets
                ScrollBar.vertical: AppScrollBar {}

                delegate: Rectangle {
                    id: preset
                    required property var modelData
                    width: list.width
                    height: content.implicitHeight + 12
                    radius: Theme.radiusSmall
                    color: root.picking && preset.modelData.name === root.selectedName ? Theme.accentSoft
                         : area.pressed ? Theme.pressed : area.containsMouse ? Theme.hover : "transparent"

                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        onContainsMouseChanged: root.showPreview(containsMouse ? preset.modelData.colors : [])
                        onClicked: {
                            if (root.picking)
                                root.select(preset.modelData.colors, preset.modelData.name)
                            else if (root.editor.loadPalette(preset.modelData.path)) {
                                root.close()
                            }
                        }
                    }

                    ColumnLayout {
                        id: content
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 5

                        RowLayout {
                            spacing: 6
                            Text {
                                text: preset.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontSize
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: preset.modelData.colors.length
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSizeSmall
                                font.features: { "tnum": 1 }
                            }
                        }
                        // A strip of the colors, wrapping for big palettes.
                        Flow {
                            Layout.fillWidth: true
                            spacing: 0
                            Repeater {
                                model: preset.modelData.colors
                                delegate: Rectangle {
                                    required property color modelData
                                    width: 8
                                    height: 8
                                    color: modelData
                                }
                            }
                        }
                    }

                    // Own presets can be renamed and removed (shown on hover).
                    Row {
                        visible: !preset.modelData.builtin && (area.containsMouse || renameButton.hovered || removeButton.hovered)
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 2
                        IconButton {
                            id: renameButton
                            implicitWidth: 22
                            implicitHeight: 22
                            iconName: "pencil"
                            tip: "Rename this preset"
                            onClicked: {
                                root.renaming = preset.modelData.path
                                nameField.text = preset.modelData.name
                                root.naming = true
                                nameField.forceActiveFocus()
                                nameField.selectAll()
                            }
                        }
                        IconButton {
                            id: removeButton
                            implicitWidth: 22
                            implicitHeight: 22
                            iconName: "trash-2"
                            tip: "Remove this preset"
                            onClicked: PaletteLibrary.removePreset(preset.modelData.path)
                        }
                    }
                }
            }

            Rectangle {
                visible: !root.picking
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.separator
            }

            // Save as preset: a name field, then Save (Replace if the name exists).
            RowLayout {
                visible: root.naming && !root.picking
                spacing: 6
                AppTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Preset name"
                    onAccepted: root.savePreset()
                    Keys.onEscapePressed: root.naming = false
                }
                AppButton {
                    text: root.renaming !== "" ? "Rename" : PaletteLibrary.hasPreset(root.presetName) ? "Replace" : "Save"
                    primary: true
                    enabled: root.presetName !== ""
                    onClicked: root.savePreset()
                }
            }

            RowLayout {
                visible: !root.naming && !root.picking
                spacing: 6
                AppButton {
                    text: "Save as Preset…"
                    onClicked: {
                        nameField.text = ""
                        root.renaming = ""
                        root.naming = true
                        nameField.forceActiveFocus()
                    }
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    iconName: "folder-open"
                    tip: "Load a palette file (.gpl, .hex, .pal, .act, .aseprite, .png)"
                    onClicked: {
                        root.close()
                        root.loadFileRequested()
                    }
                }
                IconButton {
                    iconName: "save"
                    tip: "Save the palette as a file (.gpl, .hex, .pal, .act)"
                    onClicked: {
                        root.close()
                        root.saveFileRequested()
                    }
                }
            }
        }

        // Indexed sprites only: what loading does to them, and a look at it.
        ColumnLayout {
            visible: root.editor.colorMode === "indexed" && !root.picking
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.separator
            }
            RowLayout {
                spacing: 8
                Text {
                    text: "Pixels"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                }
                Segmented {
                    model: ["Keep indices", "Match colors"]
                    currentIndex: root.editor.paletteLoadMode
                    onActivated: i => root.editor.paletteLoadMode = i
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.editor.paletteLoadMode === 0
                      ? "Each pixel keeps its number, so it takes that number's new color."
                      : "Each pixel goes to the new color nearest to what it is now."
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 104
                radius: Theme.radiusSmall
                color: Theme.hover
                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: root.previewSource !== ""
                    source: root.previewSource
                    fillMode: Image.PreserveAspectFit
                    smooth: false
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.previewSource === ""
                    text: "Point at a palette to see your sprite in it"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        RowLayout {
            visible: root.picking
            Layout.alignment: Qt.AlignRight
            spacing: 8
            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Done")
                primary: true
                enabled: root.selectedColors.length > 0
                onClicked: {
                    root.done = true
                    root.picked(root.selectedColors, root.selectedName)
                    root.close()
                }
            }
        }
    }
}
