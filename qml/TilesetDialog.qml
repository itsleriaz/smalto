pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// New tilemap layer / convert a layer to a tilemap: pick the tileset the
// layer uses, a new one (name, tile size, base index) or one the sprite
// already has. Layers sharing a tileset share its tiles.
Popup {
    id: root

    required property Editor editor

    property bool converting: false // false: a new tilemap layer
    property var tilesets: []
    property var setIndices: [] // choice i + 1 is tileset setIndices[i]
    property int tileWidth: 16
    property int tileHeight: 16
    property int baseIndex: 1
    // -1: "New tileset"; otherwise a tileset index.
    readonly property int chosen: setChoice.currentIndex > 0 ? setIndices[setChoice.currentIndex - 1] : -1
    readonly property var chosenSet: chosen >= 0 ? tilesets[chosen] : null

    function openFor(convert) {
        converting = convert
        open()
    }

    function apply() {
        // Built field by field, as a plain map: an object literal chosen by a
        // ternary reached the editor empty from the compiled QML.
        const options = {}
        if (chosen >= 0) {
            options.tileset = chosen
        } else {
            options.name = nameField.text
            options.tileWidth = tileWidth
            options.tileHeight = tileHeight
            options.baseIndex = baseIndex
        }
        if (converting)
            editor.convertToTilemap(options)
        else
            editor.addTilemapLayer(options)
        close()
    }

    onOpened: {
        const sets = editor.tilesetList()
        tilesets = sets
        // Tilesets kept in an external file have no tiles here to paint with.
        const choices = [qsTr("New tileset")]
        const indices = []
        for (let i = 0; i < sets.length; ++i)
            if (sets[i].usable) {
                choices.push(sets[i].name !== "" ? sets[i].name : qsTr("Tileset %1").arg(i + 1))
                indices.push(i)
            }
        setIndices = indices
        setChoice.model = choices
        // On a tilemap layer, its tileset is the likely pick (a new layer
        // beside it, the same tiles).
        const layer = editor.layers[editor.currentLayer]
        const own = !converting && layer && layer.kind === "tilemap" ? indices.indexOf(editor.currentTilesetIndex) : -1
        setChoice.currentIndex = own + 1
        tileWidth = editor.grid.width
        tileHeight = editor.grid.height
        baseIndex = 1
        nameField.text = converting ? editor.layers[editor.currentLayer].name : qsTr("Tileset %1").arg(sets.length + 1)
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    padding: 20
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Panel { solid: true }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    component Note: Text {
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
        Layout.maximumWidth: 280
    }

    contentItem: ColumnLayout {
        spacing: 16

        Text {
            text: root.converting ? qsTr("Convert to Tilemap") : qsTr("New Tilemap Layer")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
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

            Label {
                text: qsTr("Tile size")
                visible: root.chosen < 0
            }
            RowLayout {
                visible: root.chosen < 0
                spacing: 6
                NumberControl {
                    value: root.tileWidth
                    from: 1
                    to: 1024
                    suffix: " px"
                    onMoved: v => root.tileWidth = v
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
                    onMoved: v => root.tileHeight = v
                }
            }

            Label {
                text: qsTr("Base index")
                visible: root.chosen < 0
            }
            NumberControl {
                visible: root.chosen < 0
                value: root.baseIndex
                from: 0
                to: 9999
                tip: qsTr("The number the first tile shows as")
                onMoved: v => root.baseIndex = v
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            Note {
                text: root.chosenSet
                      ? qsTr("%1 × %2 px tiles, %3 tiles. Layers using this tileset share its tiles: editing a tile changes it on every one.")
                            .arg(root.chosenSet.tileWidth).arg(root.chosenSet.tileHeight).arg(root.chosenSet.tiles)
                      : root.converting
                      ? qsTr("The layer is cut into tiles on the grid; identical tiles are kept once.")
                      : qsTr("Starts empty. The tile size starts at the grid's.")
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
                text: root.converting ? qsTr("Convert") : qsTr("Create")
                primary: true
                onClicked: root.apply()
            }
        }
    }
}
