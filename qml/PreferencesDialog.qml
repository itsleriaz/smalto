pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Preferences: app-wide settings, grouped in sections down the left.
// Changes apply as you make them; Main.qml remembers them between runs.
Popup {
    id: root

    required property Editor editor
    required property CanvasView canvas
    // Files: reopen the last session's saved files at startup (Main.qml
    // remembers it and does the reopening).
    property bool reopenFiles: true

    // Panels: which ones the window shows (Main.qml remembers and applies
    // them). The timeline's state is the window's (Tab toggles it too).
    property bool showColors: true
    property bool showPreview: true
    property bool showTools: true
    property bool showLayers: false // the classic layers list
    property int panelStyle: 0 // 0 floating cards, 1 docked to the window edges
    property int toolColumns: 1 // the tool strip starts in this many columns
    property bool timelineShown: true
    signal timelineRequested(bool shown)
    signal resetLayoutRequested()

    // Each section's Reset to defaults (Shortcuts has its own resets).
    function resetGeneral() {
        Theme.accentChoices = Theme.themes.map(() => 0)
        Theme.themeIndex = 0
        Theme.accentIndex = 0
        Theme.mode = 0
    }
    function resetCanvas() {
        canvas.resetPreferences()
        editor.resetNewGridSize()
    }
    function resetPanels() {
        showTools = true
        showLayers = false
        showColors = true
        showPreview = true
        panelStyle = 0
        toolColumns = 1
        timelineRequested(true)
        resetLayoutRequested()
    }
    function resetFiles() {
        reopenFiles = true
        editor.resetAutosaveDelay()
        editor.resetUndoMemory()
    }

    property int section: 0
    readonly property var sections: [qsTr("General"), qsTr("Canvas"), qsTr("Panels"), qsTr("Files"), qsTr("Shortcuts")]

    // A floating window, not modal and not dimmed: the canvas stays in view
    // and changes as you choose (theme, checkerboard, panels). Takes the keys
    // while open, so Esc closes it (not the window's Esc); dragged by its top.
    focus: true
    modal: false
    padding: 0
    closePolicy: Popup.CloseOnEscape
    property bool placed: false
    onAboutToShow: {
        if (section >= sections.length) // a section that has gone (About) was the last one used
            section = 0
        if (placed)
            return
        x = Math.round((Overlay.overlay.width - width) / 2)
        y = Math.round((Overlay.overlay.height - height) / 3)
        placed = true
    }
    background: Panel {
        // The top band moves the window.
        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 46
            DragHandler {
                target: null
                property point start
                onActiveChanged: if (active) start = Qt.point(root.x, root.y)
                onTranslationChanged: if (active) {
                    root.x = start.x + translation.x
                    root.y = start.y + translation.y
                }
            }
            HoverHandler { cursorShape: Qt.SizeAllCursor }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }
    component Heading: Text {
        color: Theme.text
        font.pixelSize: 15
        font.weight: Font.DemiBold
    }
    component Note: Text {
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    // A section of the preferences that scrolls when it is taller than the window.
    component Page: Flickable {
        id: page
        default property alias content: column.data
        property alias spacing: column.spacing
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}
        ColumnLayout {
            id: column
            width: page.width - 14
        }
    }
    component Separator: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.separator
    }
    // At the foot of a section: puts that section's settings back.
    component ResetButton: AppButton {
        text: qsTr("Reset to defaults")
        Layout.alignment: Qt.AlignLeft
    }
    component Fields: GridLayout {
        columns: 2
        columnSpacing: 14
        rowSpacing: 8
    }

    contentItem: Item {
        implicitWidth: layout.implicitWidth
        implicitHeight: layout.implicitHeight

        // Above the sections (they would take the click otherwise).
        IconButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            z: 10
            iconName: "x"
            tip: qsTr("Close")
            onClicked: root.close()
        }

        RowLayout {
            id: layout
            anchors.fill: parent
            spacing: 0

            // Sections
            ColumnLayout {
                Layout.preferredWidth: 150
                Layout.fillHeight: true
                Layout.margins: 10
                spacing: 2

                Text {
                    text: qsTr("Preferences")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    Layout.leftMargin: 10
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                }

                Repeater {
                    model: root.sections
                    delegate: Rectangle {
                        id: item
                        required property string modelData
                        required property int index
                        readonly property bool current: index === root.section

                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        radius: Theme.radiusSmall
                        color: current ? Theme.accentSoft : area.containsMouse ? Theme.hover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            text: item.modelData
                            color: item.current ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            id: area
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.section = item.index
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.separator
            }

            StackLayout {
                currentIndex: root.section
                Layout.preferredWidth: 440
                Layout.preferredHeight: 420
                Layout.margins: 20

                // General
                Page {
                    spacing: 14
                    Heading { text: qsTr("Appearance") }
                    // The palette families, each shown as itself in the current
                    // light / dark variant: window, panel and accent.
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: Theme.themes
                            delegate: Rectangle {
                                id: card
                                required property var modelData
                                required property int index
                                readonly property bool current: index === Theme.themeIndex
                                readonly property var variant: Theme.dark ? modelData.dark : modelData.light
                                width: 128
                                height: 62
                                radius: Theme.radiusSmall + 2
                                color: variant.bg
                                border.color: current ? Theme.accent : cardArea.containsMouse ? Theme.controlBorder : Theme.separator
                                border.width: current ? 2 : 1

                                Rectangle {
                                    x: 8
                                    y: 8
                                    width: parent.width - 16
                                    height: 24
                                    radius: 6
                                    color: card.variant.panel
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 7
                                        spacing: 4
                                        Repeater {
                                            model: Math.min(4, card.modelData.accents.length)
                                            delegate: Rectangle {
                                                required property int index
                                                width: 10
                                                height: 10
                                                radius: 5
                                                color: Theme.dark ? card.modelData.accents[index].dark : card.modelData.accents[index].light
                                            }
                                        }
                                    }
                                }
                                Text {
                                    x: 9
                                    y: 38
                                    text: card.modelData.name
                                    color: card.variant.text
                                    font.pixelSize: Theme.fontSize
                                    font.weight: card.current ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: cardArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Theme.themeIndex = card.index // its accent comes back with it
                                }
                            }
                        }
                    }
                    Fields {
                        Label { text: qsTr("Mode") }
                        Segmented {
                            model: [qsTr("System"), qsTr("Dark"), qsTr("Light")]
                            currentIndex: Theme.mode
                            onActivated: index => Theme.mode = index
                        }
                    }
                    Note { text: qsTr("System follows your computer's light or dark setting. Every theme has a dark and a light variant.") }
                    Fields {
                        Label { text: qsTr("Accent") }
                        Row {
                            spacing: 6
                            Repeater {
                                model: Theme.accents
                                delegate: Rectangle {
                                    id: swatch
                                    required property var modelData
                                    required property int index
                                    readonly property bool current: index === Theme.accentIndex
                                    width: 22
                                    height: 22
                                    radius: 11
                                    color: Theme.dark ? modelData.dark : modelData.light
                                    // A ring around the chosen one.
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width + 6
                                        height: parent.height + 6
                                        radius: width / 2
                                        color: "transparent"
                                        border.color: swatch.current ? swatch.color : swatchArea.containsMouse ? Theme.separator : "transparent"
                                        border.width: 2
                                    }
                                    MouseArea {
                                        id: swatchArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Theme.accentIndex = swatch.index
                                    }
                                }
                            }
                        }
                    }
                    Note { text: qsTr("Accent: %1. Used for selected buttons, highlights and focus; each theme brings its own set and remembers your pick.").arg(Theme.accentPreset.name) }
                    Item { Layout.fillHeight: true }
                    ResetButton { onClicked: root.resetGeneral() }
                }

                // Canvas
                Page {
                    spacing: 14
                    Heading { text: qsTr("Checkerboard") }
                    Fields {
                        Label { text: qsTr("Colors") }
                        RowLayout {
                            spacing: 8
                            Segmented {
                                enabled: !root.canvas.checkerFollowsTheme
                                opacity: enabled ? 1 : 0.45
                                model: [qsTr("Dark"), qsTr("Light"), qsTr("Gray"), qsTr("Custom")]
                                currentIndex: root.canvas.checkerStyle
                                onActivated: i => root.canvas.checkerStyle = i
                            }
                            OptionPill {
                                text: qsTr("Match the theme")
                                tip: qsTr("Checkerboard colors taken from the current theme and mode, instead of Dark / Light / Gray")
                                checked: root.canvas.checkerFollowsTheme
                                onToggled: root.canvas.checkerFollowsTheme = !root.canvas.checkerFollowsTheme
                            }
                        }

                        Label {
                            text: qsTr("Custom")
                            visible: root.canvas.checkerStyle === 3 && !root.canvas.checkerFollowsTheme
                        }
                        RowLayout {
                            visible: root.canvas.checkerStyle === 3 && !root.canvas.checkerFollowsTheme
                            spacing: 10
                            ColorField {
                                value: root.canvas.customCheckerA
                                onEdited: c => root.canvas.customCheckerA = c
                            }
                            ColorField {
                                value: root.canvas.customCheckerB
                                onEdited: c => root.canvas.customCheckerB = c
                            }
                        }

                        Label { text: qsTr("Behavior") }
                        OptionPill {
                            text: qsTr("Fixed on screen")
                            tip: qsTr("The cells keep one size and place on the screen while you zoom and pan, instead of belonging to the picture")
                            checked: root.canvas.checkerFixed
                            onToggled: root.canvas.checkerFixed = !root.canvas.checkerFixed
                        }

                        Label { text: qsTr("Cell size") }
                        NumberControl {
                            enabled: !root.canvas.checkerFixed
                            opacity: enabled ? 1 : 0.45
                            value: root.canvas.checkerSize
                            from: 0
                            to: 64
                            zeroText: qsTr("Auto")
                            suffix: " px"
                            tip: qsTr("Checkerboard cell size in sprite pixels; Auto keeps about the same size on screen at any zoom")
                            onMoved: v => root.canvas.checkerSize = v
                        }
                    }

                    Separator {}

                    Heading { text: qsTr("Tiled view") }
                    Fields {
                        Label { text: qsTr("Copies") }
                        OptionPill {
                            text: qsTr("Dim the copies")
                            tip: qsTr("The repeats around the sprite fade a little, so the original stays clear")
                            checked: root.canvas.dimTiledCopies
                            onToggled: root.canvas.dimTiledCopies = !root.canvas.dimTiledCopies
                        }
                    }

                    Heading { text: qsTr("Grids") }
                    Fields {
                        Label { text: qsTr("Pixel grid") }
                        ColorField {
                            alpha: true
                            value: root.canvas.gridColor
                            onEdited: c => root.canvas.gridColor = c
                        }
                        Label { text: qsTr("Document grid") }
                        RowLayout {
                            spacing: 8
                            ColorField {
                                alpha: true
                                enabled: root.canvas.documentGridColor.a > 0
                                opacity: enabled ? 1 : 0.45
                                value: root.canvas.documentGridColor.a > 0 ? root.canvas.documentGridColor : Qt.rgba(1, 1, 1, 0.45)
                                onEdited: c => root.canvas.documentGridColor = c
                            }
                            OptionPill {
                                text: qsTr("Automatic")
                                tip: qsTr("Black over light colors and white over dark ones")
                                checked: root.canvas.documentGridColor.a === 0
                                onToggled: root.canvas.documentGridColor = root.canvas.documentGridColor.a === 0
                                           ? Qt.rgba(1, 0.2, 0.6, 0.6) : Qt.rgba(0, 0, 0, 0)
                            }
                        }
                    }
                    Note { text: qsTr("The pixel grid's alpha is its strength. Colors are #rrggbbaa.") }

                    Separator {}

                    Heading { text: qsTr("Trackpad scrolling") }
                    Fields {
                        Label { text: qsTr("Two-finger scroll") }
                        Segmented {
                            model: [qsTr("Pan"), qsTr("Zoom")]
                            currentIndex: root.canvas.scrollZooms ? 1 : 0
                            onActivated: i => root.canvas.scrollZooms = i === 1
                        }
                    }
                    Note {
                        text: root.canvas.scrollZooms
                              ? qsTr("On a trackpad (or any smooth-scrolling device) scrolling zooms the canvas; hold %1 to pan. Pinch zooms too. A mouse wheel with notches always zooms.").arg(Qt.platform.os === "osx" ? "\u2318" : "Ctrl")
                              : qsTr("On a trackpad (or any smooth-scrolling device) scrolling pans the canvas; hold %1 to zoom. A mouse wheel with notches always zooms.").arg(Qt.platform.os === "osx" ? "\u2318" : "Ctrl")
                    }

                    Separator {}

                    Heading { text: qsTr("Grid for new sprites") }
                    Fields {
                        Label { text: qsTr("New sprites") }
                        RowLayout {
                            spacing: 4
                            NumberControl {
                                value: root.editor.newGridSize.width
                                from: 1
                                to: 4096
                                suffix: " px"
                                tip: qsTr("Grid cell width for new sprites")
                                onMoved: v => root.editor.newGridSize = Qt.size(v, root.editor.newGridSize.height)
                            }
                            Text { text: "×"; color: Theme.textDim; font.pixelSize: Theme.fontSize }
                            NumberControl {
                                value: root.editor.newGridSize.height
                                from: 1
                                to: 4096
                                suffix: " px"
                                tip: qsTr("Grid cell height for new sprites")
                                onMoved: v => root.editor.newGridSize = Qt.size(root.editor.newGridSize.width, v)
                            }
                        }
                    }
                    Note { text: qsTr("Each sprite keeps its own grid (saved in the file); set it from the grid button in the top bar.") }
                    Item { Layout.fillHeight: true }
                    ResetButton { onClicked: root.resetCanvas() }
                }

                // Panels
                Page {
                    spacing: 14
                    Heading { text: qsTr("Show") }
                    Note { text: qsTr("Choose which panels the window shows. A hidden panel leaves its room to the canvas.") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        OptionPill {
                            text: qsTr("Tools")
                            tip: qsTr("The tool strip down the window's edge")
                            checked: root.showTools
                            onToggled: root.showTools = !root.showTools
                        }
                        OptionPill {
                            text: qsTr("Layers and timeline")
                            tip: qsTr("Layers, frames and tags, at the bottom")
                            checked: root.timelineShown
                            onToggled: root.timelineRequested(!root.timelineShown)
                        }
                        OptionPill {
                            text: qsTr("Layers list")
                            tip: qsTr("A compact layers list, for working without the timeline")
                            checked: root.showLayers
                            onToggled: root.showLayers = !root.showLayers
                        }
                        OptionPill {
                            text: qsTr("Colors and palette")
                            tip: qsTr("The color picker and the palette")
                            checked: root.showColors
                            onToggled: root.showColors = !root.showColors
                        }
                        OptionPill {
                            text: qsTr("Preview")
                            tip: qsTr("The sprite as it will look, and its animation")
                            checked: root.showPreview
                            onToggled: root.showPreview = !root.showPreview
                        }
                    }
                    Note {
                        text: qsTr("%1 shows or hides the timeline, %2 the layers list, %3 the colors and palette, %4 the preview, and %5 hides every panel (including the tool options).")
                              .arg(Shortcuts.labels.hideTimeline || qsTr("(no key)"))
                              .arg(Shortcuts.labels.layersPanel || qsTr("(no key)"))
                              .arg(Shortcuts.labels.colorsPanel || qsTr("(no key)"))
                              .arg(Shortcuts.labels.previewPanel || qsTr("(no key)"))
                              .arg(Shortcuts.labels.hideUi || qsTr("(no key)"))
                    }

                    Separator {}

                    Heading { text: qsTr("Layout") }
                    Fields {
                        Label { text: qsTr("Style") }
                        Segmented {
                            model: [qsTr("Floating"), qsTr("Docked")]
                            currentIndex: root.panelStyle
                            onActivated: index => root.panelStyle = index
                        }
                        Label { text: qsTr("Tool columns") }
                        Segmented {
                            model: ["1", "2", "3", "4"]
                            currentIndex: root.toolColumns - 1
                            onActivated: index => root.toolColumns = index + 1
                        }
                        Label { text: qsTr("Arrangement") }
                        AppButton {
                            text: qsTr("Reset panel layout")
                            onClicked: root.resetLayoutRequested()
                        }
                    }
                    Note { text: qsTr("Floating panels are cards over the canvas; docked ones fill the window's sides. Drag a panel by the grip at its top to move it to either side or to another place in its column, or onto another panel's title to make them tabs. Drag the gaps between panels, and their inner edges, to resize them. Reset panel layout puts them back where they started, at their usual sizes.") }
                    Item { Layout.fillHeight: true }
                    ResetButton { onClicked: root.resetPanels() }
                }

                // Files
                Page {
                    spacing: 14
                    Heading { text: qsTr("Startup") }
                    Fields {
                        Label { text: qsTr("Reopen files") }
                        Segmented {
                            model: [qsTr("Off"), qsTr("On")]
                            currentIndex: root.reopenFiles ? 1 : 0
                            onActivated: i => root.reopenFiles = i === 1
                        }
                    }
                    Note { text: qsTr("Opens the saved files that were open when Smalto last quit.") }

                    Separator {}

                    Heading { text: qsTr("Crash recovery") }
                    Fields {
                        Label { text: qsTr("Copy after") }
                        NumberControl {
                            value: root.editor.autosaveDelay
                            from: 0
                            to: 600
                            step: 5
                            zeroText: qsTr("Off")
                            suffix: " s"
                            tip: qsTr("Seconds after the last change before unsaved work is copied for crash recovery")
                            onMoved: v => root.editor.autosaveDelay = v
                        }
                    }
                    Note {
                        text: qsTr("Unsaved sprites are copied aside a few seconds after you stop editing, and offered back if Smalto quits unexpectedly. Your own files are never overwritten.")
                    }
                    Separator {}

                    Heading { text: qsTr("Undo history") }
                    Fields {
                        Label { text: qsTr("Memory per sprite") }
                        NumberControl {
                            value: root.editor.undoMemory
                            from: 0
                            to: 4096
                            step: 64
                            zeroText: qsTr("Unlimited")
                            suffix: " MB"
                            tip: qsTr("How much memory a sprite's undo history may use before its oldest steps are dropped")
                            onMoved: v => root.editor.undoMemory = v
                        }
                    }
                    Note {
                        text: qsTr("Big sprites with many edits can use a lot of memory. When the history is over the limit, the oldest steps are forgotten; the latest step always stays.")
                    }
                    Item { Layout.fillHeight: true }
                    ResetButton { onClicked: root.resetFiles() }
                }

                ShortcutsSettings {}
            }
        }
    }
}
