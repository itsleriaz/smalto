pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Smalto

// Export the current tilemap layer's tileset for an engine: the tiles as one
// PNG (the empty tile left out), and optionally a Tiled tileset and map (XML:
// .tsx / .tmx, or JSON: .tsj / .tmj) and a CSV per tilemap layer. Godot, Unity and
// Tiled read the PNG with the tile size; Tiled reads all of it. The choices are
// kept between runs (Main.qml).
Popup {
    id: root

    required property Editor editor
    required property CanvasView canvas

    // Kept between runs. `columns` is the wish; `cols` what this tileset can show of it.
    property int columns: 8
    property int sheetMargin: 0
    property int sheetSpacing: 0
    property bool tsx: true
    property bool tmx: false
    property bool tsj: false
    property bool tmj: false
    property bool csv: false
    property bool tres: false
    // Where the PNGs will be in the Godot project, for the TileSet's texture paths.
    property string godotFolder: "res://"
    property string lastFolder: ""
    readonly property int cols: Math.max(1, Math.min(columns, Math.max(1, editor.tileCount - 1)))
    readonly property var info: {
        const revision = editor.tilesetRevision
        return revision >= 0 ? editor.tilesetExportInfo(cols, sheetMargin, sheetSpacing)
                             : editor.tilesetExportInfo(cols, sheetMargin, sheetSpacing)
    }
    readonly property int tileTotal: info.tiles ?? 0
    readonly property int mapLayers: info.layers ?? 0
    readonly property int mapTilesets: info.mapTilesets ?? 0
    readonly property int skippedLayers: info.skippedLayers ?? 0
    // The file name the dialog suggests: the tileset's (an imported picture's name, as the
    // import named it), made safe for a file name.
    readonly property string suggestedName: (editor.tilesetName !== "" ? editor.tilesetName : qsTr("tileset"))
                                            .replace(/[\\/:*?"<>|]/g, "_") + ".png"

    function start() {
        open()
    }

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Panel {}
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Export Tileset")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG (*.png)")]
        onAccepted: {
            if (root.editor.exportTileset(selectedFile, { columns: root.cols, margin: root.sheetMargin,
                                                           spacing: root.sheetSpacing, tsx: root.tsx,
                                                           tmx: root.tmx, tsj: root.tsj, tmj: root.tmj,
                                                           csv: root.csv, tres: root.tres,
                                                           godotFolder: root.godotFolder })) {
                root.lastFolder = selectedFile.toString().replace(/\/[^\/]*$/, "")
                root.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 16

        Text {
            text: qsTr("Export Tileset")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        ImagePreview {
            Layout.preferredWidth: 360
            Layout.preferredHeight: 240
            checkerLight: root.canvas.checkerA
            checkerDark: root.canvas.checkerB
            image: root.editor.tilesetExportImage(root.cols, root.sheetMargin, root.sheetSpacing)
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 12

            Label { text: qsTr("Columns") }
            NumberControl {
                value: root.cols
                from: 1
                to: Math.max(1, root.tileTotal)
                onMoved: v => root.columns = v
            }

            Label { text: qsTr("Margin") }
            NumberControl {
                value: root.sheetMargin
                from: 0
                to: 64
                suffix: " px"
                onMoved: v => root.sheetMargin = v
            }

            Label { text: qsTr("Spacing") }
            NumberControl {
                value: root.sheetSpacing
                from: 0
                to: 64
                suffix: " px"
                onMoved: v => root.sheetSpacing = v
            }

            Label { text: qsTr("Also write") }
            ColumnLayout {
                spacing: 8
                OptionPill {
                    text: qsTr("Tiled tileset (.tsx)")
                    checked: root.tsx || root.tmx
                    enabled: !root.tmx
                    onToggled: root.tsx = !root.tsx
                }
                OptionPill {
                    text: qsTr("Tiled map (.tmx)")
                    tip: qsTr("Every tilemap layer of this tileset with tiles on this frame, as one map (needs the tileset file)")
                    enabled: root.mapLayers > 0
                    checked: root.tmx
                    onToggled: root.tmx = !root.tmx
                }
                OptionPill {
                    text: qsTr("Tiled tileset, JSON (.tsj)")
                    tip: qsTr("The same tileset in Tiled's JSON format, which many engines and importers read more easily")
                    checked: root.tsj || root.tmj
                    enabled: !root.tmj
                    onToggled: root.tsj = !root.tsj
                }
                OptionPill {
                    text: qsTr("Tiled map, JSON (.tmj)")
                    tip: qsTr("The map as Tiled's JSON (needs the JSON tileset file)")
                    enabled: root.mapLayers > 0
                    checked: root.tmj
                    onToggled: root.tmj = !root.tmj
                }
                OptionPill {
                    text: qsTr("Godot 4 TileSet (.tres)")
                    tip: qsTr("A TileSet resource for Godot 4.3 or newer: an atlas source with every tile, pointing at the PNG. Put the files in your project and set the folder below")
                    checked: root.tres
                    onToggled: root.tres = !root.tres
                }
                AppTextField {
                    visible: root.tres
                    Layout.preferredWidth: 230
                    compact: true
                    placeholderText: "res://art/tiles/"
                    text: root.godotFolder
                    selectAllOnFocus: true
                    onTextEdited: root.godotFolder = text
                }
                OptionPill {
                    text: qsTr("CSV per tilemap layer")
                    tip: qsTr("Tile numbers as Tiled counts them: 0 is empty, the first tile is 1")
                    enabled: root.mapLayers > 0
                    checked: root.csv
                    onToggled: root.csv = !root.csv
                }
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            Text {
                text: (root.tileTotal > 0
                       ? qsTr("%1 tiles, %2 × %3 px image. %4 tilemap layer(s) of this tileset on this frame.")
                             .arg(root.tileTotal).arg(root.info.width).arg(root.info.height).arg(root.mapLayers)
                       : qsTr("The tileset has no tiles to export."))
                      + (root.mapTilesets > 1
                         ? "\n" + qsTr("A map holds %1 layers of %2 tilesets of this tile size; each tileset gets its own files, named after it.")
                               .arg(root.info.mapLayers).arg(root.mapTilesets)
                         : "")
                      + (root.skippedLayers > 0
                         ? "\n" + qsTr("%1 layer(s) use another tile size and stay out of the map.").arg(root.skippedLayers)
                         : "")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 260
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
                text: qsTr("Export…")
                primary: true
                enabled: root.tileTotal > 0
                onClicked: {
                    if (root.lastFolder !== "")
                        fileDialog.currentFile = root.lastFolder + "/" + root.suggestedName
                    fileDialog.open()
                }
            }
        }
    }
}
