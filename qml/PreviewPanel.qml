pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Smalto

// The sprite as it will look, at its own zoom (Fit, or a set zoom with
// drag to pan, scroll to zoom). Follows the current frame, or plays the
// animation by itself: the tag the current frame is in, or every frame.
Panel {
    id: root

    required property Editor editor
    required property CanvasView canvas

    // What the window arranges (PanelColumns), see ColorPanel.
    readonly property real wantHeight: desiredHeight
    readonly property real minHeight: headerOnlyHeight
    readonly property int shrinkRank: 2
    movable: true

    property int viewHeight: 200 // dragged by the grip under the picture
    property real maxViewHeight: 600 // what the window leaves for it (set by it)
    // What the window leaves for the whole panel (set by it). The preview
    // shrinks to fit, and folds to its header when under `minView`.
    property real maxHeight: 10000
    // Small, so a short column still shows a thumbnail: at 60 the picture went
    // away while the panel kept a blank strip under its title ("empty").
    readonly property int minView: 24
    readonly property real headerOnlyHeight: 26 + padding * 2
    // What it would like, whatever room there is.
    readonly property real desiredHeight: headerOnlyHeight + 6 + viewHeight
    readonly property real shownViewHeight: Math.min(viewHeight, maxHeight - headerOnlyHeight - 6)
    readonly property bool folded: shownViewHeight < minView
    // Playing here only; the editor's frame stays put. While the canvas plays
    // too, this shows the canvas's frame (one clock for both, see
    // Editor::startPreviewPlayback), and goes on by itself when it stops.
    property bool playing: false
    property int frame: 0
    property int playingTag: -1 // the tag it plays (-1: every frame)
    readonly property bool following: playing && editor.playing
    readonly property int shownFrame: playing && !following ? frame : editor.currentFrame

    function play(on) {
        if (on && editor.frameCount < 2)
            return
        playing = on
        if (on) {
            frame = editor.startPreviewPlayback(editor.currentFrame)
            playingTag = editor.tagIndexAt(frame)
            refresh()
            if (following) {
                playTimer.stop()
            } else {
                playTimer.interval = editor.frameDurationAt(frame)
                playTimer.restart()
            }
        } else {
            playTimer.stop()
            editor.stopPreviewPlayback()
            refresh()
        }
    }
    function refresh() {
        if (!folded)
            preview.image = editor.frameImage(shownFrame)
    }
    // Edits arrive per stroke step: redraw at most every 40 ms.
    function scheduleRefresh() {
        if (!refreshTimer.running)
            refreshTimer.start()
    }

    implicitHeight: layout.implicitHeight + padding * 2
    padding: 10

    onFoldedChanged: refresh()
    Component.onCompleted: refresh()

    Timer {
        id: refreshTimer
        interval: 40
        onTriggered: root.refresh()
    }
    Timer {
        id: playTimer
        onTriggered: {
            if (root.following)
                return
            const next = root.editor.nextPreviewFrame() // the tag's direction and repeats
            if (next < 0) { // the last repeat has played
                root.play(false)
                return
            }
            root.frame = next
            root.refresh()
            interval = root.editor.frameDurationAt(root.frame)
            restart()
        }
    }
    Connections {
        target: root.editor
        function onCanvasChanged() { root.scheduleRefresh() }
        // Moved into another tag (or out of one) while playing: play that instead.
        function onCurrentFrameChanged() {
            if (!root.playing || root.following)
                root.scheduleRefresh()
            else if (root.editor.tagIndexAt(root.editor.currentFrame) !== root.playingTag)
                root.play(true)
        }
        // The canvas started or stopped while this plays: follow it, or go on
        // from its frame by itself (it handed over its place in the tag).
        function onPlayingChanged() {
            if (!root.playing)
                return
            if (root.editor.playing) {
                playTimer.stop()
            } else {
                root.frame = root.editor.currentFrame
                root.playingTag = root.editor.tagIndexAt(root.frame)
                playTimer.interval = root.editor.frameDurationAt(root.frame)
                playTimer.restart()
            }
            root.scheduleRefresh()
        }
        function onFramesChanged() {
            if (root.editor.frameCount < 2)
                root.play(false)
            root.scheduleRefresh()
        }
        function onLayersChanged() { root.scheduleRefresh() }
        function onPaletteChanged() { root.scheduleRefresh() }
        function onColorModeChanged() { root.scheduleRefresh() }
        function onCanvasSizeChanged() { root.scheduleRefresh() }
        function onDocumentsChanged() { // also when another sprite becomes the current one
            if (root.playing)
                root.play(false)
            root.scheduleRefresh()
        }
    }

    // Drag the grip under the picture to make it taller or shorter. It sits in
    // the panel's bottom padding, so the layout doesn't change.
    Item {
        id: grip
        parent: root
        visible: !root.folded
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.padding
        z: 5

        Rectangle {
            anchors.centerIn: parent
            width: 28
            height: 3
            radius: 1.5
            color: gripDrag.active || gripHover.hovered ? Theme.textDim : Theme.textFaint
            opacity: gripDrag.active || gripHover.hovered ? 1 : 0.6
        }
        HoverHandler {
            id: gripHover
            cursorShape: Qt.SizeVerCursor
        }
        DragHandler {
            id: gripDrag
            target: null
            cursorShape: Qt.SizeVerCursor
            property real startHeight
            onActiveChanged: if (active) startHeight = root.shownViewHeight
            onTranslationChanged: if (active)
                root.viewHeight = Math.round(Math.max(root.minView, Math.min(root.maxViewHeight, startHeight + translation.y)))
        }
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            PanelTitle {
                text: "Preview"
                Layout.fillWidth: true
            }
            IconButton {
                visible: !root.folded
                implicitWidth: 26
                implicitHeight: 26
                iconName: root.playing ? "pause" : "play"
                tip: root.playing ? "Stop" : "Play here (the current tag with its direction and repeats, or every frame)"
                enabled: root.editor.frameCount > 1
                onClicked: root.play(!root.playing)
            }
            NumberControl {
                visible: !root.folded
                value: Math.round(preview.zoom * 100)
                from: 10
                to: 6400
                factor: 0.1
                fieldWidth: 50
                suffix: "%"
                tip: "Preview zoom"
                onMoved: v => preview.fixedZoom = v / 100
            }
            IconButton {
                visible: !root.folded
                implicitWidth: 26
                implicitHeight: 26
                iconName: "scan"
                tip: "Fit"
                active: preview.fixedZoom === 0
                onClicked: {
                    preview.fixedZoom = 0
                    preview.pan = Qt.point(0, 0)
                }
            }
        }

        ImagePreview {
            id: preview
            visible: !root.folded
            Layout.fillWidth: true
            Layout.fillHeight: true // a docked panel with room to spare shows it bigger
            Layout.preferredHeight: root.shownViewHeight
            checkerLight: root.canvas.checkerA
            checkerDark: root.canvas.checkerB
            clip: true
            wheelZooms: true // about the pointer, from Fit too

            // Set zoom: drag pans.
            DragHandler {
                target: null
                enabled: preview.fixedZoom > 0
                property point start
                onActiveChanged: if (active) start = preview.pan
                onTranslationChanged: preview.pan = Qt.point(start.x + translation.x, start.y + translation.y)
            }
            TapHandler {
                // Double-click: back to Fit.
                onDoubleTapped: {
                    preview.fixedZoom = 0
                    preview.pan = Qt.point(0, 0)
                }
            }
        }
        // Folded: the title stays at the top of whatever height it was given.
        Item {
            visible: root.folded
            Layout.fillHeight: true
        }
    }
}
