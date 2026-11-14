pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// The current tilemap layer's tileset, in the color panel's palette area
// while in tile mode: click a tile to draw with it (tile 0, the empty one,
// erases). New tile adds an empty one. Dragging the eyedropper over map cells
// picks them as a stamp. Numbers are as Aseprite shows them
// (from the tileset's base index).
ColumnLayout {
    id: root

    required property Editor editor
    required property CanvasView canvas
    // The side of a tile in the grid, in pixels (the color panel keeps it between runs).
    property int cellSize: 32
    signal cellSizeChosen(int size)
    readonly property var cellSizes: [24, 32, 48, 64]
    // How many tiles a row of the grid holds, whatever the panel's width or the tile size: the
    // same tile is always in the same place, so a block picked with Shift-click means the same
    // thing everywhere. 0 (Auto) wraps at the panel's width instead. Kept between runs.
    property int columns: 8
    signal columnsChosen(int columns)
    readonly property var columnChoices: [0, 4, 6, 8, 10, 12, 16, 32]
    // The columns the grid has now.
    readonly property int gridColumns: columns > 0 ? columns
                                                   : Math.max(1, Math.floor((width + tiles.spacing) / (cellSize + tiles.spacing)))
    // How many cells show each tile, by tile index; a tile nothing shows is dimmed.
    property var useCounts: []
    // Reordering: the tile being dragged and the position it would land at (before the tile
    // with that index; the tile count means the end).
    property int dragFrom: -1
    property int dropAt: -1
    // The tiles the stamp uses, as {id: true}, so the grid can mark them.
    property var stampTiles: ({})

    function refreshStamp() {
        const marks = {}
        for (const id of root.editor.tileStampIds())
            marks[id] = true
        stampTiles = marks
    }

    spacing: 6

    function refreshCounts() {
        useCounts = root.editor.tilemapLayer ? root.editor.tileUseCounts() : []
    }
    Component.onCompleted: { refreshCounts(); refreshStamp() }
    Connections {
        target: root.editor
        // Placed or removed cells (strokes, undo, redo) and tile edits all land here.
        function onHistoryChanged() { root.refreshCounts() }
        function onTilesetChanged() { root.refreshCounts(); root.refreshStamp() }
        function onTileCursorChanged() { root.refreshStamp() }
    }

    // Shift-click: the tiles between the current one and the clicked one, as laid out in
    // the grid here (a rectangle of cells), become the stamp for the pencil.
    function pickBlock(from, to) {
        const columns = root.gridColumns
        const c0 = Math.min(from % columns, to % columns), c1 = Math.max(from % columns, to % columns)
        const r0 = Math.min(Math.floor(from / columns), Math.floor(to / columns))
        const r1 = Math.max(Math.floor(from / columns), Math.floor(to / columns))
        const ids = []
        for (let r = r0; r <= r1; ++r)
            for (let c = c0; c <= c1; ++c) {
                const index = r * columns + c
                ids.push(index > 0 && index < root.editor.tileCount ? index : 0)
            }
        root.editor.setTileStamp(c1 - c0 + 1, ids)
    }

    // The position a tile dropped at `p` (in the grid's coordinates) would take: before the
    // tile under it (its left half) or after it, and the end past the last tile. Never before
    // tile 1: the empty tile stays first.
    function insertionAt(p: point): int {
        const count = root.editor.tileCount
        let best = count
        for (let i = 1; i < count; ++i) {
            const c = tilesRepeater.itemAt(i)
            if (!c)
                continue
            if (p.y < c.y)
                return i === 1 ? 1 : (best === count ? i : best)
            if (p.y < c.y + c.height + 4) {   // this row
                if (p.x < c.x + c.width / 2)
                    return i
                best = i + 1
            }
        }
        return Math.max(1, best)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            Layout.fillWidth: true
            text: "Tileset" + (root.editor.tilesetName !== "" ? " \u00b7 " + root.editor.tilesetName : "")
                  + " \u00b7 " + root.editor.tileSize.width + "\u00d7" + root.editor.tileSize.height
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
        }
        // Flips for placed tiles (bits: 4 x, 2 y, 1 diagonal / turned).
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "flip-horizontal-2"
            tip: "Place tiles flipped horizontally"
            active: (root.editor.tileFlips & 4) !== 0
            onClicked: root.editor.tileFlips = root.editor.tileFlips ^ 4
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "flip-vertical-2"
            tip: "Place tiles flipped vertically"
            active: (root.editor.tileFlips & 2) !== 0
            onClicked: root.editor.tileFlips = root.editor.tileFlips ^ 2
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "rotate-cw"
            tip: "Place tiles turned (x and y swapped; with the flips, any quarter turn)"
            active: (root.editor.tileFlips & 1) !== 0
            onClicked: root.editor.tileFlips = root.editor.tileFlips ^ 1
        }
        IconButton {
            id: sizeButton
            implicitWidth: 26
            implicitHeight: 26
            iconName: "zoom-in"
            tip: "Tile size and columns of the grid (Ctrl-scroll over it changes the size). A fixed number of columns keeps every tile in the same place, so a block picked with Shift-click is the same at any panel width"
            active: sizeMenu.visible
            onClicked: sizeMenu.open()
            AppMenu {
                id: sizeMenu
                y: sizeButton.height + 4
                Repeater {
                    model: root.cellSizes
                    delegate: AppMenuItem {
                        required property int modelData
                        text: (modelData === root.cellSize ? "\u2713 " : "    ") + modelData + " px"
                        onTriggered: root.cellSizeChosen(modelData)
                    }
                }
                AppMenuSeparator {}
                Repeater {
                    model: root.columnChoices
                    delegate: AppMenuItem {
                        required property int modelData
                        text: (modelData === root.columns ? "\u2713 " : "    ")
                              + (modelData === 0 ? "Columns: fit the panel" : "Columns: " + modelData)
                        onTriggered: root.columnsChosen(modelData)
                    }
                }
            }
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "plus"
            tip: "New tile (empty)"
            onClicked: root.editor.addTile()
        }
    }

    // Managing the tiles: each is one undo step and the tilemaps follow.
    RowLayout {
        id: tools
        Layout.fillWidth: true
        spacing: 2
        readonly property int tile: root.editor.currentTile
        readonly property bool real: tile > 0
        // Counts that change with the tiles and the cells: read on demand.
        property int unused: 0
        property int duplicates: 0
        function refresh() {
            unused = root.editor.unusedTileCount()
            duplicates = root.editor.duplicateTileCount()
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "copy"
            tip: "Duplicate the tile"
            enabled: tools.real
            onClicked: root.editor.duplicateTile(tools.tile)
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "pencil"
            tip: "Edit the tile in its own tab, repeating around itself (double-click a tile does too)"
            enabled: tools.real
            onClicked: root.editor.editTileInTab(tools.tile)
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "chevron-left"
            tip: "Move the tile earlier"
            enabled: tools.tile > 1
            onClicked: root.editor.moveTile(tools.tile, tools.tile - 1)
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "chevron-right"
            tip: "Move the tile later"
            enabled: tools.real && tools.tile < root.editor.tileCount - 1
            onClicked: root.editor.moveTile(tools.tile, tools.tile + 1)
        }
        IconButton {
            implicitWidth: 26
            implicitHeight: 26
            iconName: "trash-2"
            tip: "Delete the tile (where it is used, the cells become empty)"
            enabled: tools.real
            onClicked: root.editor.deleteTile(tools.tile)
        }
        // A block picked with the eyedropper (drag over cells) to place together.
        Rectangle {
            visible: root.editor.tileStamp.width * root.editor.tileStamp.height > 1
            Layout.preferredHeight: 22
            Layout.preferredWidth: stampText.implicitWidth + 28
            radius: Theme.radiusSmall
            color: Theme.accentSoft
            Text {
                id: stampText
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Stamp " + root.editor.tileStamp.width + "\u00d7" + root.editor.tileStamp.height
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall
            }
            IconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 20
                implicitHeight: 20
                iconName: "x"
                tip: "Back to the single tile"
                onClicked: root.editor.clearTileStamp()
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            // From the counts the grid uses, which follow every placed or erased cell
            // (the tileset revision alone doesn't change then).
            readonly property int uses: tools.real && tools.tile < root.useCounts.length ? root.useCounts[tools.tile] : 0
            text: tools.real ? (uses === 0 ? "unused" : "used " + uses + "\u00d7") : ""
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
        }
        IconButton {
            id: tidyButton
            implicitWidth: 26
            implicitHeight: 26
            iconName: "wand-sparkles"
            tip: "Tidy the tileset"
            active: tidyMenu.visible
            onClicked: {
                tools.refresh()
                tidyMenu.open()
            }
            AppMenu {
                id: tidyMenu
                y: tidyButton.height + 4
                AppMenuItem {
                    text: "Remove unused tiles (" + tools.unused + ")"
                    enabled: tools.unused > 0
                    onTriggered: root.editor.removeUnusedTiles()
                }
                AppMenuItem {
                    text: "Merge duplicate tiles (" + tools.duplicates + ")"
                    enabled: tools.duplicates > 0
                    onTriggered: root.editor.mergeDuplicateTiles()
                }
            }
        }
    }

    Flickable {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: tiles.implicitHeight
        Layout.minimumHeight: Math.min(tiles.implicitHeight, 56)
        // A fixed number of columns can be wider than the panel: the grid then scrolls sideways.
        contentWidth: Math.max(width, tiles.width)
        contentHeight: tiles.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}
        ScrollBar.horizontal: AppScrollBar {}

        // Ctrl / Cmd + scroll: bigger or smaller tiles.
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => {
                const i = root.cellSizes.indexOf(root.cellSize)
                const next = Math.max(0, Math.min(root.cellSizes.length - 1, i + (event.angleDelta.y > 0 ? 1 : -1)))
                root.cellSizeChosen(root.cellSizes[next])
            }
        }

        // Where the dragged tile would land: a line between two tiles.
        Rectangle {
            parent: tiles
            z: 10
            visible: root.dragFrom >= 0 && root.dropAt >= 0
            width: 2
            radius: 1
            color: Theme.accent
            readonly property Item before: root.dropAt < root.editor.tileCount ? tilesRepeater.itemAt(root.dropAt) : null
            readonly property Item last: tilesRepeater.itemAt(root.editor.tileCount - 1)
            x: before ? before.x - 3 : last ? last.x + last.width + 1 : 0
            y: before ? before.y : last ? last.y : 0
            height: before ? before.height : last ? last.height : 0
        }

        Flow {
            id: tiles
            // Exactly the columns' width, so the Flow wraps there.
            width: root.columns > 0 ? root.columns * (root.cellSize + spacing) - spacing + 1 : parent.width
            spacing: 4

            Repeater {
                id: tilesRepeater
                model: root.editor.tileCount
                delegate: Rectangle {
                    id: cell
                    required property int index
                    readonly property bool current: index === root.editor.currentTile
                    // Part of the stamp (a block picked from the grid or the map).
                    readonly property bool inStamp: root.stampTiles[index] === true
                    // Nothing shows this tile (tile 0, the empty one, is never "unused").
                    readonly property bool unused: index > 0 && index < root.useCounts.length && root.useCounts[index] === 0
                    width: root.cellSize
                    height: root.cellSize
                    radius: 5
                    color: inStamp ? Theme.accentSoft : area.containsMouse ? Theme.hover : "transparent"
                    border.color: current || inStamp ? Theme.accent : Theme.swatchBorder
                    border.width: current || inStamp ? 2 : 1
                    opacity: root.dragFrom === index ? 0.4 : 1

                    ImagePreview {
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: cell.index > 0
                        opacity: cell.unused ? 0.4 : 1
                        // Depends on tilesetRevision, so it redraws when tiles change.
                        image: {
                            const revision = root.editor.tilesetRevision
                            return revision >= 0 ? root.editor.tileImage(cell.index) : root.editor.tileImage(cell.index)
                        }
                        checkerLight: root.canvas.checkerA
                        checkerDark: root.canvas.checkerB
                        checkerSize: 3
                    }
                    // The empty tile.
                    Rectangle {
                        visible: cell.index === 0
                        anchors.centerIn: parent
                        width: parent.width * 0.6
                        height: 1.5
                        rotation: -45
                        color: Theme.textFaint
                    }
                    // An unused tile: a small hollow dot in the corner (the image is dimmed too).
                    Rectangle {
                        visible: cell.unused
                        x: 4
                        y: 4
                        width: 6
                        height: 6
                        radius: 3
                        color: "transparent"
                        border.color: Theme.textDim
                        border.width: 1
                    }
                    Text {
                        visible: cell.index > 0 && (area.containsMouse || cell.current)
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 2
                        text: cell.index - 1 + root.editor.tileBaseIndex
                        color: Theme.text
                        style: Text.Outline
                        styleColor: Theme.panel
                        font.pixelSize: 9
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true // a drag reorders, it doesn't scroll
                        property point pressPos
                        onPressed: mouse => pressPos = Qt.point(mouse.x, mouse.y)
                        onPositionChanged: mouse => {
                            if (!pressed || cell.index === 0)
                                return
                            if (root.dragFrom < 0 && Math.hypot(mouse.x - pressPos.x, mouse.y - pressPos.y) < 4)
                                return
                            root.dragFrom = cell.index
                            root.dropAt = root.insertionAt(mapToItem(tiles, mouse.x, mouse.y))
                        }
                        onReleased: {
                            if (root.dragFrom >= 0 && root.dropAt >= 0) {
                                // The line is before tile `dropAt`; the tile ends up one earlier
                                // when it came from before that.
                                const to = root.dropAt > root.dragFrom ? root.dropAt - 1 : root.dropAt
                                if (to !== root.dragFrom)
                                    root.editor.moveTile(root.dragFrom, to)
                            }
                            root.dragFrom = -1
                            root.dropAt = -1
                        }
                        onCanceled: {
                            root.dragFrom = -1
                            root.dropAt = -1
                        }
                        onClicked: mouse => {
                            if ((mouse.modifiers & Qt.ShiftModifier) && cell.index > 0 && root.editor.currentTile > 0)
                                root.pickBlock(root.editor.currentTile, cell.index)
                            else
                                root.editor.currentTile = cell.index
                        }
                        onDoubleClicked: if (cell.index > 0) root.editor.editTileInTab(cell.index)
                    }
                }
            }
        }
    }
}
