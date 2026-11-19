pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// The document's grid: cell size and offset, saved in the file. Changes apply
// as you make them. (The checkerboard is in Preferences.)
Popup {
    id: root

    required property Editor editor
    required property CanvasView canvas

    readonly property rect grid: editor.grid
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

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: qsTr("Grid")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            Label { text: qsTr("Show") }
            Segmented {
                model: [qsTr("Off"), qsTr("On")]
                currentIndex: root.canvas.showCustomGrid ? 1 : 0
                onActivated: i => root.canvas.showCustomGrid = i === 1
            }

            Label { text: qsTr("Tools") }
            RowLayout {
                spacing: 6
                OptionPill {
                    text: qsTr("Snap to the grid")
                    tip: qsTr("Selections, rectangles and ellipses cover whole cells; lines end on cell corners; slices and moves go a cell at a time") + Shortcuts.hints.snapToGrid
                    checked: root.editor.snapToGrid
                    onToggled: root.editor.snapToGrid = !root.editor.snapToGrid
                }
                OptionPill {
                    text: qsTr("Edit on the canvas")
                    tip: qsTr("Shows handles to drag: one corner moves the grid, the next sizes its cells")
                    checked: root.canvas.editGrid
                    onToggled: {
                        root.canvas.editGrid = !root.canvas.editGrid
                        if (root.canvas.editGrid)
                            root.canvas.showCustomGrid = true
                    }
                }
            }

            Label { text: qsTr("Cell size") }
            RowLayout {
                spacing: 4
                NumberControl {
                    value: root.grid.width
                    from: 1
                    to: root.editor.maxImageSize
                    suffix: " px"
                    tip: qsTr("Cell width")
                    onMoved: v => root.editor.setGrid(root.grid.x, root.grid.y, v, root.grid.height)
                }
                Text { text: "\u00d7"; color: Theme.textDim; font.pixelSize: Theme.fontSize }
                NumberControl {
                    value: root.grid.height
                    from: 1
                    to: root.editor.maxImageSize
                    suffix: " px"
                    tip: qsTr("Cell height")
                    onMoved: v => root.editor.setGrid(root.grid.x, root.grid.y, root.grid.width, v)
                }
            }

            Label { text: qsTr("Offset") }
            RowLayout {
                spacing: 4
                NumberControl {
                    value: root.grid.x
                    from: -4096
                    to: 4096
                    suffix: " px"
                    tip: qsTr("Offset X")
                    onMoved: v => root.editor.setGrid(v, root.grid.y, root.grid.width, root.grid.height)
                }
                Text { text: ","; color: Theme.textDim; font.pixelSize: Theme.fontSize }
                NumberControl {
                    value: root.grid.y
                    from: -4096
                    to: 4096
                    suffix: " px"
                    tip: qsTr("Offset Y")
                    onMoved: v => root.editor.setGrid(root.grid.x, v, root.grid.width, root.grid.height)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.separator
        }

        Text {
            text: qsTr("Slices")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8
            Label { text: qsTr("Show") }
            Segmented {
                model: [qsTr("Off"), qsTr("On")]
                currentIndex: root.canvas.showSlices ? 1 : 0
                onActivated: i => root.canvas.showSlices = i === 1
            }
        }
    }
}
