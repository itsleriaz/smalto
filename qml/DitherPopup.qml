pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Dither settings for the drawing tools: the pattern (anchored to the
// canvas), Ordered's density, and what the skipped pixels get.
Popup {
    id: root

    required property Editor editor

    readonly property var patterns: ["Checker", "Ordered", "Horizontal lines", "Vertical lines", "Diagonal lines"]
    // When it last closed: a click on its own button closes it by pressing
    // outside, and must not reopen it.
    property double closedAt: 0
    onClosed: closedAt = Date.now()

    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel {}

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: "Dither"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            Label { text: "Pattern" }
            ColumnLayout {
                spacing: 4
                Row {
                    spacing: 4
                    Repeater {
                        model: root.patterns.length
                        delegate: Rectangle {
                            id: tile
                            required property int index
                            readonly property bool current: index === root.editor.ditherPattern
                            width: 36
                            height: 36
                            radius: Theme.radiusSmall
                            color: tileArea.containsMouse ? Theme.hover : "transparent"
                            border.color: current ? Theme.accent : Theme.separator
                            border.width: current ? 2 : 1
                            ImagePreview {
                                anchors.fill: parent
                                anchors.margins: 6
                                image: root.editor.ditherPreview(tile.index, root.editor.ditherDensity, 8)
                                checkerSize: 64 // one dark square: the pattern reads as white on dark
                            }
                            MouseArea {
                                id: tileArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.editor.ditherPattern = tile.index
                            }
                        }
                    }
                }
                Text {
                    text: root.patterns[root.editor.ditherPattern] ?? ""
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            Label { text: "Density"; visible: root.editor.ditherPattern === 1 }
            NumberControl {
                visible: root.editor.ditherPattern === 1
                buttons: true
                value: root.editor.ditherDensity
                from: 1
                to: 15
                suffix: "/16"
                tip: "How many of every 16 pixels get painted"
                onMoved: v => root.editor.ditherDensity = v
            }

            Label { text: "Other pixels" }
            Segmented {
                model: ["Keep", "Secondary color"]
                currentIndex: root.editor.ditherOtherColor ? 1 : 0
                onActivated: i => root.editor.ditherOtherColor = i === 1
            }
        }
    }
}
