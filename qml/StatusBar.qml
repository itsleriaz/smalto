pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// A thin bar docked along the window's bottom edge: about the sprite on the
// left (size, color mode, the pixel under the pointer), how it's viewed on the
// right (zoom, grids, tiled view). Menus and popups open upward.
Panel {
    id: root

    required property Editor editor
    required property CanvasView canvas

    signal spriteSizeRequested()
    signal colorModeRequested()

    // For screenshots (`--show popup:grid`).
    function showGrid() { gridPopup.open() }

    docked: true
    dockEdge: Qt.TopEdge
    padding: 0
    implicitHeight: 30

    // Text that does something when clicked.
    component TextAction: Item {
        id: action
        property alias text: label.text
        property string tip
        signal clicked()

        Layout.preferredWidth: label.implicitWidth + 12
        Layout.preferredHeight: 22

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall - 2
            color: area.pressed ? Theme.pressed : area.containsMouse ? Theme.hover : "transparent"
        }
        Text {
            id: label
            anchors.centerIn: parent
            color: area.containsMouse ? Theme.text : Theme.textDim
            font.pixelSize: Theme.fontSizeSmall + 1
            font.features: { "tnum": 1 }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: action.clicked()
        }
        AppToolTip {
            text: action.tip
            active: area.containsMouse && action.tip !== ""
        }
    }

    component SmallButton: IconButton {
        implicitWidth: 26
        implicitHeight: 24
        icon.width: 15
        icon.height: 15
    }

    component Separator: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 14
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        color: Theme.separator
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.margin - 4
        anchors.rightMargin: Theme.margin - 4
        anchors.topMargin: 1 // under the hairline
        spacing: 2

        TextAction {
            text: root.editor.canvasWidth + " × " + root.editor.canvasHeight
            tip: "Sprite size" + Shortcuts.hints.spriteSize
            onClicked: root.spriteSizeRequested()
        }
        TextAction {
            text: root.editor.colorMode === "indexed" ? "Indexed"
                  : root.editor.colorMode === "grayscale" ? "Grayscale" : "RGBA"
            tip: "Color mode"
            onClicked: root.colorModeRequested()
        }
        Separator {}
        Text {
            // Keeps its room, so the right side doesn't shift as it appears.
            Layout.preferredWidth: 90
            Layout.leftMargin: 4
            text: root.canvas.hovering ? root.canvas.hoverPixel.x + ", " + root.canvas.hoverPixel.y : ""
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }

        Item { Layout.fillWidth: true }

        SmallButton {
            iconName: "zoom-out"
            tip: "Zoom out" + Shortcuts.hints.zoomOut
            onClicked: root.canvas.zoomOut()
        }
        NumberControl {
            value: Math.round(root.canvas.zoom * 100)
            from: 25
            to: 12800
            factor: 0.1
            suffix: "%"
            tip: "Zoom"
            onMoved: v => root.canvas.zoomTo(v / 100)
        }
        SmallButton {
            iconName: "zoom-in"
            tip: "Zoom in" + Shortcuts.hints.zoomIn
            onClicked: root.canvas.zoomIn()
        }
        SmallButton {
            iconName: "scan"
            tip: "Fit to view" + Shortcuts.hints.fitToView
            onClicked: root.canvas.fitToView()
        }

        Separator {}

        SmallButton {
            iconName: "grid-3x3"
            tip: "Pixel grid: a line between every pixel" + Shortcuts.hints.pixelGrid
            active: root.canvas.showGrid
            onClicked: root.canvas.showGrid = !root.canvas.showGrid
        }
        SmallButton {
            id: gridButton
            iconName: "ruler"
            tip: "Custom grid settings" + (Shortcuts.labels.customGrid ? " (" + Shortcuts.labels.customGrid + " shows the grid)" : "")
            active: root.canvas.showCustomGrid || gridPopup.visible
            onClicked: if (!gridPopup.visible && Date.now() - gridPopup.closedAt > 250) gridPopup.open()

            // Opens above the button, kept inside the window (the button's
            // window position is only known when it opens).
            GridPopup {
                id: gridPopup
                editor: root.editor
                canvas: root.canvas
                y: -height - 8
                onAboutToShow: {
                    const left = gridButton.mapToItem(null, 0, 0).x
                    const want = Math.min(Math.max(8, left + gridButton.width / 2 - width / 2),
                                          Overlay.overlay.width - width - 8)
                    x = want - left
                }
            }
        }
        SmallButton {
            iconName: "layout-grid"
            tip: "Tiled view: repeat the sprite to check seams" + Shortcuts.hints.tiledView
            active: root.editor.tileMode !== 0 || tileMenu.visible
            onClicked: tileMenu.open()

            AppMenu {
                id: tileMenu
                y: -height - 8
                // The current mode is ticked.
                AppMenuItem {
                    text: "Off"
                    hint: root.editor.tileMode === 0 ? "✓" : ""
                    onTriggered: root.editor.tileMode = 0
                }
                AppMenuItem {
                    text: "Across X"
                    hint: root.editor.tileMode === 1 ? "✓" : ""
                    onTriggered: root.editor.tileMode = 1
                }
                AppMenuItem {
                    text: "Across Y"
                    hint: root.editor.tileMode === 2 ? "✓" : ""
                    onTriggered: root.editor.tileMode = 2
                }
                AppMenuItem {
                    text: "Both"
                    hint: root.editor.tileMode === 3 ? "✓" : ""
                    onTriggered: root.editor.tileMode = 3
                }
            }
        }
    }
}
