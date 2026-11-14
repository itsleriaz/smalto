pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Smalto

// A color picker: a saturation / value square, a hue slider and the red, green, blue and alpha
// as numbers. It shows `current`; every change comes out as `edited(color)`, and the owner
// writes it back (the primary color, an effect's color, ...). The hue is kept apart so it
// survives when the saturation or value reach zero.
ColumnLayout {
    id: root

    property color current: "black"
    property int squareHeight: 150
    signal edited(color c)

    spacing: 10

    property real hue: 0
    property real saturation: 0
    property real brightness: 1
    property bool editing: false

    function syncFromColor() {
        if (editing)
            return
        const c = current
        if (c.hsvSaturation > 0 && c.hsvValue > 0)
            hue = Math.max(0, c.hsvHue)
        if (c.hsvValue > 0)
            saturation = c.hsvSaturation
        brightness = c.hsvValue
    }
    function applyHsv() {
        editing = true
        edited(Qt.hsva(hue, saturation, brightness, current.a))
        editing = false
    }
    function setChannel(channel, v) {
        const c = current
        const n = v / 255
        edited(Qt.rgba(channel === 0 ? n : c.r, channel === 1 ? n : c.g, channel === 2 ? n : c.b,
                       channel === 3 ? n : c.a))
    }

    onCurrentChanged: syncFromColor()
    Component.onCompleted: syncFromColor()

    // Saturation / value square.
    Rectangle {
        id: svSquare
        Layout.fillWidth: true
        Layout.preferredHeight: root.squareHeight
        radius: Theme.radiusSmall
        clip: true
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "white" }
            GradientStop { position: 1; color: Qt.hsva(root.hue, 1, 1, 1) }
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: "black" }
            }
        }

        Rectangle {
            x: root.saturation * parent.width - width / 2
            y: (1 - root.brightness) * parent.height - height / 2
            width: 14; height: 14
            radius: 7
            color: "transparent"
            border.color: root.brightness > 0.5 && root.saturation < 0.5 ? "black" : "white"
            border.width: 2
        }

        MouseArea {
            anchors.fill: parent
            preventStealing: true
            function pick(mouse) {
                root.saturation = Math.min(1, Math.max(0, mouse.x / width))
                root.brightness = 1 - Math.min(1, Math.max(0, mouse.y / height))
                root.applyHsv()
            }
            onPressed: mouse => pick(mouse)
            onPositionChanged: mouse => pick(mouse)
        }
    }

    // Hue slider.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 12
        radius: 6
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0 / 6; color: "#ff0000" }
            GradientStop { position: 1 / 6; color: "#ffff00" }
            GradientStop { position: 2 / 6; color: "#00ff00" }
            GradientStop { position: 3 / 6; color: "#00ffff" }
            GradientStop { position: 4 / 6; color: "#0000ff" }
            GradientStop { position: 5 / 6; color: "#ff00ff" }
            GradientStop { position: 6 / 6; color: "#ff0000" }
        }

        Rectangle {
            x: root.hue * parent.width - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16
            radius: 8
            color: Qt.hsva(root.hue, 1, 1, 1)
            border.color: "white"
            border.width: 2
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            preventStealing: true
            function pick(mouse) {
                root.hue = Math.min(0.9999, Math.max(0, (mouse.x - 6) / (width - 12)))
                root.applyHsv()
            }
            onPressed: mouse => pick(mouse)
            onPositionChanged: mouse => pick(mouse)
        }
    }

    // Exact channels.
    RowLayout {
        Layout.fillWidth: true
        spacing: 2
        Repeater {
            model: ["R", "G", "B", "A"]
            RowLayout {
                required property string modelData
                required property int index
                spacing: 0
                Text {
                    text: parent.modelData
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    Layout.preferredWidth: 10
                }
                NumberControl {
                    readonly property color c: root.current
                    value: Math.round([c.r, c.g, c.b, c.a][parent.index] * 255)
                    from: 0
                    to: 255
                    bigStep: 16
                    fieldWidth: 40
                    onMoved: v => root.setChannel(parent.index, v)
                }
            }
        }
    }
}
