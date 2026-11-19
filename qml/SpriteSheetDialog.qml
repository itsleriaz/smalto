pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Smalto

// Import Sprite Sheet: cuts the picture in the current tab into tiles that become the frames of a new
// sprite. Select File opens another picture (in a tab of its own) to cut instead. The first tile's box
// (X, Y, Width, Height) is also on the canvas (SpriteSheetHandles) with handles to drag; the other
// tiles follow it. A floating window, so the canvas stays in view; nothing changes until Import.
FloatingDialog {
    id: root

    required property Editor editor

    // The first tile, and how the rest follow it.
    property int tileX: 0
    property int tileY: 0
    property int tileW: 16
    property int tileH: 16
    property int padX: 0
    property int padY: 0
    property int columns: 1
    property int rows: 1
    property bool usePadding: false
    property bool partial: false
    property bool skipEmpty: false
    // 0 horizontal strip, 1 vertical strip, 2 by rows, 3 by columns.
    property int sheetType: 2
    readonly property bool byColumns: sheetType === 3

    readonly property int sheetW: editor.canvasWidth
    readonly property int sheetH: editor.canvasHeight
    readonly property int count: columns * rows
    readonly property int maxFrames: 4096
    readonly property bool valid: tileW >= 1 && tileH >= 1 && columns >= 1 && rows >= 1 && count <= maxFrames
                                  && tileX < sheetW && tileY < sheetH

    function options(): var {
        return { x: tileX, y: tileY, width: tileW, height: tileH, padX: usePadding ? padX : 0,
                 padY: usePadding ? padY : 0, columns: columns, rows: rows, byColumns: byColumns,
                 skipEmpty: skipEmpty }
    }

    // Columns and rows that fit, as the type has them (a strip is one row or column).
    function refit() {
        const fit = editor.spriteSheetFit(options(), partial)
        columns = sheetType === 1 ? 1 : fit.width
        rows = sheetType === 0 ? 1 : fit.height
    }

    // The tile box changed (typed or dragged): clamp it to the sheet, and fit the grid again.
    function setBox(x, y, w, h) {
        tileX = Math.max(0, Math.min(sheetW - 1, Math.round(x)))
        tileY = Math.max(0, Math.min(sheetH - 1, Math.round(y)))
        tileW = Math.max(1, Math.min(editor.maxImageSize, Math.round(w)))
        tileH = Math.max(1, Math.min(editor.maxImageSize, Math.round(h)))
        refit()
    }
    function setPadding(px, py) {
        padX = Math.max(0, Math.min(1024, Math.round(px)))
        padY = Math.max(0, Math.min(1024, Math.round(py)))
        refit()
    }

    // Starts from the document's grid when it cuts the picture, else the whole picture as one tile.
    function reset() {
        const g = editor.grid
        const fits = g.width <= sheetW && g.height <= sheetH
        tileX = 0
        tileY = 0
        tileW = fits ? g.width : sheetW
        tileH = fits ? g.height : sheetH
        padX = 0
        padY = 0
        usePadding = false
        refit()
    }

    function apply() {
        if (!valid)
            return
        if (editor.importSpriteSheet(options()))
            close()
    }

    onOpened: reset()
    // Another picture, tab or edit: the box no longer applies.
    Connections {
        target: root.editor
        function onDocumentChanged() { if (root.visible) root.reset() }
        function onCanvasSizeChanged() { if (root.visible) root.reset() }
    }
    onSheetTypeChanged: refit()
    onPartialChanged: refit()

    FileDialog {
        id: sheetFile
        title: qsTr("Select Sprite Sheet")
        nameFilters: [qsTr("Images (*.png *.gif *.jpg *.jpeg *.bmp *.tga)")]
        onAccepted: root.editor.openPath(root.editor.toLocalPath(selectedFile))
    }

    dragHeight: title.height + 8

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }
    component Unit: Text {
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
    }
    component Field: NumberControl {
        fieldWidth: 64
        tabStop: true
        bigStep: 10
        onAccepted: root.apply()
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            id: title
            text: qsTr("Import Sprite Sheet")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        AppButton {
            Layout.fillWidth: true
            text: qsTr("Select File…")
            tip: qsTr("Open another picture to cut. Without it, the picture in this tab is cut.")
            onClicked: sheetFile.open()
        }
        Unit {
            Layout.fillWidth: true
            Layout.maximumWidth: 300
            elide: Text.ElideMiddle
            text: qsTr("Cutting %1 (%2 × %3)").arg(root.editor.fileName).arg(root.sheetW).arg(root.sheetH)
        }

        GridLayout {
            columns: 4
            columnSpacing: 10
            rowSpacing: 10

            Label { text: qsTr("Type") }
            AppComboBox {
                Layout.columnSpan: 3
                Layout.fillWidth: true
                model: [qsTr("Horizontal Strip"), qsTr("Vertical Strip"), qsTr("By Rows"), qsTr("By Columns")]
                currentIndex: root.sheetType
                onActivated: index => root.sheetType = index
            }

            Label { text: qsTr("X") }
            Field {
                from: 0
                to: Math.max(0, root.sheetW - 1)
                value: root.tileX
                tip: qsTr("Left edge of the first tile")
                onMoved: v => root.setBox(v, root.tileY, root.tileW, root.tileH)
            }
            Label { text: qsTr("Y") }
            Field {
                from: 0
                to: Math.max(0, root.sheetH - 1)
                value: root.tileY
                tip: qsTr("Top edge of the first tile")
                onMoved: v => root.setBox(root.tileX, v, root.tileW, root.tileH)
            }

            Label { text: qsTr("Width") }
            Field {
                from: 1
                to: root.editor.maxImageSize
                value: root.tileW
                tip: qsTr("Width of each tile")
                onMoved: v => root.setBox(root.tileX, root.tileY, v, root.tileH)
            }
            Label { text: qsTr("Height") }
            Field {
                from: 1
                to: root.editor.maxImageSize
                value: root.tileH
                tip: qsTr("Height of each tile")
                onMoved: v => root.setBox(root.tileX, root.tileY, root.tileW, v)
            }

            Label { text: qsTr("Columns") }
            Field {
                from: 1
                to: 4096
                value: root.columns
                enabled: root.sheetType !== 1
                opacity: enabled ? 1 : 0.5
                onMoved: v => root.columns = v
            }
            Label { text: qsTr("Rows") }
            Field {
                from: 1
                to: 4096
                value: root.rows
                enabled: root.sheetType !== 0
                opacity: enabled ? 1 : 0.5
                onMoved: v => root.rows = v
            }

            Item { implicitWidth: 1; implicitHeight: 1 }
            OptionPill {
                Layout.columnSpan: 3
                text: qsTr("Padding")
                tip: qsTr("Space between neighbouring tiles")
                checked: root.usePadding
                onToggled: {
                    root.usePadding = !root.usePadding
                    root.refit()
                }
            }

            Label { text: qsTr("Pad X"); visible: root.usePadding }
            Field {
                visible: root.usePadding
                from: 0
                to: 1024
                value: root.padX
                onMoved: v => root.setPadding(v, root.padY)
            }
            Label { text: qsTr("Pad Y"); visible: root.usePadding }
            Field {
                visible: root.usePadding
                from: 0
                to: 1024
                value: root.padY
                onMoved: v => root.setPadding(root.padX, v)
            }

            Item { implicitWidth: 1; implicitHeight: 1 }
            OptionPill {
                Layout.columnSpan: 3
                text: qsTr("Include partial tiles at bottom/right edges")
                tip: qsTr("A tile cut off by the picture's edge counts as a tile; what is missing is transparent")
                checked: root.partial
                onToggled: root.partial = !root.partial
            }

            Item { implicitWidth: 1; implicitHeight: 1 }
            OptionPill {
                Layout.columnSpan: 3
                text: qsTr("Skip empty tiles")
                tip: qsTr("Fully transparent tiles are left out")
                checked: root.skipEmpty
                onToggled: root.skipEmpty = !root.skipEmpty
            }
        }

        Unit {
            Layout.fillWidth: true
            Layout.maximumWidth: 330
            wrapMode: Text.WordWrap
            color: root.count > root.maxFrames || root.columns < 1 ? Theme.danger : Theme.textFaint
            text: root.count > root.maxFrames
                  ? qsTr("That makes more than %1 tiles.").arg(root.maxFrames)
                  : root.valid ? qsTr("%1 × %2 = %3 frames. Drag the box on the canvas to move or size the first tile.")
                                    .arg(root.columns).arg(root.rows).arg(root.count)
                               : qsTr("The first tile doesn't fit the picture.")
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Import")
                primary: true
                enabled: root.valid
                onClicked: root.apply()
            }
        }
    }
}
