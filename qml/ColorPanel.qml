pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

Panel {
    id: root

    required property Editor editor
    required property CanvasView canvas
    // Tile mode on a tilemap layer: the tileset shows instead of the palette.
    readonly property bool showTiles: editor.tilemapLayer && editor.editTiles

    // What the window arranges (PanelColumns): the height it wants (dragged
    // smaller on a divider: the palette scrolls), the least it can be, and
    // how early it gives way when the column is short (0 first).
    property real userHeight: -1
    readonly property real wantHeight: userHeight >= 0 ? Math.max(minimumHeight, Math.min(implicitHeight, userHeight)) : implicitHeight
    readonly property real minHeight: minimumHeight
    readonly property int shrinkRank: 0
    movable: true

    // Folds the palette away, header only (like the preview's).
    property bool paletteCollapsed: false
    // The side of a tile in the tileset grid (kept between runs).
    property int tileCellSize: 32
    // The columns of the tileset grid (0: as many as fit the panel); kept between runs.
    property int tileColumns: 8

    // Beside the panel, level with its button, on the side with room: to the
    // right when the panel is in the left half of the window. `item` is the
    // popup's parent (the button), whose coordinates its x uses.
    function sideX(item: Item, popupWidth: real): real {
        const middle = root.mapToItem(null, root.width / 2, 0).x
        return middle < Overlay.overlay.width / 2 ? root.mapToItem(item, root.width, 0).x + 8
                                                  : root.mapToItem(item, 0, 0).x - popupWidth - 8
    }

    signal loadPaletteRequested()
    signal savePaletteRequested()

    // "#rrggbb", plus "aa" when not opaque.
    function hexOf(c) {
        const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return ("#" + h(c.r) + h(c.g) + h(c.b) + (c.a < 1 ? h(c.a) : "")).toUpperCase()
    }
    // Swatch picked last (for replace / remove), and drag-to-reorder state.
    property int selectedSwatch: -1
    // The swatch under the pointer, for the one tooltip they share (a tooltip
    // per swatch made rebuilding the palette slow).
    property Item hoveredSwatch: null
    // Swatches are kept when a smaller palette comes in (another tab), and
    // hidden: switching back and forth then makes none.
    property int swatchPool: 0
    readonly property int paletteSize: editor.palette.length
    // For screenshots (`--show popup:palette` / `paletteTools`).
    function showPopup(name: string) {
        if (name === "palette" || name === "lospec") {
            if (name === "lospec")
                palettePopup.tab = 1
            palettePopup.open()
        }
        else if (name === "paletteTools")
            paletteToolsPopup.open()
    }
    onPaletteSizeChanged: swatchPool = Math.max(swatchPool, paletteSize)
    property int dragFrom: -1
    property int dropIndex: -1
    onSelectedSwatchChanged: if (selectedSwatch >= editor.palette.length) selectedSwatch = -1
    Connections {
        target: root.editor
        function onPaletteChanged() {
            if (root.selectedSwatch >= root.editor.palette.length)
                root.selectedSwatch = root.editor.palette.length - 1
        }
    }

    implicitHeight: layout.implicitHeight + padding * 2
    // Short of this, the palette is already as small as it gets (it scrolls).
    readonly property real minimumHeight: showTiles || paletteCollapsed ? implicitHeight
        : implicitHeight - Math.max(0, paletteView.Layout.preferredHeight - paletteView.Layout.minimumHeight)
    padding: 12
    clipContent: true

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: 10

        RowLayout {
            spacing: 10

            // Primary over secondary, click secondary (or press X) to swap.
            Item {
                implicitWidth: 44
                implicitHeight: 44

                Rectangle {
                    x: 16; y: 16
                    width: 28; height: 28
                    radius: 14
                    color: root.editor.secondaryColor
                    border.color: Theme.panelBorder
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.editor.swapColors()
                    }
                }
                Rectangle {
                    width: 32; height: 32
                    radius: 16
                    color: root.editor.primaryColor
                    border.color: Theme.panel
                    border.width: 2
                }
            }

            ColumnLayout {
                spacing: 2
                PanelTitle { text: "Color" }
                // Type a color: #rgb, #rrggbb or #rrggbbaa.
                AppTextField {
                    id: hexField
                    compact: true
                    selectAllOnFocus: true
                    implicitWidth: 92
                    font.pixelSize: Theme.fontSizeSmall
                    font.features: { "tnum": 1 }
                    text: root.hexOf(root.editor.primaryColor)
                    validator: RegularExpressionValidator { regularExpression: /#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})/ }
                    onEditingFinished: {
                        if (!acceptableInput) {
                            text = root.hexOf(root.editor.primaryColor)
                            return
                        }
                        let t = text.replace("#", "")
                        if (t.length === 3)
                            t = t[0] + t[0] + t[1] + t[1] + t[2] + t[2]
                        const v = i => parseInt(t.substr(i, 2), 16) / 255
                        root.editor.primaryColor = Qt.rgba(v(0), v(2), v(4), t.length === 8 ? v(6) : 1)
                        text = Qt.binding(() => root.hexOf(root.editor.primaryColor))
                    }
                }
            }

            Item { Layout.fillWidth: true }

            IconButton {
                iconName: "arrow-left-right"
                tip: "Swap colors" + Shortcuts.hints.swapColors
                onClicked: root.editor.swapColors()
            }
        }

        ColorPicker {
            Layout.fillWidth: true
            current: root.editor.primaryColor
            onEdited: c => root.editor.primaryColor = c
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.separator
        }

        // Tilemap layers: pixel mode paints, tile mode places tiles; the
        // tileset takes the palette's place in tile mode.
        RowLayout {
            visible: root.editor.tilemapLayer
            Layout.fillWidth: true
            Text {
                text: "Tilemap" + (root.editor.tilesetName !== "" ? " \u00b7 " + root.editor.tilesetName : "")
                elide: Text.ElideRight
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                Layout.fillWidth: true
            }
            Segmented {
                model: ["Pixels", "Tiles"]
                currentIndex: root.editor.editTiles ? 1 : 0
                onActivated: i => root.editor.editTiles = i === 1
            }
        }
        TilesetPanel {
            visible: root.showTiles
            Layout.fillWidth: true
            Layout.fillHeight: true
            editor: root.editor
            canvas: root.canvas
            cellSize: root.tileCellSize
            onCellSizeChosen: size => root.tileCellSize = size
            columns: root.tileColumns
            onColumnsChosen: n => root.tileColumns = n
        }

        RowLayout {
            visible: !root.showTiles
            Layout.fillWidth: true
            spacing: 2
            IconButton {
                implicitWidth: 22
                implicitHeight: 26
                iconName: root.paletteCollapsed ? "chevron-right" : "chevron-down"
                tip: root.paletteCollapsed ? "Show the palette" : "Collapse the palette"
                onClicked: root.paletteCollapsed = !root.paletteCollapsed
            }
            PanelTitle {
                text: "Palette"
                Layout.fillWidth: true
            }
            IconButton {
                visible: !root.paletteCollapsed
                implicitWidth: 26
                implicitHeight: 26
                iconName: "sliders-horizontal"
                tip: "Palette tools: ramp, sort, reduce"
                active: paletteToolsPopup.visible
                onClicked: paletteToolsPopup.visible ? paletteToolsPopup.close() : paletteToolsPopup.open()

                // Left of the panel, level with the button: the swatches stay in view.
                PaletteToolsPopup {
                    id: paletteToolsPopup
                    editor: root.editor
                    onAboutToShow: {
                        x = root.sideX(parent, width)
                        y = 0
                    }
                }
            }
            IconButton {
                visible: !root.paletteCollapsed
                implicitWidth: 26
                implicitHeight: 26
                iconName: "palette"
                tip: "Presets, load and save"
                active: palettePopup.visible
                onClicked: palettePopup.open()

                // Left of the panel, level with the button: the swatches stay in view.
                PalettePopup {
                    id: palettePopup
                    editor: root.editor
                    onWidthChanged: if (visible) x = root.sideX(parent, width) // the Lospec tab is wider
                    onAboutToShow: {
                        x = root.sideX(parent, width)
                        y = 0
                    }
                    onLoadFileRequested: root.loadPaletteRequested()
                    onSaveFileRequested: root.savePaletteRequested()
                }
            }
            // The palette's own tools (left) apart from what acts on the selected swatch (right).
            Rectangle {
                visible: !root.paletteCollapsed
                Layout.preferredWidth: 1
                Layout.preferredHeight: 14
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                color: Theme.separator
            }
            IconButton {
                visible: !root.paletteCollapsed
                implicitWidth: 26
                implicitHeight: 26
                iconName: "plus"
                tip: "Add the primary color"
                onClicked: {
                    const i = root.editor.addPaletteColor(root.editor.primaryColor)
                    if (i >= 0)
                        root.selectedSwatch = i
                }
            }
            IconButton {
                visible: !root.paletteCollapsed
                implicitWidth: 26
                implicitHeight: 26
                iconName: "replace"
                tip: "Set the selected swatch to the primary color"
                enabled: root.selectedSwatch >= 0
                onClicked: root.editor.setPaletteColor(root.selectedSwatch, root.editor.primaryColor)
            }
            IconButton {
                visible: !root.paletteCollapsed
                implicitWidth: 26
                implicitHeight: 26
                iconName: "trash-2"
                tip: "Remove the selected swatch"
                enabled: root.selectedSwatch >= 0 && root.editor.palette.length > 1
                onClicked: root.editor.removePaletteColor(root.selectedSwatch)
            }
        }

        // Left click sets primary, right click sets secondary. Scrolls when
        // the panel is too short for the whole palette.
        Flickable {
            id: paletteView
            visible: !root.showTiles && !root.paletteCollapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: swatches.implicitHeight + 6
            Layout.minimumHeight: Math.min(swatches.implicitHeight + 6, 56)
            contentWidth: width
            contentHeight: swatches.implicitHeight + 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            // Inset by 3 px on each side: the selected swatch's ring reaches out that far.
            Flow {
                id: swatches
                x: 3
                y: 3
                width: parent.width - 6
                spacing: 4

                Repeater {
                    id: swatchRepeater
                    // A count, not the color list: swatches recolor in place.
                    model: root.swatchPool

                    Rectangle {
                        id: swatch
                        required property int index
                        readonly property color modelData: root.editor.palette[index] ?? "transparent"
                        visible: index < root.paletteSize
                        readonly property bool selected: index === root.selectedSwatch
                        readonly property bool inRange: root.editor.shadeRange.x >= 0 && index >= root.editor.shadeRange.x
                                                        && index <= root.editor.shadeRange.y
                        readonly property bool dropTarget: index === root.dropIndex && root.dragFrom !== index

                        width: 24; height: 24
                        radius: 6
                        color: modelData
                        border.color: dropTarget ? Theme.accent : inRange ? Theme.accent : Theme.swatchBorder
                        border.width: dropTarget || inRange ? 2 : 1
                        opacity: root.dragFrom === index ? 0.4 : 1
                        scale: swatchArea.containsMouse && root.dragFrom < 0 ? 1.1 : 1
                        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

                        // The selected swatch: a ring a little way out (as in the effect
                        // dialogs' color swatches), clear of its neighbors in the 4 px gaps.
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width + 6
                            height: parent.height + 6
                            radius: swatch.radius + 3
                            color: "transparent"
                            border.color: Theme.text
                            border.width: 1.5
                            opacity: swatch.selected ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                        }

                        // The shade range: a soft band behind its swatches (the gaps
                        // between them are 4 px, so neighbors join into one strip).
                        Rectangle {
                            visible: swatch.inRange
                            z: -1
                            anchors.fill: parent
                            anchors.margins: -2
                            radius: 8
                            color: Theme.accentSoft
                        }

                        // Left: primary (and select), right: secondary, drag: move.
                        MouseArea {
                            id: swatchArea
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            property point pressPos
                            onContainsMouseChanged: {
                                if (containsMouse)
                                    root.hoveredSwatch = swatch
                                else if (root.hoveredSwatch === swatch)
                                    root.hoveredSwatch = null
                            }
                            onPressed: mouse => pressPos = Qt.point(mouse.x, mouse.y)
                            onPositionChanged: mouse => {
                                if (!(pressedButtons & Qt.LeftButton))
                                    return
                                if (root.dragFrom < 0 && Math.hypot(mouse.x - pressPos.x, mouse.y - pressPos.y) < 4)
                                    return
                                root.dragFrom = swatch.index
                                const p = mapToItem(swatches, mouse.x, mouse.y)
                                // The (shown) swatch under the pointer.
                                for (let i = 0; i < root.paletteSize; ++i) {
                                    const s = swatchRepeater.itemAt(i)
                                    if (s && p.x >= s.x && p.y >= s.y && p.x < s.x + s.width && p.y < s.y + s.height) {
                                        root.dropIndex = i
                                        break
                                    }
                                }
                            }
                            onReleased: mouse => {
                                if (root.dragFrom >= 0) {
                                    if (root.dropIndex >= 0 && root.dropIndex !== root.dragFrom
                                            && root.editor.movePaletteColor(root.dragFrom, root.dropIndex))
                                        root.selectedSwatch = root.dropIndex
                                    root.dragFrom = -1
                                    root.dropIndex = -1
                                    return
                                }
                                if (mouse.button === Qt.RightButton) {
                                    root.editor.secondaryColor = swatch.modelData
                                } else if ((mouse.modifiers & Qt.ShiftModifier) && root.selectedSwatch >= 0) {
                                    // A range (the shading ink's ramp) from the selected swatch.
                                    root.editor.shadeRange = Qt.point(root.selectedSwatch, swatch.index)
                                } else {
                                    root.editor.primaryColor = swatch.modelData
                                    root.selectedSwatch = swatch.index
                                    root.editor.shadeRange = Qt.point(-1, -1)
                                }
                            }
                        }
                    }
                }
                AppToolTip {
                    parent: root.hoveredSwatch ?? swatches
                    text: "Click: primary \u00b7 right click: secondary \u00b7 Shift-click: shading range \u00b7 drag to reorder"
                    active: root.hoveredSwatch !== null && root.dragFrom < 0
                }
            }
        }
    }
}
