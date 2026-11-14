pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Smalto

// Fields for canvas size mode, under the top bar in place of the tool
// options: width and height (placed by the anchor), the anchor itself, how
// much each side grows or shrinks, Cancel and Apply.
Panel {
    id: root

    required property CanvasSizeHandles sizer

    readonly property rect bounds: sizer.bounds
    readonly property int oldWidth: sizer.editor.canvasWidth
    readonly property int oldHeight: sizer.editor.canvasHeight

    function signed(v) { return v > 0 ? "+" + v : String(v) }

    implicitWidth: row.implicitWidth + padding * 2
    implicitHeight: Theme.buttonSize + padding * 2
    padding: 6

    opacity: sizer.active ? 1 : 0
    visible: opacity > 0
    transform: Translate { y: root.sizer.active ? 0 : -8; Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } } }
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }

    component Separator: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 18
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        color: Theme.separator
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 4

        IconButton {
            iconName: "scan"
            enabled: false
        }
        Label {
            text: "Canvas Size"
            color: Theme.text
            Layout.rightMargin: 6
        }

        NumberControl {
            value: root.bounds.width
            from: 1
            to: 4096
            suffix: " px"
            tip: "Width"
            onMoved: v => root.sizer.setSize(v, root.bounds.height)
        }
        Label { text: "×" }
        NumberControl {
            value: root.bounds.height
            from: 1
            to: 4096
            suffix: " px"
            tip: "Height"
            onMoved: v => root.sizer.setSize(root.bounds.width, v)
        }

        Separator {}

        // Anchor: which part of the old canvas stays put when the size is typed.
        Grid {
            columns: 3
            spacing: 2
            HoverHandler { id: anchorHover }
            AppToolTip {
                text: "Anchor: where the current picture stays when you type a size"
                active: anchorHover.hovered
            }
            Repeater {
                model: 9
                delegate: Rectangle {
                    id: cell
                    required property int index
                    readonly property real ax: (index % 3) / 2
                    readonly property real ay: Math.floor(index / 3) / 2
                    readonly property bool current: ax === root.sizer.anchorX && ay === root.sizer.anchorY
                    width: 8
                    height: 8
                    radius: 1
                    color: current ? Theme.accent : cellArea.containsMouse ? Theme.textDim : Theme.pressed
                    MouseArea {
                        id: cellArea
                        anchors.fill: parent
                        anchors.margins: -1
                        hoverEnabled: true
                        onClicked: root.sizer.setAnchor(cell.ax, cell.ay)
                    }
                }
            }
        }

        Separator {}

        // How much each side moves (negative = cropped).
        Label {
            text: {
                const b = root.bounds
                return "L " + root.signed(-b.x) + "  T " + root.signed(-b.y)
                    + "  R " + root.signed(b.x + b.width - root.oldWidth)
                    + "  B " + root.signed(b.y + b.height - root.oldHeight)
            }
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }

        IconButton {
            Layout.leftMargin: 6
            iconName: "x"
            tip: "Cancel (Esc)"
            onClicked: root.sizer.cancel()
        }
        IconButton {
            iconName: "check"
            active: true
            tip: "Apply (Enter)"
            onClicked: root.sizer.apply()
        }
    }
}
