pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Pen (tablet) dynamics and the stroke stabilizer for the freehand tools: what the pressure changes,
// its curve, the tilt, and how long the stabilizer's string is (that one is for the mouse too).
Popup {
    id: root

    required property Editor editor
    required property CanvasView canvas

    // When it last closed: a click on its own button closes it by pressing
    // outside, and must not reopen it.
    property double closedAt: 0
    onClosed: closedAt = Date.now()

    readonly property bool inkShown: editor.pressureOpacity || editor.pressureColor
    readonly property var curve: editor.pressureCurve

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
    component Heading: Text {
        color: Theme.text
        font.pixelSize: 15
        font.weight: Font.DemiBold
    }

    contentItem: ColumnLayout {
        spacing: 14

        Heading { text: "Pen" }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            // The pen's pressure right now: shows the tablet reaches the app at all.
            Label { text: "Pressure now" }
            Item {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 16
                Rectangle {
                    visible: root.canvas.penPressure >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 6
                    radius: 3
                    color: Theme.controlTrack
                    Rectangle {
                        width: parent.width * Math.max(0, root.canvas.penPressure)
                        height: parent.height
                        radius: 3
                        color: Theme.accent
                    }
                }
                Text {
                    visible: root.canvas.penPressure < 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: "No pen seen yet"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            Label { text: "Pressure" }
            Flow {
                Layout.preferredWidth: 220
                spacing: 4
                OptionPill {
                    text: "Size"
                    tip: "Press lighter for a smaller brush (built-in brushes; a custom brush keeps its size)"
                    checked: root.editor.pressureSize
                    onToggled: root.editor.pressureSize = !root.editor.pressureSize
                }
                OptionPill {
                    text: "Opacity"
                    tip: "Press lighter to paint (or erase) more faintly; a pixel keeps the firmest press of the stroke"
                    checked: root.editor.pressureOpacity
                    onToggled: root.editor.pressureOpacity = !root.editor.pressureOpacity
                }
                OptionPill {
                    text: "Color"
                    tip: "Pencil: light pressure paints the secondary color, firm the primary (or a custom brush's own colors), and the colors between (the right button swaps them)"
                    checked: root.editor.pressureColor
                    onToggled: root.editor.pressureColor = !root.editor.pressureColor
                }
            }

            Label { text: "Smallest"; visible: root.editor.pressureSize }
            NumberControl {
                visible: root.editor.pressureSize
                buttons: true
                value: root.editor.pressureMinSize
                from: 0
                to: 100
                step: 5
                suffix: "%"
                tip: "The brush size at the lightest press, as a part of the full size (never under 1 px)"
                onMoved: v => root.editor.pressureMinSize = v
            }

            Label { text: "Opacity / color"; visible: root.inkShown }
            AppComboBox {
                visible: root.inkShown
                implicitWidth: 130
                model: ["Smooth", "Dither 2 × 2", "Dither 4 × 4", "Dither 8 × 8", "Blue noise"]
                currentIndex: root.editor.pressureDither
                onActivated: index => root.editor.pressureDither = index
            }

            Label { text: "Curve"; Layout.alignment: Qt.AlignRight | Qt.AlignTop; Layout.topMargin: 4 }
            ColumnLayout {
                spacing: 4
                EasingCurveEditor {
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 112
                    x1: root.curve[0] ?? 1 / 3
                    y1: root.curve[1] ?? 1 / 3
                    x2: root.curve[2] ?? 2 / 3
                    y2: root.curve[3] ?? 2 / 3
                    onEdited: (x1, y1, x2, y2) => root.editor.pressureCurve = [x1, y1, x2, y2]
                }
                RowLayout {
                    spacing: 4
                    Text {
                        text: "Pressure →, effect ↑"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.fillWidth: true
                    }
                    OptionPill {
                        indicator: false
                        text: "Reset"
                        tip: "Back to a straight line"
                        onToggled: root.editor.pressureCurve = [1 / 3, 1 / 3, 2 / 3, 2 / 3]
                    }
                }
            }

            Label { text: "Tilt" }
            OptionPill {
                text: "Turns the brush"
                tip: "The brush (and its preview while the pen hovers) turns the way the pen leans (0° leaning right), on top of the brush angle. Square and custom brushes"
                checked: root.editor.tiltAngle
                onToggled: root.editor.tiltAngle = !root.editor.tiltAngle
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separator }

        Heading { text: "Stabilizer" }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            Label { text: "Strength" }
            NumberControl {
                buttons: true
                value: root.canvas.stabilizer
                from: 0
                to: 100
                step: 5
                suffix: " px"
                zeroText: "Off"
                tip: "The brush trails the pointer on a string this long (on screen): shaky moves within it don't paint, and curves come out smooth. Mouse and pen"
                onMoved: v => root.canvas.stabilizer = v
            }
        }
    }
}
