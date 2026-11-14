import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Smalto

// Export as PNG (one frame or a numbered sequence), animated GIF or a sprite
// sheet (PNG + JSON), with a live preview of the result (frames play at
// their durations). Choices are kept while the app runs.
Popup {
    id: root

    required property Editor editor

    readonly property var formats: ["png", "gif", "sheet"]
    readonly property var layouts: ["horizontal", "vertical", "grid", "tags"]
    readonly property string format: formats[formatChoice.currentIndex]
    // One row per tag; the frame range doesn't apply.
    readonly property bool byTag: format === "sheet" && layoutChoice.currentIndex === 3
    // "all", "current" or a tag index.
    readonly property var range: rangeChoice.currentIndex === 0 ? "all"
                               : rangeChoice.currentIndex === 1 ? "current"
                               : Math.max(0, tagBox.currentIndex)
    property int exportScale: 1
    // PNG and sheet: save as an indexed PNG when there are 256 colors or fewer.
    property bool indexedPng: false
    // Remembered between runs by Main.qml (with exportScale).
    property alias formatIndex: formatChoice.currentIndex
    property alias layoutIndex: layoutChoice.currentIndex
    property alias rangeIndex: rangeChoice.currentIndex
    property string lastFolder: "" // where the last export went (a file URL)
    readonly property var options: ({
        format: format,
        range: range,
        scale: exportScale,
        indexedPng: indexedPng,
        layout: layouts[layoutChoice.currentIndex]
    })
    readonly property var info: {
        // Also depends on the document.
        void [editor.frameCount, editor.currentFrame, editor.canvasWidth, editor.canvasHeight]
        return editor.exportInfo(options)
    }

    // Prepared again when the options or the document change while open.
    readonly property var preview: {
        void [editor.frameCount, editor.currentFrame, editor.canvasWidth, editor.canvasHeight]
        return visible ? editor.prepareExportPreview(options) : { durations: [] }
    }
    readonly property var previewDurations: root.preview.durations
    readonly property var previewCells: root.preview.cells ?? []
    property int previewIndex: 0
    property bool previewPlaying: true
    property bool showCells: true
    onPreviewChanged: previewIndex = 0
    onFormatChanged: {
        previewImage.fixedZoom = 0
        previewImage.pan = Qt.point(0, 0)
    }
    function step(delta: int) {
        const n = previewDurations.length
        if (n < 2)
            return
        previewPlaying = false
        previewIndex = (previewIndex + delta + n) % n
    }
    function zoomBy(factor: real) {
        const zoom = previewImage.fixedZoom > 0 ? previewImage.fixedZoom : previewImage.zoom
        previewImage.fixedZoom = Math.max(0.1, Math.min(64, zoom * factor))
    }
    function fitPreview() {
        previewImage.fixedZoom = 0
        previewImage.pan = Qt.point(0, 0)
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

    // Tags may disappear when another file is opened.
    Connections {
        target: root.editor
        function onFramesChanged() {
            if (root.editor.tags.length === 0 && rangeChoice.currentIndex === 2)
                rangeChoice.currentIndex = 0
            if (root.editor.tags.length === 0 && layoutChoice.currentIndex === 3)
                layoutChoice.currentIndex = 0
        }
    }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    contentItem: ColumnLayout {
        spacing: 16

        Text {
            text: qsTr("Export")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: 400
            Layout.preferredHeight: 240
            radius: Theme.radiusSmall
            color: Theme.hover

            ImagePreview {
                id: previewImage
                anchors.fill: parent
                anchors.margins: 12
                anchors.bottomMargin: 34
                clip: true
                cells: root.format === "sheet" && root.showCells ? root.previewCells : []
                cellColor: Theme.accent
                image: {
                    void root.preview
                    return root.editor.exportPreviewFrame(root.visible ? root.previewIndex : -1)
                }
            }

            // Zoomed in: drag moves the image. Scroll zooms; double-click fits.
            DragHandler {
                target: null
                enabled: previewImage.fixedZoom > 0
                property point start
                onActiveChanged: if (active) start = previewImage.pan
                onTranslationChanged: previewImage.pan = Qt.point(start.x + translation.x, start.y + translation.y)
            }
            WheelHandler {
                target: null
                onWheel: event => root.zoomBy(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1)
            }
            TapHandler { onDoubleTapped: root.fitPreview() }

            Timer {
                running: root.visible && root.previewPlaying && root.previewDurations.length > 1
                repeat: true
                interval: Math.max(10, root.previewDurations[root.previewIndex] || 100)
                onTriggered: root.previewIndex = (root.previewIndex + 1) % root.previewDurations.length
            }

            // Frame counter, play / step, zoom.
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: controls.verticalCenter
                anchors.leftMargin: 8
                visible: root.previewDurations.length > 1
                text: (root.previewIndex + 1) + " / " + root.previewDurations.length
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                font.features: { "tnum": 1 }
            }
            Row {
                id: controls
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                spacing: 2
                visible: root.previewDurations.length > 1
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: "chevron-left"
                    tip: qsTr("Previous frame")
                    onClicked: root.step(-1)
                }
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: root.previewPlaying ? "pause" : "play"
                    tip: root.previewPlaying ? qsTr("Pause") : qsTr("Play")
                    onClicked: root.previewPlaying = !root.previewPlaying
                }
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: "chevron-right"
                    tip: qsTr("Next frame")
                    onClicked: root.step(1)
                }
            }
            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 4
                spacing: 2
                IconButton {
                    visible: root.format === "sheet"
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: "square-dashed"
                    tip: qsTr("Outline each frame")
                    active: root.showCells
                    onClicked: root.showCells = !root.showCells
                }
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: "zoom-out"
                    tip: qsTr("Zoom out")
                    onClicked: root.zoomBy(1 / 1.25)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44
                    horizontalAlignment: Text.AlignHCenter
                    text: Math.round(previewImage.zoom * 100) + "%"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    font.features: { "tnum": 1 }
                }
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: "zoom-in"
                    tip: qsTr("Zoom in")
                    onClicked: root.zoomBy(1.25)
                }
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconName: "scan"
                    tip: qsTr("Fit")
                    active: previewImage.fixedZoom === 0
                    onClicked: root.fitPreview()
                }
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 10

            Label { text: qsTr("Format") }
            Segmented {
                id: formatChoice
                model: [qsTr("PNG"), qsTr("GIF"), qsTr("Sprite sheet")]
            }

            Label {
                text: qsTr("Frames")
                visible: !root.byTag
            }
            RowLayout {
                visible: !root.byTag
                spacing: 8
                Segmented {
                    id: rangeChoice
                    model: root.editor.tags.length > 0 ? [qsTr("All"), qsTr("Current"), qsTr("Tag")]
                                                       : [qsTr("All"), qsTr("Current")]
                }
                AppComboBox {
                    id: tagBox
                    visible: rangeChoice.currentIndex === 2
                    model: root.editor.tags
                    textRole: "name"
                    implicitWidth: 170
                    labelFor: i => {
                        const t = root.editor.tags[i]
                        return t ? t.name + "  " + (t.from + 1) + "\u2013" + (t.to + 1) : ""
                    }
                }
            }

            Label { text: qsTr("Scale") }
            RowLayout {
                spacing: 2
                IconButton {
                    iconName: "minus"
                    tip: qsTr("Smaller")
                    enabled: root.exportScale > 1
                    onClicked: root.exportScale = Math.max(1, root.exportScale - 1)
                }
                Text {
                    text: root.exportScale + "×"
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    font.features: { "tnum": 1 }
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 32
                }
                IconButton {
                    iconName: "plus"
                    tip: qsTr("Larger")
                    enabled: root.exportScale < 64
                    onClicked: root.exportScale = Math.min(64, root.exportScale + 1)
                }
            }

            Label {
                text: qsTr("Colors")
                visible: root.format !== "gif"
            }
            Segmented {
                visible: root.format !== "gif"
                model: [qsTr("RGBA"), qsTr("Indexed if ≤ 256")]
                currentIndex: root.indexedPng ? 1 : 0
                onActivated: i => root.indexedPng = i === 1
            }

            Label {
                text: qsTr("Layout")
                visible: root.format === "sheet"
            }
            Segmented {
                id: layoutChoice
                visible: root.format === "sheet"
                model: root.editor.tags.length > 0 ? [qsTr("Row"), qsTr("Column"), qsTr("Grid"), qsTr("By tag")]
                                                   : [qsTr("Row"), qsTr("Column"), qsTr("Grid")]
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.maximumWidth: 440
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            text: {
                const n = root.info.frames
                const size = root.info.width + " × " + root.info.height + " px"
                const frames = n === 1 ? qsTr("1 frame") : qsTr("%1 frames").arg(n)
                if (root.format === "png" && n > 1)
                    return frames + " · " + size + qsTr(" each, saved as numbered files (name-1.png, name-2.png, …)")
                if (root.byTag)
                    return qsTr("%1 tags, one per row, in tag order: ").arg(root.info.rows) + frames
                            + qsTr(" in one %1 image, plus a .json file with frames and tags").arg(size)
                if (root.format === "sheet")
                    return frames + qsTr(" in one %1 image, plus a .json file with frames and tags").arg(size)
                if (root.format === "gif")
                    return frames + " · " + size + qsTr(" · loops; alpha becomes on/off; frames get their own colors past 255 in all")
                return size
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
                onClicked: {
                    const ext = root.format === "gif" ? ".gif" : ".png"
                    const stem = root.editor.fileName.replace(/\.(aseprite|ase)$/i, "")
                    // Where the last export went, else next to the sprite.
                    const folder = root.lastFolder !== "" ? root.lastFolder
                            : root.editor.fileUrl.toString() !== ""
                            ? root.editor.fileUrl.toString().replace(/\/[^\/]*$/, "")
                            : fileDialog.currentFolder.toString()
                    fileDialog.nameFilters = root.format === "gif" ? [qsTr("GIF images (*.gif)")]
                                                                   : [qsTr("PNG images (*.png)")]
                    fileDialog.defaultSuffix = ext.substring(1)
                    fileDialog.currentFolder = folder
                    fileDialog.selectedFile = folder + "/" + stem + ext
                    fileDialog.open()
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Export")
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (root.editor.exportFile(selectedFile, root.options)) {
                root.lastFolder = selectedFile.toString().replace(/\/[^\/]*$/, "")
                root.close()
            }
        }
    }
}
