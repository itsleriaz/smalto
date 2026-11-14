pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// A classic layers list for working on one image, without the timeline: the
// layers top to bottom with eye, lock and alpha lock, groups that fold, drag
// to reorder, double-click to rename, a right-click menu, a thumbnail of each
// layer's cel, Shift-click to take several layers (a block, as in the timeline),
// and the current layer's blend and opacity above. It acts on the current frame, like the
// canvas. The timeline's layer column does the same things beside the frames.
Panel {
    id: root

    required property Editor editor
    required property CanvasView canvas // for the checkerboard behind the thumbnails

    // The tileset dialog belongs to the window.
    signal tilesetRequested(bool convert)

    // What the window arranges (PanelColumns), see ColorPanel.
    property real preferredHeight: 320
    readonly property real wantHeight: preferredHeight
    readonly property real minHeight: 220
    readonly property int shrinkRank: 1
    movable: true

    readonly property int rowHeight: 28
    readonly property var currentLayerData: editor.layers[editor.currentLayer] ?? ({ opacity: 100, blend: 0, background: true })

    // Layer whose name is being edited (-1: none).
    property int renamingLayer: -1
    // Layer drag: the row it would land on, and the zone (0 above, 1 below,
    // 2 into a group); -1: not dragging.
    property int dropRow: -1
    property int dropZone: 0
    // Where the pointer is in the list while a layer is dragged (scrolls it at the edges).
    property real dragViewY: 0

    // Thumbnails are made again after the pixels or the layers change.
    property int thumbRevision: 0
    Timer {
        id: thumbTimer
        interval: 150
        onTriggered: root.thumbRevision++
    }
    Connections {
        target: root.editor
        function onCanvasChanged() { if (!thumbTimer.running) thumbTimer.start() }
        function onLayersChanged() { thumbTimer.restart() }
        function onFramesChanged() { thumbTimer.restart() }
        function onCurrentFrameChanged() { thumbTimer.restart() }
        function onPaletteChanged() { thumbTimer.restart() }
        function onDocumentsChanged() { thumbTimer.restart() }
    }

    // Where a layer dragged to `dragViewY` (in the list's own coordinates)
    // would land: above / below a row, or into a group.
    function updateDrop() {
        const y = dragViewY + list.contentY
        const r = Math.max(0, Math.min(list.count - 1, Math.floor(y / rowHeight)))
        const frac = Math.max(0, Math.min(1, y / rowHeight - r))
        const target = editor.layers[layerModel.layerForRow(r)]
        dropRow = r
        dropZone = target && target.kind === "group"
            ? (frac < 0.3 ? 0 : frac > 0.7 ? 1 : 2)
            : (frac < 0.5 ? 0 : 1)
    }
    // Past the top or bottom of the list while dragging: it scrolls.
    Timer {
        interval: 30
        repeat: true
        running: root.dropRow >= 0
        onTriggered: {
            const edge = 26
            let v = 0
            if (root.dragViewY < edge)
                v = -(edge - root.dragViewY) / 2
            else if (root.dragViewY > list.height - edge)
                v = (root.dragViewY - (list.height - edge)) / 2
            if (v === 0)
                return
            list.contentY = Math.max(0, Math.min(list.contentHeight - list.height, list.contentY + v))
            root.updateDrop()
        }
    }

    padding: 10
    clipContent: true

    TimelineModel {
        id: layerModel
        editor: root.editor
    }

    LayerMenu {
        id: layerMenu
        editor: root.editor
        onRenameRequested: index => root.renamingLayer = index
        onTilesetRequested: convert => root.tilesetRequested(convert)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            text: "Layers"
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
        }

        // Blend mode and opacity of the current layer (fixed for a background).
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            AppComboBox {
                Layout.fillWidth: true
                model: root.editor.blendModes()
                currentIndex: root.currentLayerData.blend
                enabled: !root.currentLayerData.background
                onActivated: index => root.editor.setLayerBlend(root.editor.currentLayer, index)
            }
            NumberControl {
                value: root.currentLayerData.opacity
                enabled: !root.currentLayerData.background
                opacity: enabled ? 1 : 0.4
                from: 0
                to: 100
                bigStep: 10
                suffix: "%"
                fieldWidth: 48
                tip: "Layer opacity"
                onMoved: v => root.editor.setLayerOpacity(root.editor.currentLayer, v)
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: layerModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            // Keeps the current layer in view when it changes elsewhere.
            readonly property int currentRow: layerModel.rowForLayer(root.editor.currentLayer)
            onCurrentRowChanged: if (currentRow >= 0) positionViewAtIndex(currentRow, ListView.Contain)

            // A right click under the rows: the menu of the current layer.
            // On the view, not its contentItem (rows-sized), and behind it.
            MouseArea {
                parent: list
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.RightButton
                onPressed: {
                    layerMenu.layerIndex = root.editor.currentLayer
                    layerMenu.popup()
                }
            }

            // Where a dragged layer will land: a line between rows, or the group.
            Rectangle {
                parent: list.contentItem
                z: 10
                visible: root.dropRow >= 0
                width: list.width
                y: root.dropZone === 2 ? root.dropRow * root.rowHeight
                   : (root.dropRow + (root.dropZone === 1 ? 1 : 0)) * root.rowHeight - 1
                height: root.dropZone === 2 ? root.rowHeight : 2
                radius: root.dropZone === 2 ? 4 : 1
                color: root.dropZone === 2 ? Theme.accentSoft : Theme.accent
                border.color: Theme.accent
                border.width: root.dropZone === 2 ? 1 : 0
            }

            delegate: Rectangle {
                id: row
                required property int index
                required property int layerIndex
                required property string name
                required property string kind
                required property int depth
                required property bool layerVisible
                required property bool shown
                required property bool locked
                required property bool alphaLock
                required property bool collapsed
                required property int layerOpacity
                required property string blendName
                readonly property bool current: layerIndex === root.editor.currentLayer
                // Part of a block taken with Shift-click (the timeline's cel range).
                readonly property bool inBlock: {
                    void root.editor.range
                    return (root.editor.rangeType === Editor.CelRange && root.editor.inRange(layerIndex, root.editor.currentFrame))
                           || root.editor.isPicked(layerIndex)
                }
                readonly property bool renaming: root.renamingLayer === layerIndex

                width: list.width
                height: root.rowHeight
                radius: Theme.radiusSmall
                color: current || inBlock ? Theme.accentSoft : area.containsMouse ? Theme.hover : "transparent"

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    preventStealing: true // drags reorder layers, not scroll
                    property real pressY
                    property bool dragging: false
                    // A plain left click on a layer of the block picks just that layer, when
                    // released without a drag (a drag moves the whole block).
                    property bool pickOnRelease: false
                    onPressed: mouse => {
                        if ((mouse.modifiers & Qt.ControlModifier) && mouse.button === Qt.LeftButton) {
                            // Cmd / Ctrl: this layer in or out of the selection, whatever is next to it.
                            root.editor.togglePick(row.layerIndex)
                            pickOnRelease = false
                        } else if ((mouse.modifiers & Qt.ShiftModifier) && mouse.button === Qt.LeftButton) {
                            // From the current layer to this one, on the current frame.
                            root.editor.selectLayerBlock(root.editor.currentLayer, row.layerIndex)
                        } else {
                            // A click inside the block keeps it (a drag then moves it all, and
                            // the right-click menu acts on all of it).
                            pickOnRelease = row.inBlock && mouse.button === Qt.LeftButton
                            if (!row.inBlock) {
                                if (root.editor.rangeType === Editor.CelRange)
                                    root.editor.clearRange()
                                root.editor.currentLayer = row.layerIndex
                            }
                        }
                        pressY = mouse.y
                        dragging = false
                        if (mouse.button === Qt.RightButton) {
                            layerMenu.layerIndex = row.layerIndex
                            layerMenu.popup()
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || (pressedButtons & Qt.RightButton))
                            return
                        if (!dragging && Math.abs(mouse.y - pressY) < 4)
                            return
                        dragging = true
                        root.dragViewY = mapToItem(list, mouse.x, mouse.y).y
                        root.updateDrop()
                    }
                    onReleased: {
                        if (dragging && root.dropRow >= 0)
                            root.editor.moveLayer(row.layerIndex, layerModel.layerForRow(root.dropRow), root.dropZone)
                        else if (pickOnRelease && !dragging) {
                            root.editor.clearRange()
                            root.editor.currentLayer = row.layerIndex
                        }
                        pickOnRelease = false
                        dragging = false
                        root.dropRow = -1
                    }
                    onCanceled: {
                        dragging = false
                        root.dropRow = -1
                    }
                    onDoubleClicked: root.renamingLayer = row.layerIndex
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 2 + row.depth * 12
                    anchors.rightMargin: 4
                    spacing: 0

                    IconButton {
                        implicitWidth: 18
                        implicitHeight: 24
                        visible: row.kind === "group"
                        iconName: "chevron-down"
                        rotation: row.collapsed ? -90 : 0
                        tip: row.collapsed ? "Expand group" : "Collapse group"
                        onClicked: root.editor.setLayerCollapsed(row.layerIndex, !row.collapsed)
                    }
                    Item { // keeps names aligned between groups and layers
                        implicitWidth: 18
                        implicitHeight: 24
                        visible: row.kind !== "group"
                    }
                    // The layer's cel on this frame (groups show a folder, tilemaps a grid).
                    Item {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 22
                        Layout.rightMargin: 4
                        // Image layers, tilemaps and groups all show what they draw on this frame.
                        ImagePreview {
                            anchors.fill: parent
                            opacity: row.shown ? 1 : 0.4
                            checkerLight: root.canvas.checkerA
                            checkerDark: root.canvas.checkerB
                            image: { void root.thumbRevision; return root.editor.layerImage(row.layerIndex, 48) }
                        }
                        // What kind it is: a small mark in the corner of a group or a tilemap.
                        IconButton {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            visible: row.kind !== "image"
                            enabled: false
                            implicitWidth: 13
                            implicitHeight: 13
                            iconName: row.kind === "group" ? "folder-open" : row.kind === "particles" ? "wand-sparkles" : "grid-2x2"
                        }
                    }
                    Text {
                        visible: !row.renaming
                        Layout.fillWidth: true
                        Layout.minimumWidth: 40
                        text: row.name
                        elide: Text.ElideRight
                        color: row.shown ? Theme.text : Theme.textFaint
                        font.pixelSize: Theme.fontSize
                        font.weight: row.kind === "group" ? Font.DemiBold : Font.Normal
                    }
                    AppTextField {
                        id: nameField
                        visible: row.renaming
                        Layout.fillWidth: true
                        compact: true
                        text: row.name
                        onVisibleChanged: if (visible) { text = row.name; forceActiveFocus(); selectAll() }
                        onAccepted: row.finishRename(true)
                        onActiveFocusChanged: if (!activeFocus && row.renaming) row.finishRename(true)
                        Keys.onEscapePressed: row.finishRename(false)
                    }
                    // Non-default blend / opacity.
                    Text {
                        visible: !row.renaming && list.width > 270 && (row.layerOpacity < 100 || row.blendName !== "Normal") // in a narrow list the header shows it
                        Layout.leftMargin: 4
                        Layout.maximumWidth: 74
                        elide: Text.ElideRight
                        text: (row.blendName !== "Normal" ? row.blendName : "")
                              + (row.blendName !== "Normal" && row.layerOpacity < 100 ? " · " : "")
                              + (row.layerOpacity < 100 ? row.layerOpacity + "%" : "")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall - 1
                        font.features: { "tnum": 1 }
                    }
                    // Alpha lock (image layers); the layer menu names it.
                    Rectangle {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        Layout.leftMargin: 2
                        radius: 4
                        visible: row.kind === "image"
                        color: row.alphaLock ? Theme.accentSoft : alphaArea.containsMouse ? Theme.hover : "transparent"
                        opacity: row.alphaLock || area.containsMouse || alphaArea.containsMouse ? 1 : 0.35
                        Text {
                            anchors.centerIn: parent
                            text: "α"
                            color: row.alphaLock ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSize
                            font.weight: row.alphaLock ? Font.DemiBold : Font.Normal
                        }
                        MouseArea {
                            id: alphaArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.editor.setLayerAlphaLock(row.layerIndex, !row.alphaLock)
                        }
                    }
                    IconButton {
                        implicitWidth: 24
                        implicitHeight: 24
                        iconName: row.locked ? "lock" : "lock-open"
                        opacity: row.locked || area.containsMouse || hovered ? 1 : 0.35
                        tip: row.locked ? "Unlock layer" : "Lock layer"
                        onClicked: root.editor.setLayerLocked(row.layerIndex, !row.locked)
                    }
                    IconButton {
                        implicitWidth: 24
                        implicitHeight: 24
                        iconName: row.layerVisible ? "eye" : "eye-off"
                        tip: row.layerVisible ? "Hide layer" : "Show layer"
                        onClicked: root.editor.setLayerVisible(row.layerIndex, !row.layerVisible)
                    }
                }

                function finishRename(commit) {
                    if (!renaming)
                        return
                    root.renamingLayer = -1
                    const text = nameField.text.trim()
                    if (commit && text !== "" && text !== name)
                        root.editor.setLayerName(layerIndex, text)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            IconButton {
                iconName: "plus"
                tip: "New layer" + Shortcuts.hints.newLayer
                onClicked: root.editor.addLayer()
            }
            IconButton {
                iconName: "folder-plus"
                tip: "New group"
                onClicked: root.editor.addGroup()
            }
            IconButton {
                iconName: "copy"
                tip: "Duplicate layer"
                onClicked: root.editor.duplicateLayer()
            }
            Item { Layout.fillWidth: true }
            IconButton {
                iconName: "trash-2"
                tip: "Delete layer"
                enabled: root.editor.canRemoveLayer
                onClicked: root.editor.removeLayer()
            }
        }
    }
}
