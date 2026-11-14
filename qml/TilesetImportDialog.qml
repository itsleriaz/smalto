pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Import a tileset image: the picture with its tiles outlined, the tile size,
// margin and spacing that cut it, and a new tileset (on a new tilemap layer)
// of the tiles, row by row.
Popup {
    id: root

    required property Editor editor
    required property CanvasView canvas

    property int tileWidth: 16
    property int tileHeight: 16
    property int sheetMargin: 0
    property int sheetSpacing: 0
    property bool skipEmpty: false
    property bool layOut: true
    property bool mergeSame: false
    property var tilesets: []
    property var setIndices: [] // choice i + 1 is tileset setIndices[i]
    // -1: a new tileset; otherwise the tileset the tiles are added to.
    readonly property int chosen: setChoice.currentIndex > 0 ? setIndices[setChoice.currentIndex - 1] : -1
    readonly property var chosenSet: chosen >= 0 ? tilesets[chosen] : null
    readonly property size grid: editor.tilesetSheetGrid(tileWidth, tileHeight, sheetMargin, sheetSpacing)
    readonly property int count: grid.width * grid.height

    // Call after loadTilesetSheet succeeded.
    function start() {
        preview.image = editor.tilesetSheet()
        nameField.text = editor.tilesetSheetName()
        const size = editor.tilesetSheetSize()
        // The document grid's size when the picture is cut evenly by it, else what the
        // picture's own edges suggest (the Detect button does the same on demand).
        const g = editor.grid
        const fits = size.width % g.width === 0 && size.height % g.height === 0 && size.width >= g.width && size.height >= g.height
        tileWidth = Math.max(1, Math.min(size.width, g.width))
        tileHeight = Math.max(1, Math.min(size.height, g.height))
        if (!fits)
            detect()
        sheetMargin = 0
        sheetSpacing = 0
        skipEmpty = false
        layOut = true
        mergeSame = false
        // Tilesets the sprite has (not the ones kept in an external file).
        const sets = editor.tilesetList()
        tilesets = sets
        const choices = [qsTr("New tileset")]
        const indices = []
        for (let i = 0; i < sets.length; ++i)
            if (sets[i].usable) {
                choices.push(sets[i].name !== "" ? sets[i].name : qsTr("Tileset %1").arg(i + 1))
                indices.push(i)
            }
        setIndices = indices
        setChoice.model = choices
        setChoice.currentIndex = 0
        open()
    }

    // Takes the tile size the picture's edges suggest, if they suggest one.
    function detect() {
        const size = editor.detectTilesetSize()
        if (size.width > 0 && size.height > 0) {
            tileWidth = size.width
            tileHeight = size.height
            sheetMargin = 0
            sheetSpacing = 0
            detectNote = ""
        } else {
            detectNote = qsTr("Nothing in the picture shows a tile size.")
        }
    }
    property string detectNote: ""

    function apply() {
        // Field by field, as a plain map.
        const options = {}
        options.name = nameField.text
        options.tileWidth = tileWidth
        options.tileHeight = tileHeight
        options.margin = sheetMargin
        options.spacing = sheetSpacing
        options.skipEmpty = skipEmpty
        options.merge = mergeSame
        options.layout = layOut
        options.tileset = chosen
        if (editor.importTileset(options))
            close()
    }

    // The size the sheet is cut with: the tileset's own when adding to one.
    onChosenSetChanged: if (chosenSet) {
        tileWidth = chosenSet.tileWidth
        tileHeight = chosenSet.tileHeight
    }

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Panel { solid: true }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    contentItem: ColumnLayout {
        spacing: 16

        Text {
            text: qsTr("Import Tileset Image")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        ImagePreview {
            id: preview
            Layout.preferredWidth: 360
            Layout.preferredHeight: 260
            checkerLight: root.canvas.checkerA
            checkerDark: root.canvas.checkerB
            cellColor: Theme.accent
            cells: root.editor.tilesetSheetCells(root.tileWidth, root.tileHeight, root.sheetMargin, root.sheetSpacing)
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 12

            Label { text: qsTr("Tileset") }
            AppComboBox {
                id: setChoice
                Layout.preferredWidth: 200
            }

            Label {
                text: qsTr("Name")
                visible: root.chosen < 0
            }
            AppTextField {
                id: nameField
                visible: root.chosen < 0
                Layout.preferredWidth: 200
                onAccepted: root.apply()
            }

            Label { text: qsTr("Tile size") }
            RowLayout {
                spacing: 6
                NumberControl {
                    value: root.tileWidth
                    from: 1
                    to: 1024
                    suffix: " px"
                    enabled: root.chosen < 0 // a tileset's tiles have a size already
                    opacity: enabled ? 1 : 0.5
                    onMoved: v => { root.tileWidth = v; root.detectNote = "" }
                }
                Text {
                    text: "×"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize
                }
                NumberControl {
                    value: root.tileHeight
                    from: 1
                    to: 1024
                    suffix: " px"
                    enabled: root.chosen < 0
                    opacity: enabled ? 1 : 0.5
                    onMoved: v => { root.tileHeight = v; root.detectNote = "" }
                }
                IconButton {
                    iconName: "wand-sparkles"
                    tip: qsTr("Guess the tile size from the picture (no margin or spacing)")
                    enabled: root.chosen < 0
                    onClicked: root.detect()
                }
            }

            Label { text: qsTr("Margin") }
            NumberControl {
                value: root.sheetMargin
                from: 0
                to: 256
                suffix: " px"
                tip: qsTr("Space around the tiles, inside the image's edge")
                onMoved: v => root.sheetMargin = v
            }

            Label { text: qsTr("Spacing") }
            NumberControl {
                value: root.sheetSpacing
                from: 0
                to: 256
                suffix: " px"
                tip: qsTr("Space between neighbouring tiles")
                onMoved: v => root.sheetSpacing = v
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            OptionPill {
                text: qsTr("Skip empty tiles")
                tip: qsTr("Fully transparent tiles are left out, so the tiles after them move up")
                checked: root.skipEmpty
                onToggled: root.skipEmpty = !root.skipEmpty
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            OptionPill {
                text: qsTr("Merge identical tiles")
                tip: qsTr("Equal tiles are made once (and blank ones are the empty tile); with a tileset chosen, also against the tiles it has")
                checked: root.mergeSame
                onToggled: root.mergeSame = !root.mergeSame
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            OptionPill {
                text: qsTr("Place the tiles as in the picture")
                tip: qsTr("The new layer starts with the tiles laid out as the picture shows them, from its top left")
                checked: root.layOut
                onToggled: root.layOut = !root.layOut
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            Text {
                text: (root.count > 0
                       ? qsTr("%1 × %2 = %3 tiles").arg(root.grid.width).arg(root.grid.height).arg(root.count)
                       : qsTr("The tile size doesn't fit the image."))
                      + (root.detectNote !== "" ? "  " + root.detectNote : "")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }
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
                enabled: root.count > 0
                onClicked: root.apply()
            }
        }
    }
}
