pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Palette tools: ramp (also inserting colors between two), sort, clean up and reduce, on the shade range (Shift-click
// swatches) or the whole palette. Each is one undo step. A floating window
// that stays open while you pick ranges; Insert and Reduce show the colors
// they would make before you apply them.
FloatingDialog {
    id: root

    required property Editor editor

    // How many entries the tools work on (re-read when the palette or range changes).
    readonly property int entries: {
        void editor.palette
        void editor.shadeRange
        void editor.colorMode
        return editor.paletteToolEntries()
    }
    readonly property bool ranged: editor.shadeRange.x >= 0
    property int reduceTo: 8
    property int insertCount: 3
    // Ramps turn the hue (dark purple to light yellow via orange) instead of blending straight.
    property bool hueArc: false

    onOpened: reduceTo = Math.max(1, Math.min(reduceTo, entries - 1))
    onClosed: showReduce = false

    // Indexed sprites: Reduce shown on the canvas as the number is set.
    property bool showReduce: false
    readonly property bool indexed: editor.colorMode === "indexed"
    function updateReduce() {
        if (showReduce && visible && indexed && entries > reduceTo)
            editor.previewReducePixels(reduceTo)
        else if (editor.effectActive)
            editor.cancelColorModePreview()
    }
    onShowReduceChanged: updateReduce()
    onReduceToChanged: Qt.callLater(updateReduce)
    onEntriesChanged: Qt.callLater(updateReduce)
    // Any other edit (or undo) ended the preview: the switch goes off.
    Connections {
        target: root.editor
        function onEffectChanged() {
            if (root.showReduce && !root.editor.effectActive)
                root.showReduce = false
        }
    }

    padding: 14
    dragHeight: title.height + 6

    // What Insert and Reduce would make (re-read as the palette, the range
    // or the numbers change).
    readonly property var insertColors: {
        void entries
        void insertCount
        void hueArc
        void editor.palette
        void editor.shadeRange
        return visible ? editor.previewInsert(insertCount, hueArc) : []
    }
    readonly property var reduceColors: {
        void entries
        void reduceTo
        void editor.palette
        void editor.shadeRange
        return visible ? editor.previewReduce(reduceTo) : []
    }
    // A row of small swatches.
    component Strip: Flow {
        id: strip
        property var colors: []
        spacing: 2
        Repeater {
            model: strip.colors
            Rectangle {
                required property var modelData
                width: 12
                height: 12
                radius: 2
                color: modelData
                border.color: Theme.swatchBorder
                border.width: 1
            }
        }
    }

    component Caption: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSizeSmall
    }
    // Compact action button, sized to its text.
    component ToolButton: AppButton {
        implicitWidth: contentItem.implicitWidth + 20
        implicitHeight: 28
        enabled: root.entries >= 2
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                id: title
                Layout.fillWidth: true
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                text: "Palette tools"
            }
            IconButton {
                implicitWidth: 24
                implicitHeight: 24
                iconName: "x"
                tip: "Close"
                onClicked: root.close()
            }
        }
        Caption {
            text: root.ranged ? "Colors \u00b7 colors " + (root.editor.shadeRange.x + 1) + "\u2013"
                                + (root.editor.shadeRange.y + 1)
                              : "Whole palette"
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6

            Caption { text: "Ramp" }
            RowLayout {
                spacing: 2
                ToolButton {
                    text: "Make ramp"
                    enabled: root.entries >= 3
                    tip: root.entries >= 3 ? (root.hueArc ? "Turn the hue evenly from the first color to the last"
                                                          : "Blend evenly from the first color to the last")
                                           : "Shift-click swatches to pick 3 or more colors"
                    onClicked: root.editor.rampPalette(root.hueArc)
                }
                ToolButton {
                    text: "Insert"
                    enabled: root.entries >= 2 && root.ranged
                    tip: enabled ? "Add the new colors, a ramp from the first picked color to the last, before the last one"
                                 : "Shift-click a second swatch to pick the colors to add between"
                    onClicked: root.editor.insertRamp(root.insertCount, root.hueArc)
                }
                NumberControl {
                    buttons: true
                    value: root.insertCount
                    from: 1
                    to: 32
                    suffix: " new"
                    fieldWidth: 70
                    tip: "How many colors Insert adds"
                    onMoved: v => root.insertCount = v
                }
            }
            Item { implicitWidth: 1; implicitHeight: 1; visible: root.insertColors.length > 0 }
            Strip {
                visible: root.insertColors.length > 0
                Layout.maximumWidth: 250
                colors: root.insertColors
            }

            Caption { text: "Hue" }
            Segmented {
                model: ["Straight", "Hue shift"]
                currentIndex: root.hueArc ? 1 : 0
                onActivated: i => root.hueArc = i === 1
            }

            Caption { text: "Sort" }
            RowLayout {
                spacing: 2
                ToolButton { text: "Hue"; onClicked: root.editor.sortPalette("hue") }
                ToolButton { text: "Saturation"; onClicked: root.editor.sortPalette("saturation") }
                ToolButton { text: "Lightness"; onClicked: root.editor.sortPalette("lightness") }
                ToolButton { text: "Reverse"; onClicked: root.editor.reversePalette() }
            }

            Caption { text: "Clean up" }
            RowLayout {
                spacing: 2
                ToolButton {
                    readonly property int count: { void root.entries; return root.editor.removableColors(true, false) }
                    text: "Unused"
                    enabled: count > 0
                    tip: count > 0 ? "Remove " + count + (count === 1 ? " color" : " colors") + " no pixel uses"
                                   : "Every color is used"
                    onClicked: root.editor.removeUnusedColors(true, false)
                }
                ToolButton {
                    readonly property int count: { void root.entries; return root.editor.removableColors(false, true) }
                    text: "Duplicates"
                    enabled: count > 0
                    tip: count > 0 ? "Merge " + count + (count === 1 ? " color" : " colors") + " into an identical one"
                                   : "No two colors are the same"
                    onClicked: root.editor.removeUnusedColors(false, true)
                }
            }

            Caption { text: "Reduce" }
            RowLayout {
                spacing: 6
                NumberControl {
                    buttons: true
                    enabled: root.entries >= 2
                    value: root.reduceTo
                    from: 1
                    to: Math.max(1, root.entries - 1)
                    suffix: root.reduceTo === 1 ? " color" : " colors"
                    fieldWidth: 80
                    tip: "How many colors to keep; the closest ones merge first"
                    onMoved: v => root.reduceTo = v
                }
                ToolButton {
                    text: "Reduce"
                    enabled: root.entries > root.reduceTo
                    onClicked: {
                        root.showReduce = false
                        root.editor.reducePalette(root.reduceTo)
                    }
                }
                OptionPill {
                    visible: root.indexed
                    text: "On canvas"
                    tip: "Show the reduced sprite on the canvas while you choose (the canvas can't be drawn on meanwhile)"
                    checked: root.showReduce
                    onToggled: root.showReduce = !root.showReduce
                }
            }
            Item { implicitWidth: 1; implicitHeight: 1; visible: root.reduceColors.length > 0 }
            Strip {
                visible: root.reduceColors.length > 0
                Layout.maximumWidth: 250
                colors: root.reduceColors
            }
        }
    }
}
