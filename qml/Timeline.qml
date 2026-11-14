pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Layers x frames grid, Aseprite style: layers as rows on the left (topmost
// first), frames as columns, a dot per cel, tags as bars above the frame
// numbers. The frame numbers, layer column and cel grid are TableViews
// synced to the cel grid, so they scroll together.
//
// Frame numbers: press and drag to select frames (the current frame
// follows), Shift extends, dragging a selection moves the frames.
// Cels: press and drag to select a block, Shift extends, dragging a
// selection moves it (Alt copies). Right click for frame / cel / layer /
// tag actions.
Panel {
    id: root

    required property Editor editor
    // Upper bound for resizing; set by the window.
    property real maximumHeight: 600
    property real preferredHeight: 220

    readonly property int rowHeight: 26
    readonly property int cellWidth: 24
    // The layer column (names, eye, lock) can be put away to leave the room to
    // the frames, e.g. when the layers list is on.
    property bool layerColumnShown: true
    readonly property int layerColumnWidth: layerColumnShown ? 210 : 0
    readonly property int headerHeight: 22
    readonly property int laneHeight: 20
    readonly property int currentRow: timelineModel.rowForLayer(editor.currentLayer)

    // Timeline range, as plain values for bindings.
    readonly property bool frameRange: editor.rangeType === Editor.FrameRange
    readonly property bool celRange: editor.rangeType === Editor.CelRange
    readonly property rect range: editor.range

    // The tag bar under the pointer, for the one tooltip they share.
    property MouseArea hoveredTagArea: null
    property int hoveredTagIndex: -1
    // The current layer's blend mode and opacity; the classic layers list has its own, so the window hides these then.
    property bool layerProps: true
    // Tag bars are kept (hidden) when a document with fewer tags comes in.
    property int tagPool: 0
    readonly property int tagCount: editor.tags.length
    onTagCountChanged: tagPool = Math.max(tagPool, tagCount)

    // Layer whose name is being edited (-1: none).
    property int renamingLayer: -1

    // Current layer's properties, for the toolbar.
    readonly property var currentLayerData: editor.layers[editor.currentLayer] ?? ({ opacity: 100, blend: 0, background: true })

    // Layer drag: where the dragged layer would land (row, and zone:
    // 0 above, 1 below, 2 into a group). dropRow -1: not dragging.
    property int dropRow: -1
    property int dropZone: 0

    // Any press on tags, frames, layers or cels (e.g. to route Ctrl+C to
    // frames / cels).
    signal interacted()
    // The layer menu wants a tileset dialog (the window has it).
    signal tilesetRequested(bool convert)

    implicitHeight: Math.min(preferredHeight, maximumHeight)
    padding: 8

    dockEdge: Qt.TopEdge

    TimelineModel {
        id: timelineModel
        editor: root.editor
    }

    function frameInRange(frame) {
        return editor.rangeType !== Editor.NoRange && frame >= range.x && frame < range.x + range.width
    }
    function celInRange(layerIndex, frame) {
        return frameInRange(frame) && layerIndex >= range.y && layerIndex < range.y + range.height
    }
    // Scrolls the cel grid (the frame numbers and layer column follow),
    // kept inside its content.
    function scrollTo(x: real, y: real) {
        cels.contentX = Math.max(0, Math.min(cels.contentWidth - cels.width, x))
        cels.contentY = Math.max(0, Math.min(cels.contentHeight - cels.height, y))
    }

    // Dragging near an edge of the grid scrolls it, faster the closer to the
    // edge (or past it). `pos` is the pointer in the grid view's coordinates;
    // `move` re-runs the drag there after each step so its target follows.
    property point autoScrollPos
    property var autoScrollMove: null
    readonly property int autoScrollEdge: 24
    function autoScroll(pos: point, move) {
        autoScrollPos = pos
        autoScrollMove = move
        const e = autoScrollEdge
        const near = pos.x < e || pos.y < e || pos.x > cels.width - e || pos.y > cels.height - e
        if (near && !autoScrollTimer.running)
            autoScrollTimer.start()
        else if (!near)
            autoScrollTimer.stop()
    }
    function stopAutoScroll() {
        autoScrollTimer.stop()
        autoScrollMove = null
    }
    Timer {
        id: autoScrollTimer
        interval: 30
        repeat: true
        onTriggered: {
            const p = root.autoScrollPos, e = root.autoScrollEdge
            const speed = d => Math.min(30, 2 + (e - d) / 2) // d: distance in from the edge
            const dx = p.x < e ? -speed(p.x) : p.x > cels.width - e ? speed(cels.width - p.x) : 0
            const dy = p.y < e ? -speed(p.y) : p.y > cels.height - e ? speed(cels.height - p.y) : 0
            const x = cels.contentX, y = cels.contentY
            root.scrollTo(x + dx, y + dy)
            if ((cels.contentX !== x || cels.contentY !== y) && root.autoScrollMove)
                root.autoScrollMove()
        }
    }

    // Middle-button drag anywhere in the timeline pans it. Only the middle
    // button, so the cels' and layers' own left / right handling is untouched.
    DragHandler {
        id: middlePan
        target: null
        acceptedButtons: Qt.MiddleButton
        dragThreshold: 0
        cursorShape: Qt.ClosedHandCursor // shown while dragging
        property point start
        onActiveChanged: if (active) start = Qt.point(cels.contentX, cels.contentY)
        onTranslationChanged: if (active) root.scrollTo(start.x - translation.x, start.y - translation.y)
    }

    function revealCurrent() {
        if (cels.rows > 0 && cels.columns > 0)
            cels.positionViewAtCell(Qt.point(editor.currentFrame, Math.max(0, currentRow)), TableView.Contain)
    }
    Connections {
        target: root.editor
        function onCurrentFrameChanged() { Qt.callLater(root.revealCurrent) }
        function onCurrentLayerChanged() { Qt.callLater(root.revealCurrent) }
    }

    // Tags stacked in lanes so overlapping ones don't cover each other:
    // by start frame, and on a tie the longer (outer) tag first, so nested
    // tags sit below the tag they're in.
    readonly property var tagLanes: {
        const tags = editor.tags
        const order = tags.map((t, i) => i).sort((a, b) => tags[a].from - tags[b].from
                                                         || (tags[b].to - tags[b].from) - (tags[a].to - tags[a].from))
        const laneEnds = []
        const lanes = new Array(tags.length)
        for (const i of order) {
            let lane = laneEnds.findIndex(end => end < tags[i].from)
            if (lane < 0) {
                lane = laneEnds.length
                laneEnds.push(0)
            }
            laneEnds[lane] = tags[i].to
            lanes[i] = lane
        }
        return { lanes: lanes, count: Math.max(1, laneEnds.length) }
    }

    component Separator: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 18
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        color: Theme.separator
    }

    // Drag the top edge to resize.
    MouseArea {
        x: 0
        y: -root.padding - 4
        width: parent.width
        height: 8
        cursorShape: Qt.SizeVerCursor
        property real startY
        property real startHeight
        onPressed: mouse => {
            startY = mapToItem(null, mouse.x, mouse.y).y
            startHeight = root.implicitHeight
        }
        onPositionChanged: mouse => {
            const y = mapToItem(null, mouse.x, mouse.y).y
            root.preferredHeight = Math.max(110, Math.min(root.maximumHeight, startHeight + startY - y))
        }
    }

    // --- menus ---------------------------------------------------------------

    AppMenu {
        id: frameMenu
        AppMenuItem { text: "New Frame"; hint: Shortcuts.labels.newFrame; onTriggered: root.editor.addFrame() }
        AppMenuItem { text: root.frameRange && root.range.width > 1 ? "Duplicate Frames" : "Duplicate Frame"; hint: Shortcuts.labels.duplicateFrame; onTriggered: root.editor.duplicateFrame() }
        AppMenuItem { text: "Reverse Frames"; enabled: root.frameRange && root.range.width > 1; onTriggered: root.editor.reverseFrames() }
        AppMenuItem {
            text: root.frameRange && root.range.width > 1 ? "Frame Duration (" + root.range.width + " frames)\u2026" : "Frame Duration\u2026"
            onTriggered: durationPopup.openAt(frameMenu.x, frameMenu.y)
        }
        AppMenuSeparator {}
        AppMenuItem { text: "Cut"; hint: Shortcuts.labels.cut; onTriggered: root.editor.cutTimeline() }
        AppMenuItem { text: "Copy"; hint: Shortcuts.labels.copy; onTriggered: root.editor.copyTimeline() }
        AppMenuItem { text: "Paste"; hint: Shortcuts.labels.paste; enabled: root.editor.canPasteTimeline; onTriggered: root.editor.pasteTimeline() }
        AppMenuSeparator {}
        AppMenuItem { text: "New Tag from Frames"; onTriggered: root.editor.addTag() }
        AppMenuSeparator {}
        AppMenuItem {
            danger: true
            text: root.frameRange && root.range.width > 1 ? "Delete Frames" : "Delete Frame"
            enabled: (root.frameRange ? root.range.width : 1) < root.editor.frameCount
            onTriggered: root.editor.removeFrame()
        }
    }

    AppMenu {
        id: celMenu
        AppMenuItem { text: "Link Cels"; enabled: root.range.width > 1; onTriggered: root.editor.linkCels() }
        AppMenuItem { text: "Unlink Cels"; onTriggered: root.editor.unlinkCels() }
        AppMenuSeparator {}
        AppMenuItem { text: "Cut"; hint: Shortcuts.labels.cut; onTriggered: root.editor.cutTimeline() }
        AppMenuItem { text: "Copy"; hint: Shortcuts.labels.copy; onTriggered: root.editor.copyTimeline() }
        AppMenuItem { text: "Paste"; hint: Shortcuts.labels.paste; enabled: root.editor.canPasteTimeline; onTriggered: root.editor.pasteTimeline() }
        AppMenuSeparator {}
        AppMenuItem { text: "New Tag from Frames"; onTriggered: root.editor.addTag() }
        AppMenuSeparator {}
        AppMenuItem { text: "Clear Cels"; danger: true; onTriggered: root.editor.clearCels() }
    }

    // A Smear group's key (right-click on its diamond).
    AppMenu {
        id: keyMenu
        property int frame: -1
        readonly property int ease: ((root.editor.smearState.keyList ?? []).find(k => k.frame === keyMenu.frame) ?? {}).ease ?? 0
        AppMenuItem { text: "Easing: Linear"; hint: keyMenu.ease === 0 ? "\u2713" : ""; onTriggered: root.editor.setSmearKeyEase(keyMenu.frame, 0) }
        AppMenuItem { text: "Easing: In"; hint: keyMenu.ease === 1 ? "\u2713" : ""; onTriggered: root.editor.setSmearKeyEase(keyMenu.frame, 1) }
        AppMenuItem { text: "Easing: Out"; hint: keyMenu.ease === 2 ? "\u2713" : ""; onTriggered: root.editor.setSmearKeyEase(keyMenu.frame, 2) }
        AppMenuItem { text: "Easing: In-out"; hint: keyMenu.ease === 3 ? "\u2713" : ""; onTriggered: root.editor.setSmearKeyEase(keyMenu.frame, 3) }
        AppMenuItem { text: "Easing: Custom"; hint: keyMenu.ease === 4 ? "\u2713" : "(curve in the panel)"; onTriggered: root.editor.setSmearKeyEase(keyMenu.frame, 4) }
        AppMenuSeparator {}
        AppMenuItem { text: "Copy Pose"; onTriggered: root.editor.copySmearPose(keyMenu.frame) }
        AppMenuItem { text: "Paste Pose"; enabled: root.editor.canPasteSmearPose; onTriggered: root.editor.pasteSmearPose(keyMenu.frame) }
        AppMenuSeparator {}
        AppMenuItem { text: "Remove Key"; danger: true; hint: "Delete"; onTriggered: root.editor.removeSmearKey(keyMenu.frame) }
    }

    LayerMenu {
        id: layerMenu
        editor: root.editor
        onRenameRequested: index => root.renamingLayer = index
        onTilesetRequested: convert => root.tilesetRequested(convert)
    }

    AppMenu {
        id: tagMenu
        property int tagIndex: -1
        AppMenuItem { text: "Edit Tag…"; onTriggered: tagEditor.openFor(tagMenu.tagIndex) }
        AppMenuSeparator {}
        AppMenuItem { text: "Delete Tag"; danger: true; onTriggered: root.editor.removeTag(tagMenu.tagIndex) }
    }

    TagEditor {
        id: tagEditor
        editor: root.editor
    }

    // Exact duration for the selected frames, from the frame menu.
    Popup {
        id: durationPopup
        padding: 10
        background: Panel { solid: true }
        function openAt(x, y) {
            this.x = x
            this.y = y - 60
            open()
            durationField.startEditing()
        }
        contentItem: RowLayout {
            spacing: 6
            Text {
                text: root.frameRange && root.range.width > 1 ? "Duration of " + root.range.width + " frames" : "Frame duration"
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            NumberControl {
                id: durationField
                value: root.editor.frameDuration
                from: 1
                to: 65535
                step: 10
                bigStep: 100
                suffix: " ms"
                fieldWidth: 80
                onMoved: v => root.editor.setFramesDuration(v)
                onEditingChanged: if (!editing) durationPopup.close()
            }
        }
    }

    // Onion skin settings.
    Popup {
        id: onionPopup
        padding: 12
        background: Panel { solid: true }

        component Stepper: RowLayout {
            id: stepper
            property string label
            property alias value: number.value
            property alias step: number.step
            property alias from: number.from
            property alias to: number.to
            property alias suffix: number.suffix
            signal changed(int value)
            spacing: 2
            Text {
                text: stepper.label
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
                Layout.preferredWidth: 64
            }
            NumberControl {
                id: number
                buttons: true
                fieldWidth: 44
                onMoved: v => stepper.changed(v)
            }
        }

        contentItem: ColumnLayout {
            spacing: 6
            Text {
                text: "Onion skin"
                color: Theme.text
                font.pixelSize: Theme.fontSize
                font.weight: Font.DemiBold
                Layout.bottomMargin: 2
            }
            Stepper {
                label: "Before"
                value: root.editor.onionBefore
                onChanged: v => root.editor.onionBefore = v
            }
            Stepper {
                label: "After"
                value: root.editor.onionAfter
                onChanged: v => root.editor.onionAfter = v
            }
            Stepper {
                label: "Opacity"
                value: root.editor.onionOpacity
                step: 10
                from: 10
                to: 100
                suffix: "%"
                onChanged: v => root.editor.onionOpacity = v
            }
            RowLayout {
                Text {
                    text: "Tint"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize
                    Layout.preferredWidth: 64
                }
                Segmented {
                    model: ["Red / blue", "Off"]
                    currentIndex: root.editor.onionTint ? 0 : 1
                    onActivated: index => root.editor.onionTint = index === 0
                }
            }
            RowLayout {
                Text {
                    text: "Drawn"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize
                    Layout.preferredWidth: 64
                }
                Segmented {
                    model: ["Behind", "In front"]
                    currentIndex: root.editor.onionFront ? 1 : 0
                    onActivated: index => root.editor.onionFront = index === 1
                }
            }
            OptionPill {
                text: "Loop within the tag"
                tip: "In a tag, the frames before its first and after its last are the ones at its other end, as it plays"
                checked: root.editor.onionLoop
                onToggled: root.editor.onionLoop = !root.editor.onionLoop
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Playback and frame/layer actions.
        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            IconButton {
                iconName: "chevron-left"
                tip: "Previous frame (Left)"
                onClicked: root.editor.previousFrame()
            }
            IconButton {
                iconName: root.editor.playing ? "pause" : "play"
                tip: "Play / pause (Enter)"
                enabled: root.editor.frameCount > 1
                onClicked: root.editor.playing = !root.editor.playing
            }
            IconButton {
                iconName: "chevron-right"
                tip: "Next frame (Right)"
                onClicked: root.editor.nextFrame()
            }
            Separator {}

            IconButton {
                iconName: "plus"
                tip: "New frame" + Shortcuts.hints.newFrame
                onClicked: root.editor.addFrame()
            }
            IconButton {
                iconName: "copy"
                tip: "Duplicate frames" + Shortcuts.hints.duplicateFrame
                onClicked: root.editor.duplicateFrame()
            }
            IconButton {
                iconName: "trash-2"
                tip: "Delete frames"
                enabled: (root.frameRange ? root.range.width : 1) < root.editor.frameCount
                onClicked: root.editor.removeFrame()
            }
            NumberControl {
                Layout.leftMargin: 4
                value: root.editor.frameDuration
                from: 1
                to: 65535
                step: 10
                bigStep: 100
                suffix: " ms"
                fieldWidth: 70
                tip: root.frameRange && root.range.width > 1 ? "Duration of the selected frames" : "Frame duration"
                onMoved: v => root.editor.setFramesDuration(v)
            }

            Separator {}

            OptionPill {
                text: "Onion skin"
                tip: "Show the frames around this one" + Shortcuts.hints.onionSkin
                checked: root.editor.onionSkin
                onToggled: root.editor.onionSkin = !root.editor.onionSkin
            }
            IconButton {
                implicitWidth: 18
                iconName: "chevron-down"
                tip: "Onion skin settings"
                active: onionPopup.visible
                onClicked: {
                    onionPopup.x = mapToItem(onionPopup.parent, 0, 0).x
                    onionPopup.y = mapToItem(onionPopup.parent, 0, 0).y - onionPopup.implicitHeight - 6
                    onionPopup.open()
                }
            }
            // Last before the gap: its width changes while frames play, and
            // nothing should move with it.
            Text {
                Layout.leftMargin: 8
                text: "Frame " + (root.editor.currentFrame + 1) + " / " + root.editor.frameCount
                      + (root.editor.rangeType !== Editor.NoRange && root.range.width > 1
                         ? "  ·  " + root.range.width + " selected" : "")
                      + (root.editor.frameCount === 1 ? "  ·  add a frame to animate" : "")
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                font.features: { "tnum": 1 }
            }

            Item { Layout.fillWidth: true }

            IconButton {
                iconName: "panel-left"
                active: !root.layerColumnShown
                tip: root.layerColumnShown ? "Hide the layer column (frames only)" : "Show the layer column"
                onClicked: root.layerColumnShown = !root.layerColumnShown
            }
            Text {
                visible: root.layerProps
                text: "Layer"
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                Layout.rightMargin: 4
            }
            // Blend mode and opacity of the current layer (fixed for a background).
            AppComboBox {
                visible: root.layerProps
                implicitWidth: 120
                model: root.editor.blendModes()
                currentIndex: root.currentLayerData.blend
                enabled: !root.currentLayerData.background
                onActivated: index => root.editor.setLayerBlend(root.editor.currentLayer, index)
            }
            NumberControl {
                visible: root.layerProps
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
            Separator {}
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
                iconName: "trash-2"
                tip: "Delete layer"
                enabled: root.editor.canRemoveLayer
                onClicked: root.editor.removeLayer()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 0
            columnSpacing: 0

            // --- tags row ------------------------------------------------------

            Text {
                Layout.preferredWidth: root.layerColumnWidth
                Layout.preferredHeight: tagsStrip.Layout.preferredHeight
                leftPadding: 8
                verticalAlignment: Text.AlignVCenter
                text: "Tags"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }
            Item {
                id: tagsStrip
                Layout.fillWidth: true
                // One full lane per level of overlap (a layout ignores `height`).
                Layout.preferredHeight: root.tagLanes.count * root.laneHeight + 4
                Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.durationFast } }
                clip: true

                Text {
                    visible: root.editor.tags.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    x: 6
                    text: "Select frames, right click → New Tag from Frames"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall - 1
                }

                AppToolTip {
                    parent: root.hoveredTagArea ?? root
                    readonly property var hovered: root.editor.tags[root.hoveredTagIndex]
                    text: (hovered ? "<b>" + hovered.name.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b> \u00b7 frames "
                                     + (hovered.from + 1) + "\u2013" + (hovered.to + 1) + "<br>" : "")
                          + "Drag the ends to resize, the middle to move \u00b7 double-click to edit"
                    active: root.hoveredTagArea !== null && !root.hoveredTagArea.pressed
                }
                Repeater {
                    model: root.tagPool
                    delegate: Rectangle {
                        id: tagBar
                        required property int index
                        readonly property var modelData: root.editor.tags[index]
                            ?? ({ name: "", from: 0, to: 0, color: "transparent", direction: 0, repeat: 0 })
                        visible: index < root.tagCount
                        readonly property bool dark: modelData.color.hslLightness < 0.6
                        // Frames shown; differ from the tag's while it's dragged.
                        property int shownFrom: modelData.from
                        property int shownTo: modelData.to

                        x: shownFrom * root.cellWidth - header.contentX + 1
                        y: (root.tagLanes.lanes[index] ?? 0) * root.laneHeight + 2
                        width: (shownTo - shownFrom + 1) * root.cellWidth - 2
                        height: root.laneHeight - 2
                        radius: 4
                        color: modelData.color
                        opacity: tagArea.containsMouse || tagArea.mode !== "" ? 1 : 0.85
                        // The tag being played.
                        border.width: root.editor.playingTag === index ? 2 : 0
                        border.color: dark ? "white" : "#141417"

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            anchors.rightMargin: 4
                            verticalAlignment: Text.AlignVCenter
                            text: tagBar.modelData.name
                                  + (tagBar.modelData.direction === 1 ? "  \u2190"
                                     : tagBar.modelData.direction >= 2 ? "  \u21c4" : "")
                                  + (tagBar.modelData.repeat > 0 ? "  \u00d7" + tagBar.modelData.repeat : "")
                            elide: Text.ElideRight
                            color: tagBar.dark ? "white" : "#141417"
                            font.pixelSize: Theme.fontSize
                            font.weight: Font.DemiBold
                        }

                        // A tag narrower than its label shows the whole label
                        // over the neighboring frames while the pointer is on it.
                        Rectangle {
                            visible: tagArea.containsMouse && fullLabel.implicitWidth + 10 > tagBar.width
                            z: 5
                            width: fullLabel.implicitWidth + 10
                            height: parent.height
                            radius: parent.radius
                            color: tagBar.modelData.color
                            border.width: 1
                            border.color: Qt.rgba(0, 0, 0, 0.25)
                            Text {
                                id: fullLabel
                                anchors.verticalCenter: parent.verticalCenter
                                x: 5
                                text: tagBar.modelData.name
                                      + (tagBar.modelData.direction === 1 ? "  \u2190"
                                         : tagBar.modelData.direction >= 2 ? "  \u21c4" : "")
                                      + (tagBar.modelData.repeat > 0 ? "  \u00d7" + tagBar.modelData.repeat : "")
                                color: tagBar.dark ? "white" : "#141417"
                                font.pixelSize: Theme.fontSize
                                font.weight: Font.DemiBold
                            }
                        }

                        // Ends resize the tag, the middle moves it; a click
                        // selects its frames, a double-click edits it.
                        MouseArea {
                            id: tagArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            property string mode: "" // "left", "right", "move" while dragging
                            property real pressX // where it was pressed, in the strip's view and the grid's scroll then
                            property real pressContentX
                            property int startFrom
                            property int startTo
                            property bool dragged: false

                            // The drag with the pointer at `viewX` in the strip (the grid may have scrolled since).
                            function dragTo(viewX: real) {
                                const d = Math.round((viewX + cels.contentX - pressContentX) / root.cellWidth)
                                if (d === 0 && !dragged)
                                    return
                                dragged = true
                                const last = root.editor.frameCount - 1
                                if (mode === "left") {
                                    tagBar.shownFrom = Math.max(0, Math.min(startTo, startFrom + d))
                                } else if (mode === "right") {
                                    tagBar.shownTo = Math.max(startFrom, Math.min(last, startTo + d))
                                } else {
                                    const shift = Math.max(-startFrom, Math.min(last - startTo, d))
                                    tagBar.shownFrom = startFrom + shift
                                    tagBar.shownTo = startTo + shift
                                }
                            }

                            function edgeAt(x) {
                                const grip = Math.min(6, width / 3)
                                return x < grip ? "left" : x > width - grip ? "right" : "move"
                            }
                            cursorShape: {
                                const edge = mode !== "" ? mode : edgeAt(mouseX)
                                return edge === "move" ? (dragged ? Qt.ClosedHandCursor : Qt.PointingHandCursor)
                                                       : Qt.SizeHorCursor
                            }

                            onContainsMouseChanged: {
                                if (containsMouse) {
                                    root.hoveredTagIndex = tagBar.index
                                    root.hoveredTagArea = tagArea
                                } else if (root.hoveredTagArea === tagArea)
                                    root.hoveredTagArea = null
                            }
                            onPressed: mouse => {
                                root.interacted()
                                root.editor.playing = false
                                root.editor.selectFrames(tagBar.modelData.from, tagBar.modelData.to)
                                if (root.editor.currentFrame < tagBar.modelData.from || root.editor.currentFrame > tagBar.modelData.to)
                                    root.editor.currentFrame = tagBar.modelData.from
                                if (mouse.button === Qt.RightButton) {
                                    tagMenu.tagIndex = tagBar.index
                                    tagMenu.popup()
                                    return
                                }
                                mode = edgeAt(mouse.x)
                                pressX = mapToItem(tagsStrip, mouse.x, 0).x // the bar itself moves
                                pressContentX = pressX + cels.contentX
                                startFrom = tagBar.modelData.from
                                startTo = tagBar.modelData.to
                                dragged = false
                            }
                            onPositionChanged: mouse => {
                                if (!pressed || mode === "")
                                    return
                                const viewX = mapToItem(tagsStrip, mouse.x, 0).x
                                dragTo(viewX)
                                // Near an end of the strip: it scrolls, and the tag follows the pointer.
                                root.autoScroll(Qt.point(viewX, cels.height / 2), () => tagArea.dragTo(viewX))
                            }
                            onCanceled: root.stopAutoScroll()
                            onReleased: {
                                root.stopAutoScroll()
                                if (mode !== "" && dragged) {
                                    const from = tagBar.shownFrom
                                    const to = tagBar.shownTo
                                    // Back to following the tag (the edit may be a no-op).
                                    tagBar.shownFrom = Qt.binding(() => tagBar.modelData.from)
                                    tagBar.shownTo = Qt.binding(() => tagBar.modelData.to)
                                    root.editor.setTag(tagBar.index, { from: from, to: to })
                                    root.editor.selectFrames(from, to)
                                }
                                mode = ""
                                dragged = false
                            }
                            onDoubleClicked: tagEditor.openFor(tagBar.index)
                        }
                    }
                }
            }

            // --- frame numbers -------------------------------------------------

            Item {
                Layout.preferredWidth: root.layerColumnWidth
                Layout.preferredHeight: root.headerHeight
            }

            TableView {
                id: header
                Layout.fillWidth: true
                Layout.preferredHeight: root.headerHeight
                clip: true
                interactive: false
                model: timelineModel
                syncView: cels
                syncDirection: Qt.Horizontal
                rowHeightProvider: row => row === 0 ? root.headerHeight : 0

                // Frame numbers, current frame, range and onion markers are painted
                // by the grid behind; the cells only give the view its size.
                delegate: Item {}

                TimelineGrid {
                    parent: header // the view itself, not its scrolling content
                    anchors.fill: parent
                    z: -1
                    model: timelineModel
                    mode: TimelineGrid.FrameNumbers
                    contentX: header.contentX
                    contentY: header.contentY
                    cellWidth: root.cellWidth
                    rowHeight: root.rowHeight
                    currentRow: root.currentRow
                    accent: Theme.accent
                    accentSoft: Theme.accentSoft
                    hover: Theme.hover
                    text: Theme.text
                    textDim: Theme.textDim
                    textFaint: Theme.textFaint
                    fontPixelSize: Theme.fontSizeSmall - 1
                }

                // Where dragged frames will land. Children of a TableView live
                // in its scrolling content item, so this is content space.
                Rectangle {
                    parent: header.contentItem
                    visible: headerArea.moving && headerArea.delta !== 0
                    x: (root.range.x + headerArea.delta) * root.cellWidth
                    y: 0
                    width: root.range.width * root.cellWidth
                    height: header.height
                    radius: 4
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                    z: 5
                }

                // In content coordinates, like the cel grid's input.
                MouseArea {
                    id: headerArea
                    parent: header.contentItem
                    width: header.contentWidth
                    height: header.height
                    z: 10
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property int anchorFrame: 0
                    property int pressFrame: 0
                    property bool moving: false
                    property int delta: 0

                    function frameAt(x) {
                        return Math.max(0, Math.min(root.editor.frameCount - 1, Math.floor(x / root.cellWidth)))
                    }
                    onPressed: mouse => {
                        root.interacted()
                        root.editor.playing = false
                        const f = frameAt(mouse.x)
                        const inside = root.frameRange && root.frameInRange(f)
                        if (mouse.button === Qt.RightButton) {
                            if (!inside) {
                                root.editor.selectFrames(f, f)
                                root.editor.currentFrame = f
                            }
                            frameMenu.popup()
                            return
                        }
                        if (mouse.modifiers & Qt.ShiftModifier) {
                            root.editor.selectFrames(anchorFrame, f)
                            root.editor.currentFrame = f
                            return
                        }
                        if (inside) {
                            moving = true
                            pressFrame = f
                            delta = 0
                            return
                        }
                        anchorFrame = f
                        root.editor.selectFrames(f, f)
                        root.editor.currentFrame = f
                    }
                    // The drag with the pointer at `x` (content coordinates).
                    function dragTo(x: real, shift: bool) {
                        const f = frameAt(x)
                        if (moving) {
                            delta = Math.max(-root.range.x, Math.min(root.editor.frameCount - root.range.x - root.range.width,
                                                                     f - pressFrame))
                        } else if (!shift) {
                            root.editor.selectFrames(anchorFrame, f)
                            root.editor.currentFrame = f
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || (pressedButtons & Qt.RightButton))
                            return
                        const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        dragTo(mouse.x, shift)
                        // Near an end of the frames: they scroll, and the drag follows the pointer.
                        const viewX = mouse.x - cels.contentX
                        root.autoScroll(Qt.point(viewX, cels.height / 2), () => headerArea.dragTo(viewX + cels.contentX, shift))
                    }
                    onCanceled: root.stopAutoScroll()
                    onReleased: mouse => {
                        root.stopAutoScroll()
                        if (!moving)
                            return
                        moving = false
                        if (delta !== 0) {
                            root.editor.moveRange(0, delta, false)
                        } else {
                            // A click inside the selection picks that frame alone.
                            const f = frameAt(mouse.x)
                            anchorFrame = f
                            root.editor.selectFrames(f, f)
                            root.editor.currentFrame = f
                        }
                        delta = 0
                    }
                    onWheel: wheel => wheel.accepted = false
                }
            }

            // --- layer column --------------------------------------------------

            TableView {
                id: layerColumn
                Layout.preferredWidth: root.layerColumnWidth
                Layout.fillHeight: true
                clip: true
                model: timelineModel
                syncView: cels
                syncDirection: Qt.Vertical
                columnWidthProvider: column => column === 0 ? root.layerColumnWidth : 0
                boundsBehavior: Flickable.StopAtBounds

                // A right click under the rows: the menu of the current layer.
                // On the view, not its contentItem (rows-sized), and behind it.
                MouseArea {
                    parent: layerColumn
                    anchors.fill: parent
                    z: -1
                    acceptedButtons: Qt.RightButton
                    onPressed: {
                        layerMenu.layerIndex = root.editor.currentLayer
                        layerMenu.popup()
                    }
                }

                // Where a dragged layer will land: a line between rows, or
                // the group it goes into.
                Rectangle {
                    parent: layerColumn.contentItem
                    z: 10
                    visible: root.dropRow >= 0
                    x: 2
                    width: root.layerColumnWidth - 4
                    y: root.dropZone === 2 ? root.dropRow * root.rowHeight
                       : (root.dropRow + (root.dropZone === 1 ? 1 : 0)) * root.rowHeight - 1
                    height: root.dropZone === 2 ? root.rowHeight : 2
                    radius: root.dropZone === 2 ? 4 : 1
                    color: root.dropZone === 2 ? Theme.accentSoft : Theme.accent
                    border.color: Theme.accent
                    border.width: root.dropZone === 2 ? 1 : 0
                }

                delegate: Rectangle {
                    id: layerRow
                    required property int row
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
                    readonly property bool current: row === root.currentRow
                    // Part of a block of layers taken with Shift-click (a cel range on the current frame).
                    readonly property bool inBlock: {
                        void root.editor.range
                        return (root.editor.rangeType === Editor.CelRange && root.editor.inRange(layerIndex, root.editor.currentFrame))
                               || root.editor.isPicked(layerIndex)
                    }
                    readonly property bool renaming: root.renamingLayer === layerIndex

                    implicitWidth: root.layerColumnWidth
                    implicitHeight: root.rowHeight
                    color: current || inBlock ? Theme.accentSoft : rowArea.containsMouse ? Theme.hover : "transparent"

                    MouseArea {
                        id: rowArea
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
                            root.interacted()
                            if ((mouse.modifiers & Qt.ControlModifier) && mouse.button === Qt.LeftButton) {
                                // Cmd / Ctrl: this layer in or out of the selection, whatever is next to it.
                                root.editor.togglePick(layerRow.layerIndex)
                                pickOnRelease = false
                            } else if ((mouse.modifiers & Qt.ShiftModifier) && mouse.button === Qt.LeftButton) {
                                // From the current layer to this one, on the current frame.
                                root.editor.selectLayerBlock(root.editor.currentLayer, layerRow.layerIndex)
                            } else {
                                // A click inside the block keeps it (the right-click menu then acts on all of it).
                                pickOnRelease = layerRow.inBlock && mouse.button === Qt.LeftButton
                                if (!layerRow.inBlock) {
                                    if (root.editor.rangeType === Editor.CelRange)
                                        root.editor.clearRange()
                                    root.editor.currentLayer = layerRow.layerIndex
                                }
                            }
                            pressY = mouse.y
                            dragging = false
                            if (mouse.button === Qt.RightButton) {
                                layerMenu.layerIndex = layerRow.layerIndex
                                layerMenu.popup()
                            }
                        }
                        onPositionChanged: mouse => {
                            if (!pressed || (pressedButtons & Qt.RightButton))
                                return
                            if (!dragging && Math.abs(mouse.y - pressY) < 4)
                                return
                            dragging = true
                            const p = mapToItem(layerColumn.contentItem, mouse.x, mouse.y)
                            dropAt(p.y)
                            // Past the top or bottom: scroll (the rows follow the grid's).
                            const view = Qt.point(root.autoScrollEdge + 1, p.y - layerColumn.contentY)
                            root.autoScroll(view, () => rowArea.dropAt(view.y + layerColumn.contentY))
                        }
                        // Where it would land at content y: above / below a row, or into a group.
                        function dropAt(y: real) {
                            const rowCount = layerColumn.rows
                            const r = Math.max(0, Math.min(rowCount - 1, Math.floor(y / root.rowHeight)))
                            const frac = Math.max(0, Math.min(1, y / root.rowHeight - r))
                            const target = root.editor.layers[timelineModel.layerForRow(r)]
                            root.dropRow = r
                            root.dropZone = target && target.kind === "group"
                                ? (frac < 0.3 ? 0 : frac > 0.7 ? 1 : 2)
                                : (frac < 0.5 ? 0 : 1)
                        }
                        onReleased: {
                            root.stopAutoScroll()
                            if (dragging && root.dropRow >= 0) {
                                root.editor.moveLayer(layerRow.layerIndex, timelineModel.layerForRow(root.dropRow), root.dropZone)
                            } else if (pickOnRelease && !dragging) {
                                root.editor.clearRange()
                                root.editor.currentLayer = layerRow.layerIndex
                            }
                            pickOnRelease = false
                            dragging = false
                            root.dropRow = -1
                        }
                        onCanceled: {
                            root.stopAutoScroll()
                            dragging = false
                            root.dropRow = -1
                        }
                        onDoubleClicked: root.renamingLayer = layerRow.layerIndex
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        anchors.rightMargin: 6
                        spacing: 0

                        IconButton {
                            implicitWidth: 24
                            implicitHeight: 24
                            iconName: layerRow.layerVisible ? "eye" : "eye-off"
                            tip: layerRow.layerVisible ? "Hide layer" : "Show layer"
                            onClicked: root.editor.setLayerVisible(layerRow.layerIndex, !layerRow.layerVisible)
                        }
                        IconButton {
                            implicitWidth: 24
                            implicitHeight: 24
                            iconName: layerRow.locked ? "lock" : "lock-open"
                            opacity: layerRow.locked || rowArea.containsMouse || hovered ? 1 : 0.35
                            tip: layerRow.locked ? "Unlock layer" : "Lock layer"
                            onClicked: root.editor.setLayerLocked(layerRow.layerIndex, !layerRow.locked)
                        }
                        // Alpha lock (image layers): painting stays on the
                        // layer's own pixels. No tooltip per row; the layer
                        // menu names it.
                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            radius: 4
                            visible: layerRow.kind === "image"
                            color: layerRow.alphaLock ? Theme.accentSoft : alphaArea.containsMouse ? Theme.hover : "transparent"
                            opacity: layerRow.alphaLock || rowArea.containsMouse || alphaArea.containsMouse ? 1 : 0.35
                            Text {
                                anchors.centerIn: parent
                                text: "\u03b1"
                                color: layerRow.alphaLock ? Theme.text : Theme.textDim
                                font.pixelSize: Theme.fontSize + 3
                                font.italic: true
                                font.weight: layerRow.alphaLock ? Font.DemiBold : Font.Normal
                            }
                            MouseArea {
                                id: alphaArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.editor.setLayerAlphaLock(layerRow.layerIndex, !layerRow.alphaLock)
                            }
                        }

                        Item { Layout.preferredWidth: 4 + layerRow.depth * 12 }

                        IconButton {
                            implicitWidth: 18
                            implicitHeight: 24
                            visible: layerRow.kind === "group"
                            iconName: "chevron-down"
                            rotation: layerRow.collapsed ? -90 : 0
                            tip: layerRow.collapsed ? "Expand group" : "Collapse group"
                            onClicked: root.editor.setLayerCollapsed(layerRow.layerIndex, !layerRow.collapsed)
                        }
                        IconButton {
                            implicitWidth: 18
                            implicitHeight: 24
                            visible: layerRow.kind === "tilemap" || layerRow.kind === "particles"
                            enabled: false
                            iconName: layerRow.kind === "particles" ? "wand-sparkles" : "grid-2x2"
                        }

                        Text {
                            visible: !layerRow.renaming
                            Layout.fillWidth: true
                            Layout.leftMargin: 4
                            text: layerRow.name
                            elide: Text.ElideRight
                            color: layerRow.shown ? Theme.text : Theme.textFaint
                            font.pixelSize: Theme.fontSize
                            font.weight: layerRow.kind === "group" ? Font.DemiBold : Font.Normal
                        }
                        // Non-default blend / opacity.
                        Text {
                            visible: !layerRow.renaming && (layerRow.layerOpacity < 100 || layerRow.blendName !== "Normal")
                            Layout.leftMargin: 4
                            text: (layerRow.blendName !== "Normal" ? layerRow.blendName : "")
                                  + (layerRow.blendName !== "Normal" && layerRow.layerOpacity < 100 ? " \u00b7 " : "")
                                  + (layerRow.layerOpacity < 100 ? layerRow.layerOpacity + "%" : "")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSizeSmall - 1
                            font.features: { "tnum": 1 }
                        }
                        AppTextField {
                                id: nameField
                                visible: layerRow.renaming
                                Layout.fillWidth: true
                                Layout.leftMargin: 2
                                text: layerRow.name
                                onVisibleChanged: if (visible) { text = layerRow.name; forceActiveFocus(); selectAll() }
                                onAccepted: layerRow.finishRename(true)
                                onActiveFocusChanged: if (!activeFocus && layerRow.renaming) layerRow.finishRename(true)
                                Keys.onEscapePressed: layerRow.finishRename(false)
                                compact: true
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

            // --- cels ----------------------------------------------------------

            TableView {
                id: cels
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                interactive: false // pointer input selects and moves cels
                model: timelineModel
                columnWidthProvider: () => root.cellWidth
                rowHeightProvider: () => root.rowHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar {}
                ScrollBar.vertical: AppScrollBar {}
                onRowsChanged: Qt.callLater(root.revealCurrent)
                onColumnsChanged: Qt.callLater(root.revealCurrent)

                // Cels are painted by the grid behind (one item instead of a few
                // per cell); the cells only give the view its size.
                delegate: Item {}

                TimelineGrid {
                    parent: cels // the view itself, not its scrolling content
                    anchors.fill: parent
                    z: -1
                    model: timelineModel
                    mode: TimelineGrid.Cels
                    contentX: cels.contentX
                    contentY: cels.contentY
                    cellWidth: root.cellWidth
                    rowHeight: root.rowHeight
                    currentRow: root.currentRow
                    accent: Theme.accent
                    accentSoft: Theme.accentSoft
                    hover: Theme.hover
                    text: Theme.text
                    textDim: Theme.textDim
                    textFaint: Theme.textFaint
                    fontPixelSize: Theme.fontSizeSmall - 1
                }

                // Pointer input over the whole grid, in content coordinates
                // (inside the content item, so the scroll bars stay usable).
                MouseArea {
                    id: celArea
                    parent: cels.contentItem
                    width: cels.contentWidth
                    height: cels.contentHeight
                    z: 10
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property int anchorLayer: 0
                    property int anchorFrame: 0
                    property int pressRow: 0
                    property int pressFrame: 0
                    property bool moving: false
                    property int rowDelta: 0
                    property int frameDelta: 0
                    // A live layer's range bar: dragging an end ("left", "right") or the body ("move")
                    // changes the frames the effect runs over. A press in the body is an ordinary
                    // click until it is dragged to another frame.
                    property string rangeMode: ""
                    property bool bodyPending: false
                    property int rangeFrom: 0
                    property int rangeTo: 0
                    property int rangeFrame: 0 // the frame pressed

                    // "left" or "right" when (x, y) is on an end of a stream's range bar, else "".
                    function gripAt(x: real, y: real): string {
                        const row = rowAt(y)
                        const f = frameAt(x)
                        const live = timelineModel.liveRange(row)
                        if (live.x < 0 || !timelineModel.liveStream(row))
                            return ""
                        const inCell = x - f * root.cellWidth
                        if (f === live.x && inCell < 8)
                            return "left"
                        return f === live.y && inCell > root.cellWidth - 8 && !timelineModel.liveOpenEnded(row) ? "right" : ""
                    }
                    property bool overGrip: false
                    hoverEnabled: true
                    cursorShape: overGrip || rangeMode !== "" ? Qt.SizeHorCursor
                               : overKey || keyFrom >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    // A Smear group's key: the frame of the diamond under (x, y), or -1. Dragging one moves
                    // it (Alt copies it) to `keyTo`.
                    function keyAt(x: real, y: real): int {
                        const f = frameAt(x)
                        const keys = timelineModel.smearKeys(rowAt(y))
                        return keys.indexOf(f) >= 0 && Math.abs(x - (f + 0.5) * root.cellWidth) < 9 ? f : -1
                    }
                    property bool overKey: false
                    property int keyFrom: -1
                    property int keyTo: -1

                    function applyRange(f: int) {
                        const last = root.editor.frameCount - 1
                        const d = f - rangeFrame
                        let s = rangeFrom, e = rangeTo
                        if (rangeMode === "left") {
                            s = Math.max(0, Math.min(rangeTo, rangeFrom + d))
                        } else if (rangeMode === "right") {
                            e = Math.max(rangeFrom, Math.min(last, rangeTo + d))
                        } else {
                            const shift = Math.max(-5000 - rangeFrom, Math.min(last - rangeTo, d))
                            s = rangeFrom + shift
                            e = rangeTo + shift
                        }
                        root.editor.setParticleRange(s, e, true)
                    }

                    function rowAt(y) { return Math.max(0, Math.min(cels.rows - 1, Math.floor(y / root.rowHeight))) }
                    function frameAt(x) { return Math.max(0, Math.min(cels.columns - 1, Math.floor(x / root.cellWidth))) }

                    onPressed: mouse => {
                        root.interacted()
                        root.editor.playing = false
                        const row = rowAt(mouse.y)
                        const f = frameAt(mouse.x)
                        const layer = timelineModel.layerForRow(row)
                        const inside = root.celInRange(layer, f)
                        const key = keyAt(mouse.x, mouse.y)
                        if (key >= 0) { // on a Smear group's key: it is picked, and dragged or offered a menu
                            root.editor.currentLayer = layer
                            root.editor.currentFrame = key
                            if (mouse.button === Qt.RightButton) {
                                keyMenu.frame = key
                                keyMenu.popup()
                            } else {
                                keyFrom = key
                                keyTo = key
                            }
                            return
                        }
                        if (mouse.button === Qt.RightButton) {
                            if (!inside) {
                                root.editor.selectCels(layer, f, layer, f)
                                root.editor.currentLayer = layer
                                root.editor.currentFrame = f
                            }
                            if (root.frameRange)
                                frameMenu.popup()
                            else
                                celMenu.popup()
                            return
                        }
                        if (mouse.modifiers & Qt.ShiftModifier) {
                            root.editor.selectCels(anchorLayer, anchorFrame, layer, f)
                            return
                        }
                        rangeMode = ""
                        bodyPending = false
                        const live = timelineModel.liveRange(row)
                        if (live.x >= 0 && f >= live.x && f <= live.y) {
                            rangeFrom = live.x
                            rangeTo = live.y
                            rangeFrame = f
                            rangeMode = gripAt(mouse.x, mouse.y)
                            if (rangeMode !== "") {
                                root.editor.currentLayer = layer
                                return
                            }
                            bodyPending = true
                        }
                        if (inside) {
                            moving = true
                            pressRow = row
                            pressFrame = f
                            rowDelta = 0
                            frameDelta = 0
                            return
                        }
                        anchorLayer = layer
                        anchorFrame = f
                        root.editor.currentLayer = layer
                        root.editor.currentFrame = f
                        root.editor.selectCels(layer, f, layer, f)
                    }
                    // The drag at (x, y) in the content: moved cels' offset, or
                    // the range being selected.
                    function dragTo(x: real, y: real, shift: bool) {
                        const row = rowAt(y)
                        const f = frameAt(x)
                        if (keyFrom >= 0) {
                            keyTo = f
                            return
                        }
                        if (bodyPending && rangeMode === "" && f !== rangeFrame) {
                            rangeMode = "move"
                            moving = false
                            rowDelta = 0
                            frameDelta = 0
                        }
                        if (rangeMode !== "") {
                            applyRange(f)
                        } else if (moving) {
                            rowDelta = root.frameRange ? 0 : row - pressRow
                            frameDelta = f - pressFrame
                        } else if (!shift) {
                            root.editor.selectCels(anchorLayer, anchorFrame, timelineModel.layerForRow(row), f)
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) {
                            overGrip = gripAt(mouse.x, mouse.y) !== ""
                            overKey = keyAt(mouse.x, mouse.y) >= 0
                            return
                        }
                        if (pressedButtons & Qt.RightButton)
                            return
                        const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        dragTo(mouse.x, mouse.y, shift)
                        // Past an edge: scroll, and follow with the same view point.
                        const view = Qt.point(mouse.x - cels.contentX, mouse.y - cels.contentY)
                        root.autoScroll(view, () => celArea.dragTo(view.x + cels.contentX, view.y + cels.contentY, shift))
                    }
                    onCanceled: {
                        root.stopAutoScroll()
                        keyFrom = -1
                        keyTo = -1
                    }
                    onReleased: mouse => {
                        root.stopAutoScroll()
                        if (keyFrom >= 0) {
                            if (keyTo !== keyFrom)
                                root.editor.moveSmearKey(keyFrom, keyTo, (mouse.modifiers & Qt.AltModifier) !== 0)
                            keyFrom = -1
                            keyTo = -1
                            return
                        }
                        if (rangeMode !== "") {
                            rangeMode = ""
                            bodyPending = false
                            return
                        }
                        bodyPending = false
                        if (!moving)
                            return
                        moving = false
                        if (rowDelta !== 0 || frameDelta !== 0) {
                            // Rows go down as layers go up.
                            root.editor.moveRange(-rowDelta, frameDelta, (mouse.modifiers & Qt.AltModifier) !== 0)
                        } else {
                            const layer = timelineModel.layerForRow(rowAt(mouse.y))
                            const f = frameAt(mouse.x)
                            anchorLayer = layer
                            anchorFrame = f
                            root.editor.currentLayer = layer
                            root.editor.currentFrame = f
                            root.editor.selectCels(layer, f, layer, f)
                        }
                        rowDelta = 0
                        frameDelta = 0
                    }
                    onWheel: wheel => {
                        // Not a Flickable gesture any more: scroll by hand.
                        const shift = wheel.modifiers & Qt.ShiftModifier
                        let dx = wheel.pixelDelta.x !== 0 ? -wheel.pixelDelta.x : -wheel.angleDelta.x / 4
                        let dy = wheel.pixelDelta.y !== 0 ? -wheel.pixelDelta.y : -wheel.angleDelta.y / 4
                        if (shift && dx === 0) {
                            dx = dy
                            dy = 0
                        }
                        root.scrollTo(cels.contentX + dx, cels.contentY + dy)
                    }
                }

                // Where a dragged key will land.
                Rectangle {
                    parent: cels.contentItem
                    z: 11
                    visible: celArea.keyFrom >= 0 && celArea.keyTo !== celArea.keyFrom
                    width: 10
                    height: 10
                    rotation: 45
                    x: (celArea.keyTo + 0.5) * root.cellWidth - width / 2
                    y: timelineModel.rowForLayer(root.editor.currentLayer) * root.rowHeight + root.rowHeight / 2 - height / 2
                    color: Theme.accentTint
                    border.color: Theme.accent
                    border.width: 2
                }

                // Where dragged cels / frames will land.
                Rectangle {
                    parent: cels.contentItem
                    z: 11
                    visible: celArea.moving && (celArea.rowDelta !== 0 || celArea.frameDelta !== 0)
                    x: (root.range.x + celArea.frameDelta) * root.cellWidth
                    y: root.frameRange ? 0
                       : (timelineModel.rowForLayer(root.range.y + root.range.height - 1) + celArea.rowDelta) * root.rowHeight
                    width: root.range.width * root.cellWidth
                    height: (root.frameRange ? cels.rows : root.range.height) * root.rowHeight
                    radius: 4
                    color: Theme.accentTint
                    border.color: Theme.accent
                    border.width: 2
                }
            }
        }
    }
}
