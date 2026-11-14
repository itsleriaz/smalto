pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Symmetry for the drawing tools: which axes mirror (combined, they make
// every copy they produce together) and where they cross, in half pixels.
// The handles at the canvas edges (SymmetryHandles) move the same point.
Popup {
    id: root

    required property Editor editor

    // When it last closed: a click on its own button closes it by pressing
    // outside, and must not reopen it.
    property double closedAt: 0
    onClosed: closedAt = Date.now()

    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel { solid: true }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    // An axis on / off, drawn as its line.
    component AxisTile: Rectangle {
        id: tile
        property real angle // of the line, degrees (0: vertical)
        property bool checked
        property string name
        signal toggled()

        width: 40
        height: 40
        radius: Theme.radiusSmall
        color: checked ? Theme.accentSoft : tileArea.containsMouse ? Theme.hover : "transparent"
        border.color: checked ? Theme.accent : Theme.separator
        border.width: 1
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: parent.height - 14
            radius: 1
            rotation: tile.angle
            color: tile.checked ? Theme.text : Theme.textDim
        }
        MouseArea {
            id: tileArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: tile.toggled()
        }
        AppToolTip {
            text: tile.name
            active: tileArea.containsMouse
        }
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: "Symmetry"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            Label { text: "Axes" }
            ColumnLayout {
                spacing: 4
                Row {
                    spacing: 4
                    AxisTile {
                        angle: 0
                        name: "Left – right"
                        checked: root.editor.symmetryHorizontal
                        onToggled: root.editor.symmetryHorizontal = !checked
                    }
                    AxisTile {
                        angle: 90
                        name: "Top – bottom"
                        checked: root.editor.symmetryVertical
                        onToggled: root.editor.symmetryVertical = !checked
                    }
                    AxisTile {
                        angle: 45
                        name: "Diagonal 45°"
                        checked: root.editor.symmetryRising
                        onToggled: root.editor.symmetryRising = !checked
                    }
                    AxisTile {
                        angle: -45
                        name: "Diagonal −45°"
                        checked: root.editor.symmetryFalling
                        onToggled: root.editor.symmetryFalling = !checked
                    }
                }
                Text {
                    // Fixed text: a caption that changed with the hovered tile resized the
                    // popup, which re-centers under its button and moved the tile away
                    // from the pointer, over and over (flicker).
                    text: "Combined axes mirror every way they make together"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            Label { text: "Radial" }
            RowLayout {
                spacing: 8
                NumberControl {
                    value: root.editor.symmetryRadial
                    from: 0
                    to: 12
                    bigStep: 2
                    zeroText: "Off"
                    buttons: true
                    tip: "Copies turned round the crossing point"
                    // 1 copy is no symmetry: stepping up from off lands on 2, down from 2 on off.
                    onMoved: v => root.editor.symmetryRadial = v === 1 ? (root.editor.symmetryRadial >= 2 ? 0 : 2) : v
                }
                OptionPill {
                    text: "Mirror"
                    tip: "Also mirror each turned copy left to right"
                    enabled: root.editor.symmetryRadial >= 2
                    checked: root.editor.symmetryRadialMirror
                    onToggled: root.editor.symmetryRadialMirror = !root.editor.symmetryRadialMirror
                }
                OptionPill {
                    text: "Turn brush"
                    tip: "Turn square and custom brushes with each copy, instead of keeping their own orientation"
                    enabled: root.editor.symmetryRadial >= 2
                    checked: root.editor.symmetryRadialTurn
                    onToggled: root.editor.symmetryRadialTurn = !root.editor.symmetryRadialTurn
                }
            }

            Label { text: "Center" }
            RowLayout {
                spacing: 4
                NumberControl {
                    divisor: 2
                    value: Math.round(root.editor.symmetryCenter.x * 2)
                    from: 0
                    to: root.editor.canvasWidth * 2
                    bigStep: 16
                    tip: "Where the axes cross, left to right (half pixels)"
                    onMoved: v => root.editor.symmetryCenter = Qt.point(v / 2, root.editor.symmetryCenter.y)
                }
                Text { text: ","; color: Theme.textDim; font.pixelSize: Theme.fontSize }
                NumberControl {
                    divisor: 2
                    value: Math.round(root.editor.symmetryCenter.y * 2)
                    from: 0
                    to: root.editor.canvasHeight * 2
                    bigStep: 16
                    tip: "Where the axes cross, top to bottom (half pixels)"
                    onMoved: v => root.editor.symmetryCenter = Qt.point(root.editor.symmetryCenter.x, v / 2)
                }
                AppButton {
                    text: "Center"
                    implicitWidth: contentItem.implicitWidth + 20
                    implicitHeight: 28
                    tip: "Put the axes back through the middle of the canvas"
                    onClicked: root.editor.centerSymmetry()
                }
            }
        }

        Text {
            text: "Or drag the handles at the canvas edges"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
