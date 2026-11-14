pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// New sprite: size (presets or custom), color mode, background, frames.
// Choices are kept between runs (Main.qml). Emits `accepted` with the options
// for Editor.newSprite; the window confirms unsaved changes first.
Popup {
    id: root

    required property Editor editor

    signal accepted(var options)

    readonly property var presets: [
        { w: 16, h: 16 }, { w: 32, h: 32 }, { w: 48, h: 48 }, { w: 64, h: 64 },
        { w: 128, h: 128 }, { w: 256, h: 256 }, { w: 320, h: 180 }, { w: 640, h: 360 },
    ]
    readonly property var colorModes: ["rgba", "grayscale", "indexed"]
    readonly property var backgrounds: ["transparent", "white", "black", "color"]
    // Sizes saved by the user, as "48x64;100x100" (kept between runs by Main.qml), after the built-in ones.
    property string userPresets: ""
    property color backgroundColor: "#8a8a8a"
    readonly property var userPresetList: userPresets === "" ? []
        : userPresets.split(";").map(s => { const p = s.split("x"); return { w: parseInt(p[0]), h: parseInt(p[1]), user: true } })
                      .filter(p => p.w > 0 && p.h > 0)
    readonly property var allPresets: presets.concat(userPresetList)
    readonly property bool sizeIsPreset: allPresets.some(p => p.w === spriteWidth && p.h === spriteHeight)
    function savePreset() {
        if (!valid || sizeIsPreset)
            return
        userPresets = (userPresets === "" ? "" : userPresets + ";") + spriteWidth + "x" + spriteHeight
    }
    function removePreset(w: int, h: int) {
        userPresets = userPresetList.filter(p => p.w !== w || p.h !== h).map(p => p.w + "x" + p.h).join(";")
    }

    property int spriteWidth: 64
    property int spriteHeight: 64
    property int frames: 1
    // Remembered between runs by Main.qml (with the size and frames).
    property alias modeIndex: modeChoice.currentIndex
    property alias backgroundIndex: backgroundChoice.currentIndex
    readonly property bool valid: spriteWidth >= 1 && spriteWidth <= 4096 && spriteHeight >= 1 && spriteHeight <= 4096

    // With an image on the clipboard the dialog starts at its size, as Aseprite does; the
    // remembered size comes back when the dialog is closed without creating anything.
    property int restoreWidth: 0
    property int restoreHeight: 0
    property bool created: false

    function create() {
        if (!valid)
            return
        created = true
        accepted({
            width: spriteWidth,
            height: spriteHeight,
            colorMode: colorModes[modeChoice.currentIndex],
            background: backgrounds[backgroundChoice.currentIndex],
            backgroundColor: backgroundColor,
            frames: frames
        })
        close()
    }

    onAboutToShow: {
        created = false
        restoreWidth = spriteWidth
        restoreHeight = spriteHeight
        const clip = editor.clipboardImageSize()
        if (clip.width > 0 && clip.height > 0) {
            spriteWidth = clip.width
            spriteHeight = clip.height
        }
    }
    onClosed: {
        if (!created && restoreWidth > 0) {
            spriteWidth = restoreWidth
            spriteHeight = restoreHeight
        }
    }
    onOpened: {
        widthField.text = spriteWidth
        heightField.text = spriteHeight
        widthField.forceActiveFocus()
        widthField.selectAll()
    }

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel {}
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    component NumberField: AppTextField {
        signal edited(int value)
        implicitWidth: 72
        horizontalAlignment: Text.AlignHCenter
        font.features: { "tnum": 1 }
        selectAllOnFocus: true
        validator: IntValidator { bottom: 1; top: 4096 }
        onTextEdited: if (acceptableInput) edited(parseInt(text))
        onAccepted: root.create()
    }

    contentItem: ColumnLayout {
        spacing: 16

        Text {
            text: qsTr("New Sprite")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 12

            Label { text: qsTr("Presets"); Layout.alignment: Qt.AlignRight | Qt.AlignTop; Layout.topMargin: 6 }
            Flow {
                Layout.preferredWidth: 360
                spacing: 4
                Repeater {
                    model: root.allPresets
                    delegate: Rectangle {
                        id: preset
                        required property var modelData
                        readonly property bool current: modelData.w === root.spriteWidth && modelData.h === root.spriteHeight
                        width: presetLabel.implicitWidth + 18
                        height: 28
                        radius: Theme.radiusSmall
                        color: current ? Theme.accentSoft : presetArea.containsMouse ? Theme.pressed : Theme.hover
                        Text {
                            id: presetLabel
                            anchors.centerIn: parent
                            text: preset.modelData.w + " × " + preset.modelData.h
                            color: preset.current ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSize
                            font.features: { "tnum": 1 }
                        }
                        MouseArea {
                            id: presetArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: mouse => {
                                if (mouse.button === Qt.RightButton) { // your own sizes can go
                                    if (preset.modelData.user === true)
                                        root.removePreset(preset.modelData.w, preset.modelData.h)
                                    return
                                }
                                root.spriteWidth = preset.modelData.w
                                root.spriteHeight = preset.modelData.h
                                widthField.text = root.spriteWidth
                                heightField.text = root.spriteHeight
                            }
                            onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) root.create() }
                        }
                        // A saved size shows a faint dot; right-click removes it.
                        Rectangle {
                            visible: preset.modelData.user === true
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 4
                            width: 4
                            height: 4
                            radius: 2
                            color: Theme.accent
                        }
                    }
                }
            }

            Label { text: qsTr("Size") }
            RowLayout {
                spacing: 6
                NumberField {
                    id: widthField
                    onEdited: v => root.spriteWidth = v
                }
                Text {
                    text: "×"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize
                }
                NumberField {
                    id: heightField
                    onEdited: v => root.spriteHeight = v
                }
                IconButton {
                    iconName: "arrow-left-right"
                    tip: qsTr("Swap width and height")
                    onClicked: {
                        const w = root.spriteWidth
                        root.spriteWidth = root.spriteHeight
                        root.spriteHeight = w
                        widthField.text = root.spriteWidth
                        heightField.text = root.spriteHeight
                    }
                }
                IconButton {
                    iconName: "plus"
                    tip: root.sizeIsPreset ? qsTr("This size is already a preset")
                                           : qsTr("Save this size as a preset (right-click a saved one to remove it)")
                    enabled: root.valid && !root.sizeIsPreset
                    onClicked: root.savePreset()
                }
                Text {
                    text: qsTr("px")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            Label { text: qsTr("Color mode") }
            Segmented {
                id: modeChoice
                model: [qsTr("RGBA"), qsTr("Grayscale"), qsTr("Indexed")]
            }

            Label { text: qsTr("Background") }
            Segmented {
                id: backgroundChoice
                model: [qsTr("Transparent"), qsTr("White"), qsTr("Black"), qsTr("Color")]
            }
            Item {
                implicitWidth: 1
                implicitHeight: 1
                visible: backgroundChoice.currentIndex === 3
            }
            ColorField {
                visible: backgroundChoice.currentIndex === 3
                value: root.backgroundColor
                onEdited: c => root.backgroundColor = c
            }

            Label { text: qsTr("Frames") }
            NumberControl {
                buttons: true
                value: root.frames
                from: 1
                to: 999
                fieldWidth: 44
                onMoved: v => root.frames = v
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Create")
                primary: true
                enabled: root.valid
                onClicked: root.create()
            }
        }
    }
}
