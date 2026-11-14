pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Saved custom brushes (BrushLibrary): click one to draw with it, × removes
// it; the button saves the current custom brush.
Popup {
    id: root

    required property Editor editor
    required property CanvasView canvas

    // Reads `brushes` so it updates when the library changes.
    readonly property bool currentSaved: BrushLibrary.brushes.length > 0 && editor.hasCustomBrush
                                         && BrushLibrary.contains(editor.customBrushImage)
    // When it last closed: a click on its own button closes it by pressing
    // outside, and must not reopen it.
    property double closedAt: 0
    onClosed: closedAt = Date.now()

    width: 268
    padding: 12
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel {}

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Brushes")
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            AppButton {
                text: root.currentSaved ? qsTr("Saved") : qsTr("Save current")
                enabled: root.editor.hasCustomBrush && !root.currentSaved
                tip: qsTr("Keep the current custom brush in the library")
                onClicked: BrushLibrary.add(root.editor.customBrushImage)
            }
        }

        Text {
            visible: BrushLibrary.brushes.length === 0
            Layout.fillWidth: true
            text: qsTr("No saved brushes yet. Select pixels, press %1 to make a brush, then save it here.")
                  .arg(Shortcuts.labels.brushFromSelection)
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }

        GridView {
            id: grid
            visible: BrushLibrary.brushes.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(4, Math.ceil(BrushLibrary.brushes.length / 5)) * cellHeight
            cellWidth: Math.floor(width / 5)
            cellHeight: 52
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: BrushLibrary.brushes
            ScrollBar.vertical: AppScrollBar {}

            delegate: Item {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: Theme.radiusSmall - 2
                    color: area.containsMouse ? Theme.hover : "transparent"
                    border.color: Theme.swatchBorder

                    ImagePreview {
                        anchors.fill: parent
                        anchors.margins: 5
                        image: BrushLibrary.image(cell.modelData.path)
                        checkerLight: root.canvas.checkerA
                        checkerDark: root.canvas.checkerB
                        checkerSize: 3
                    }
                    Text {
                        visible: area.containsMouse
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 2
                        text: cell.modelData.width + "×" + cell.modelData.height
                        color: Theme.textDim
                        font.pixelSize: 9
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.editor.setCustomBrush(BrushLibrary.image(cell.modelData.path))
                        root.close()
                    }
                }
                // Remove
                Rectangle {
                    visible: area.containsMouse || removeArea.containsMouse
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: 16
                    height: 16
                    radius: 8
                    color: removeArea.containsMouse ? Theme.danger : Theme.tooltip
                    border.color: Theme.panelBorder
                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: removeArea.containsMouse ? Theme.textOnAccent : Theme.textDim
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: removeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: BrushLibrary.remove(cell.modelData.path)
                    }
                }
            }
        }
    }
}
