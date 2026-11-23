pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Smalto

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    minimumWidth: 1100 // the tabs and the main bar share the top row
    minimumHeight: 600
    // No system title bar: the tabs take its place. macOS keeps its traffic
    // lights, floating over our row (`extendIntoTitleBar` in MacTitleBar.mm,
    // called from main()); elsewhere the window is frameless and TitleBar
    // draws the buttons.
    flags: Qt.platform.os === "osx" ? (Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint)
                                    : (Qt.Window | Qt.FramelessWindowHint)
    visible: false // shown once the saved size and place are back (restoreWindow)
    title: Qt.platform.os === "osx"
           ? appEditor.fileName + (appEditor.modified ? " \u2014 Edited" : "")
           : (appEditor.modified ? "*" : "") + appEditor.fileName + " \u2014 Smalto"
    color: Theme.background
    // ApplicationWindow keeps its content clear of the safe area (Qt 6.9+),
    // which on macOS is the title bar; our top row sits under the traffic
    // lights on purpose, so the content gets the whole window.
    Connections {
        target: window.contentItem
        function onYChanged() { window.fillWindow() }
        function onHeightChanged() { window.fillWindow() }
        function onXChanged() { window.fillWindow() }
        function onWidthChanged() { window.fillWindow() }
    }
    function fillWindow() {
        if (Qt.platform.os !== "osx")
            return
        const c = contentItem
        if (c.x !== 0) c.x = 0
        if (c.y !== 0) c.y = 0
        if (c.width !== width) c.width = width
        if (c.height !== height) c.height = height
    }

    Editor {
        id: appEditor
    }

    // --- remembered between runs (QSettings, next to recent files) ----------

    // The window's normal (not maximized) size and place, and whether it was
    // maximized; written as it changes.
    Settings {
        id: windowSettings
        category: "window"
        property int x: 0
        property int y: 0
        property int width: 1440
        property int height: 900
        property bool placed: false // x / y were saved at least once
        property bool maximized: false
        property alias timelineHeight: timeline.preferredHeight
        property alias timelineHidden: ui.timelineHidden
        property alias paletteCollapsed: colorPanel.paletteCollapsed
        property alias tileCellSize: colorPanel.tileCellSize
        property alias tileColumns: colorPanel.tileColumns
        property alias timelineLayerColumn: timeline.layerColumnShown
        property alias sidebarWidth: ui.sidebarWidth
        property alias leftWidth: ui.leftWidth
        property alias layersHeight: ui.layersHeight
        property alias colorsHeight: ui.colorsHeight
        property alias panelLayout: ui.panelLayout
        property alias liveSections: ui.liveSections
        property alias smearSections: ui.smearSections
        property alias liveOverlay: ui.liveOverlay
        property alias previewHeight: previewPanel.viewHeight
        property alias showGrid: canvasView.showGrid
        property alias showCustomGrid: canvasView.showCustomGrid
        property alias tileMode: appEditor.tileMode
        property alias paletteLoadMode: appEditor.paletteLoadMode
        property alias showSlices: canvasView.showSlices
        property alias checkerStyle: canvasView.checkerStyle
        property alias checkerSize: canvasView.checkerSize
        property alias checkerFixed: canvasView.checkerFixed
        property alias dimTiledCopies: canvasView.dimTiledCopies
        property alias customCheckerA: canvasView.customCheckerA
        property alias customCheckerB: canvasView.customCheckerB
        property alias gridColor: canvasView.gridColor
        property alias documentGridColor: canvasView.documentGridColor
        property alias checkerFollowsTheme: canvasView.checkerFollowsTheme
    }

    // Tool options, colors and onion skin. The two enums are kept as plain
    // numbers (an alias would store them tagged with the C++ type's name).
    // The theme: 0 System, 1 Dark, 2 Light (Theme.mode).
    Settings {
        id: appearanceSettings
        category: "appearance"
        property int theme: 0
        property int palette: 0 // an index into Theme.themes
        property int accent: 0 // before accents: the chosen theme's accent
        property string accents: "" // each theme's accent index, comma separated
    }
    Connections {
        target: Theme
        function onModeChanged() { appearanceSettings.theme = Theme.mode }
        function onThemeIndexChanged() { appearanceSettings.palette = Theme.themeIndex }
        function onAccentChoicesChanged() { appearanceSettings.accents = Theme.accentChoices.join(",") }
    }

    // The export and new sprite dialogs' last choices.
    Settings {
        category: "export"
        property alias format: exportPopup.formatIndex
        property alias layout: exportPopup.layoutIndex
        property alias range: exportPopup.rangeIndex
        property alias scale: exportPopup.exportScale
        property alias indexedPng: exportPopup.indexedPng
        property alias folder: exportPopup.lastFolder
    }
    // The tileset export dialog's last choices.
    Settings {
        category: "tilesetExport"
        property alias columns: tilesetExportDialog.columns
        property alias margin: tilesetExportDialog.sheetMargin
        property alias spacing: tilesetExportDialog.sheetSpacing
        property alias tsx: tilesetExportDialog.tsx
        property alias tmx: tilesetExportDialog.tmx
        property alias tsj: tilesetExportDialog.tsj
        property alias tmj: tilesetExportDialog.tmj
        property alias tres: tilesetExportDialog.tres
        property alias godotFolder: tilesetExportDialog.godotFolder
        property alias csv: tilesetExportDialog.csv
        property alias folder: tilesetExportDialog.lastFolder
    }
    Settings {
        category: "newSprite"
        property alias width: newSpriteDialog.spriteWidth
        property alias height: newSpriteDialog.spriteHeight
        property alias frames: newSpriteDialog.frames
        property alias userPresets: newSpriteDialog.userPresets
        property alias backgroundColor: newSpriteDialog.backgroundColor
        property alias colorMode: newSpriteDialog.modeIndex
        property alias background: newSpriteDialog.backgroundIndex
    }

    // The saved files open when the app last quit, and which was current.
    Settings {
        id: session
        category: "session"
        property var files: []
        property string current: ""
    }
    function rememberSession() {
        session.files = appEditor.documents.map(d => d.path).filter(p => p !== "")
        session.current = appEditor.filePath
    }
    function reopenSession() {
        if (!preferencesDialog.reopenFiles)
            return
        for (const path of session.files)
            if (appEditor.fileExists(path))
                openPath(path)
        const current = appEditor.documentForPath(session.current)
        if (current >= 0)
            appEditor.currentDocument = current
    }

    // The rest of Preferences (the checkerboard is under "window").
    Settings {
        category: "preferences"
        property alias reopenFiles: preferencesDialog.reopenFiles
        property alias section: preferencesDialog.section
        property alias autosaveDelay: appEditor.autosaveDelay
        property alias undoMemory: appEditor.undoMemory
        property alias newGridSize: appEditor.newGridSize
        property alias scrollZooms: canvasView.scrollZooms
        property alias showColors: preferencesDialog.showColors
        property alias showPreview: preferencesDialog.showPreview
        property alias showTools: preferencesDialog.showTools
        property alias toolColumns: preferencesDialog.toolColumns
        property alias showLayers: preferencesDialog.showLayers
        property alias showTags: preferencesDialog.showTags
        property alias panelStyle: preferencesDialog.panelStyle
    }

    Settings {
        id: toolSettings
        category: "tools"
        property int tool: Editor.Pencil
        property int brushShape: Editor.SquareBrush
        property alias brushSize: appEditor.brushSize
        property alias brushAngle: appEditor.brushAngle
        property alias pixelPerfect: appEditor.pixelPerfect
        property alias shading: appEditor.shading
        property alias brushColors: appEditor.brushColors
        property alias brushBlend: appEditor.brushBlend
        property alias pressureSize: appEditor.pressureSize
        property alias pressureOpacity: appEditor.pressureOpacity
        property alias pressureColor: appEditor.pressureColor
        property alias pressureMinSize: appEditor.pressureMinSize
        property alias pressureDither: appEditor.pressureDither
        property alias pressureCurve: appEditor.pressureCurve
        property alias tiltAngle: appEditor.tiltAngle
        property alias stabilizer: canvasView.stabilizer
        property alias dither: appEditor.dither
        property alias ditherPattern: appEditor.ditherPattern
        property alias ditherDensity: appEditor.ditherDensity
        property alias ditherOtherColor: appEditor.ditherOtherColor
        property alias fillShapes: appEditor.fillShapes
        property alias fillContiguous: appEditor.fillContiguous
        property alias fillTolerance: appEditor.fillTolerance
        property alias wandContiguous: appEditor.wandContiguous
        property alias lassoPolygon: appEditor.lassoPolygon
        property alias transformPivot: appEditor.transformPivot
        property alias sliceKeys: appEditor.sliceKeys
        property alias editTiles: appEditor.editTiles
        property alias tileFlips: appEditor.tileFlips
        property alias tileEditMode: appEditor.tileEditMode
        property alias textFont: appEditor.textFont
        property alias textAlign: appEditor.textAlign
        property alias snapToGrid: appEditor.snapToGrid
        property alias gradientShape: appEditor.gradientShape
        property alias liveSourceKind: appEditor.liveSourceKind
        property alias liveFieldKind: appEditor.liveFieldKind
        property alias gradientRamp: appEditor.gradientRamp
        property alias gradientDither: appEditor.gradientDither
        property alias gradientRepeat: appEditor.gradientRepeat
        property alias gradientCustom: appEditor.gradientCustom
        property alias gradientStopsText: appEditor.gradientStopsText
        property alias textLetterSpacing: appEditor.textLetterSpacing
        property alias textLineSpacing: appEditor.textLineSpacing
        property alias wandTolerance: appEditor.wandTolerance
        property alias sampleMerged: appEditor.sampleMerged
        property alias symmetryHorizontal: appEditor.symmetryHorizontal
        property alias symmetryVertical: appEditor.symmetryVertical
        property alias symmetryRising: appEditor.symmetryRising
        property alias symmetryFalling: appEditor.symmetryFalling
        property alias symmetryRadial: appEditor.symmetryRadial
        property alias symmetryRadialMirror: appEditor.symmetryRadialMirror
        property alias primaryColor: appEditor.primaryColor
        property alias secondaryColor: appEditor.secondaryColor
        property alias onionSkin: appEditor.onionSkin
        property alias onionBefore: appEditor.onionBefore
        property alias onionAfter: appEditor.onionAfter
        property alias onionOpacity: appEditor.onionOpacity
        property alias onionTint: appEditor.onionTint
        property alias onionFront: appEditor.onionFront
        property alias onionLoop: appEditor.onionLoop
    }

    // Puts the window back where it was (if that's still on a screen) and shows it.
    // Untyped: qmllint's type for Qt.application lacks `screens`.
    readonly property var application: Qt.application

    function restoreWindow() {
        const s = windowSettings
        width = Math.max(minimumWidth, s.width)
        height = Math.max(minimumHeight, s.height)
        // The title bar has to land on a screen that's still there.
        const onScreen = s.placed && window.application.screens.some(sc =>
            s.x + width - 100 > sc.virtualX && s.x + 100 < sc.virtualX + sc.width
            && s.y >= sc.virtualY - 10 && s.y + 40 < sc.virtualY + sc.height)
        if (onScreen) {
            x = s.x
            y = s.y
        }
        if (s.maximized)
            showMaximized()
        else
            show()
    }
    function rememberGeometry() {
        if (visibility !== Window.Windowed)
            return
        windowSettings.x = x
        windowSettings.y = y
        windowSettings.width = width
        windowSettings.height = height
        windowSettings.placed = true
    }
    onXChanged: rememberGeometry()
    onYChanged: rememberGeometry()
    onWidthChanged: rememberGeometry()
    onHeightChanged: rememberGeometry()
    onVisibilityChanged: function(visibility) {
        if (visibility === Window.Maximized || visibility === Window.Windowed)
            windowSettings.maximized = visibility === Window.Maximized
        rememberGeometry()
    }

    // --- files: closing a document with unsaved changes asks first ---------

    // What Ctrl+C / X / V act on: the last thing clicked, canvas or timeline.
    property bool timelineEdit: false

    // True while a text field has focus: single-key shortcuts stay out of the way.
    readonly property bool typing: activeFocusItem !== null && "cursorPosition" in activeFocusItem

    property var pendingAction: null
    property var afterSave: null
    property bool closeConfirmed: false

    function confirmDiscard(action) {
        if (!appEditor.modified) {
            action()
            return
        }
        pendingAction = action
        unsavedDialog.open()
    }

    function openDocument() {
        openDialog.open()
    }

    // Closes a tab, asking first if it has unsaved changes (it becomes the
    // current one so the question and Save are about it).
    function closeDocument(index) {
        if (!appEditor.isDocumentModified(index)) {
            appEditor.closeDocument(index)
            return
        }
        appEditor.currentDocument = index
        confirmDiscard(() => appEditor.closeDocument(appEditor.currentDocument))
    }

    // Closes every tab but `keep`, asking about unsaved ones in turn (Cancel
    // stops there). From the last tab down, so the ones left keep their
    // indices; `kept` follows `keep` as tabs before it close.
    function closeOthers(keep) {
        const step = (i, kept) => {
            if (i < 0) {
                appEditor.currentDocument = kept
                return
            }
            if (i === kept) {
                step(i - 1, kept)
                return
            }
            const next = () => step(i - 1, i < kept ? kept - 1 : kept)
            if (!appEditor.isDocumentModified(i)) {
                appEditor.closeDocument(i)
                next()
                return
            }
            appEditor.currentDocument = i
            confirmDiscard(() => {
                appEditor.closeDocument(appEditor.currentDocument)
                next()
            })
        }
        step(appEditor.documents.length - 1, keep)
    }

    // Closes every tab to the right of `index`, asking about unsaved ones in turn from the last one
    // (Cancel stops there); the tab itself ends up current.
    function closeToTheRight(index) {
        const step = i => {
            if (i <= index) {
                appEditor.currentDocument = index
                return
            }
            if (!appEditor.isDocumentModified(i)) {
                appEditor.closeDocument(i)
                step(i - 1)
                return
            }
            appEditor.currentDocument = i
            confirmDiscard(() => {
                appEditor.closeDocument(appEditor.currentDocument)
                step(i - 1)
            })
        }
        step(appEditor.documents.length - 1)
    }

    // Asks about each document with unsaved changes in turn, closing it once
    // answered, then runs `done`. Cancel stops there.
    function closeAll(done) {
        for (let i = 0; i < appEditor.documents.length; ++i) {
            if (appEditor.isDocumentModified(i)) {
                appEditor.currentDocument = i
                confirmDiscard(() => {
                    appEditor.closeDocument(appEditor.currentDocument)
                    window.closeAll(done)
                })
                return
            }
        }
        done()
    }

    // Runs `then` once the document is saved.
    function save(then) {
        if (appEditor.saveFile()) {
            if (then)
                then()
        } else if (appEditor.fileUrl.toString() === "") {
            saveAs(then)
        }
    }

    function saveAs(then) {
        afterSave = then || null
        saveDialog.selectedFile = appEditor.fileUrl.toString() !== ""
                ? appEditor.fileUrl
                : saveDialog.currentFolder + "/" + appEditor.fileName + ".aseprite"
        saveDialog.open()
    }

    onClosing: close => {
        if (!closeConfirmed)
            rememberSession() // before closeAll closes any of them
        if (closeConfirmed || !appEditor.documents.some(d => d.modified))
            return
        close.accepted = false
        closeAll(() => {
            closeConfirmed = true
            window.close()
        })
    }

    // Opens a file by path (dialog, recent files, Finder, drops, command
    // line). An image that is part of a numbered sequence asks whether to
    // open the whole sequence as frames (not GIFs: they have their own).
    function openPath(path) {
        if (appEditor.isImageFile(path) && !/\.gif$/i.test(path)) {
            const sequence = appEditor.imageSequence(path)
            if (sequence.length > 1) {
                sequenceDialog.path = path
                sequenceDialog.files = sequence
                sequenceDialog.open()
                return
            }
        }
        if (!appEditor.openPath(path))
            RecentFiles.remove(path)
    }

    // Several files opened at once each get a tab.
    function openQueuedFiles() {
        for (const path of FileOpenQueue.take())
            openPath(path)
    }

    Connections {
        target: appEditor
        function onLiveFieldPlaced(index) { ui.liveField = index }
        function onToolChanged() { toolSettings.tool = appEditor.tool }
        function onBrushSizeChanged() { toolSettings.brushShape = appEditor.brushShape }
    }

    Component.onCompleted: {
        window.fillWindow()
        Theme.mode = appearanceSettings.theme
        const choices = Theme.themes.map(() => 0)
        if (appearanceSettings.accents !== "") {
            appearanceSettings.accents.split(",").forEach((v, i) => { if (i < choices.length) choices[i] = Number(v) || 0 })
        } else if (appearanceSettings.palette >= 0 && appearanceSettings.palette < choices.length) {
            choices[appearanceSettings.palette] = appearanceSettings.accent // saved before each theme had its own
        }
        Theme.accentChoices = choices
        Theme.themeIndex = appearanceSettings.palette
        Theme.accentIndex = choices[Theme.themeIndex] ?? 0
        appEditor.tool = toolSettings.tool
        appEditor.brushShape = toolSettings.brushShape
        restoreWindow()
        reopenSession()
        const showAt = Qt.application.arguments.indexOf("--show")
        if (showAt >= 0 && showAt + 1 < Qt.application.arguments.length) {
            showTimer.spec = Qt.application.arguments[showAt + 1]
            showTimer.start()
        }
        // Documents left by a crash: ask before bringing them back.
        const recovered = appEditor.recoverableDocuments()
        if (recovered.length > 0) {
            recoveryDialog.documents = recovered
            recoveryDialog.open()
        }
        openQueuedFiles()
    }

    Connections {
        target: FileOpenQueue
        function onFilesQueued() { window.openQueuedFiles() }
    }

    Connections {
        target: appEditor
        function onFileUsed(path) { RecentFiles.add(path) }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open")
        nameFilters: [qsTr("Sprites and images (*.aseprite *.ase *.png *.gif *.jpg *.jpeg *.bmp *.tga)"),
                      qsTr("Aseprite files (*.aseprite *.ase)"),
                      qsTr("Images (*.png *.gif *.jpg *.jpeg *.bmp *.tga)"), qsTr("All files (*)")]
        onAccepted: window.openPath(appEditor.toLocalPath(selectedFile))
    }

    FileDialog {
        id: loadPaletteDialog
        title: qsTr("Load Palette")
        nameFilters: [qsTr("Palettes (*.gpl *.hex *.pal *.act *.aseprite *.ase *.png *.gif *.jpg *.jpeg *.bmp *.tga)"),
                      qsTr("GIMP palettes (*.gpl)"), qsTr("Hex palettes (*.hex)"), qsTr("Photoshop / JASC palettes (*.pal *.act)"), qsTr("Sprites (*.aseprite *.ase)"),
                      qsTr("Images (*.png *.gif *.jpg *.jpeg *.bmp *.tga)")]
        onAccepted: appEditor.loadPalette(appEditor.toLocalPath(selectedFile))
    }

    FileDialog {
        id: savePaletteDialog
        title: qsTr("Save Palette")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gpl"
        nameFilters: [qsTr("GIMP palette (*.gpl)"), qsTr("Hex palette (*.hex)"), qsTr("JASC palette (*.pal)"),
                      qsTr("Photoshop color table (*.act)")]
        onAccepted: {
            const path = appEditor.toLocalPath(selectedFile)
            const name = path.replace(/^.*[\\/]/, "").replace(/\.[^.]*$/, "")
            appEditor.savePalette(path, name)
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import Image as Layer")
        nameFilters: [qsTr("Images (*.png *.gif *.jpg *.jpeg *.bmp *.tga)")]
        onAccepted: appEditor.importLayer(selectedFile)
    }

    FileDialog {
        id: tilesetImportFile
        title: qsTr("Import Tileset Image")
        nameFilters: [qsTr("Images (*.png *.gif *.jpg *.jpeg *.bmp *.tga)")]
        onAccepted: if (appEditor.loadTilesetSheet(selectedFile)) tilesetImportDialog.start()
    }

    FolderDialog {
        id: importFolderChoice
        title: qsTr("Import Folder as Animation")
        onAccepted: imageFolderDialog.start(appEditor.toLocalPath(selectedFolder))
    }
    ImageFolderDialog {
        id: imageFolderDialog
        editor: appEditor
    }

    AppDialog {
        id: sequenceDialog
        property string path
        property var files: []
        readonly property string first: files.length ? files[0].replace(/^.*[\\/]/, "") : ""
        readonly property string last: files.length ? files[files.length - 1].replace(/^.*[\\/]/, "") : ""

        title: qsTr("Open %1 numbered images as an animation?").arg(files.length)
        text: qsTr("%1 to %2 become the frames of one sprite.").arg(first).arg(last)
        buttons: [
            { id: "single", text: qsTr("Just This File"), leading: true },
            { id: "cancel", text: qsTr("Cancel") },
            { id: "all", text: qsTr("Open as Animation"), kind: "primary" },
        ]
        onClicked: id => {
            if (id === "all")
                appEditor.openImages(files)
            else if (id === "single")
                appEditor.openPath(path)
        }
    }

    AppDialog {
        id: recoveryDialog
        property var documents: []
        title: documents.length === 1
               ? qsTr("Restore a document from the last session?")
               : qsTr("Restore %1 documents from the last session?").arg(documents.length)
        text: (documents.length === 1 ? qsTr("Smalto didn't quit normally. This one had unsaved changes:")
                                      : qsTr("Smalto didn't quit normally. These had unsaved changes:")) + "<br>"
              + documents.map(d => "<b>" + d.name.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b> · " + d.saved)
                         .join("<br>")
        buttons: [
            { id: "discard", text: qsTr("Discard"), kind: "danger", leading: true },
            { id: "cancel", text: qsTr("Not Now") },
            { id: "restore", text: qsTr("Restore"), kind: "primary" },
        ]
        // Not Now (or Esc) keeps the copies for the next start.
        onClicked: id => {
            if (id === "restore")
                appEditor.restoreRecovered()
            else if (id === "discard")
                appEditor.discardRecovered()
        }
    }

    AppDialog {
        id: unsavedDialog
        title: qsTr("Do you want to save the changes to \u201c%1\u201d?").arg(appEditor.fileName)
        text: qsTr("Your changes will be lost if you don't save them.")
        buttons: [
            { id: "discard", text: qsTr("Don't Save"), kind: "danger", leading: true },
            { id: "cancel", text: qsTr("Cancel") },
            // "Save…" when saving will ask where.
            { id: "save", text: appEditor.filePath === "" ? qsTr("Save\u2026") : qsTr("Save"), kind: "primary" },
        ]
        onClicked: id => {
            const action = window.pendingAction
            window.pendingAction = null
            if (id === "save")
                window.save(action)
            else if (id === "discard")
                action()
        }
    }

    // A file's particle layer whose pixels were changed outside Smalto: keep them, or draw it again.
    function askStale() {
        const list = appEditor.staleLiveLayers()
        if (list.length === 0)
            return
        staleDialog.layerIndex = list[0].index
        staleDialog.layerName = list[0].name
        staleDialog.open()
    }
    Connections {
        target: appEditor
        function onStaleLiveFound() { window.askStale() }
    }
    AppDialog {
        id: staleDialog
        property int layerIndex: -1
        property string layerName
        title: qsTr("\u201c%1\u201d was changed outside Smalto").arg(layerName)
        text: qsTr("This layer is a particle effect, but its pixels no longer match its settings. Keep the pixels as an ordinary layer, or draw the effect again from its settings? Drawing it again replaces the pixels.")
        buttons: [
            { id: "later", text: qsTr("Decide Later"), leading: true },
            { id: "keep", text: qsTr("Keep Pixels") },
            { id: "render", text: qsTr("Draw Again"), kind: "primary" },
        ]
        onClicked: id => {
            if (id !== "keep" && id !== "render")
                return
            appEditor.resolveStaleLive(layerIndex, id === "render")
            Qt.callLater(window.askStale) // the next one, if any
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "aseprite"
        nameFilters: [qsTr("Aseprite files (*.aseprite *.ase)")]
        onAccepted: {
            const then = window.afterSave
            window.afterSave = null
            if (appEditor.saveFileAs(selectedFile) && then)
                then()
        }
        onRejected: window.afterSave = null
    }

    ExportPopup {
        id: exportPopup
        editor: appEditor
    }

    NewSpriteDialog {
        id: newSpriteDialog
        editor: appEditor
        onAccepted: options => appEditor.newSprite(options)
    }

    // Where the floating windows (sprite size, color mode, effects) open:
    // centered down the left side, past the left column of panels if it has
    // any. They stay where they're dragged to (the binding goes).
    readonly property real floatingX: columns.hasLeft ? columns.leftX + columns.leftWidth + Theme.margin
                                                      : toolStrip.x + toolStrip.width + Theme.margin
    function floatingY(h: real): real {
        const top = columns.columnTop, bottom = columns.columnBottom
        return Math.round(Math.max(top, top + (bottom - top - h) / 2))
    }

    SpriteSizeDialog {
        id: spriteSizeDialog
        editor: appEditor
        x: window.floatingX
        y: window.floatingY(height)
    }

    SpriteSheetDialog {
        id: spriteSheetDialog
        editor: appEditor
        x: window.floatingX
        y: window.floatingY(height)
    }

    // Floating like the effects.
    ColorModeDialog {
        id: colorModeDialog
        editor: appEditor
        x: window.floatingX
        y: window.floatingY(height)
    }

    // The menus: one description (AppMenus) for the in-window ones and for
    // the macOS menu bar. What isn't the editor's or the canvas's goes here.
    readonly property bool nativeMenus: Qt.platform.os === "osx" && Qt.platform.pluginName === "cocoa"
    AppMenus {
        id: appMenus
        editor: appEditor
        canvas: canvasView
        onWindowAction: (action, arg) => window.menuAction(action, arg)
    }
    // Made once the window is on screen: earlier, the app menu's Preferences
    // and Quit were missing until the app was activated again.
    Loader {
        active: window.nativeMenus && window.visible
        sourceComponent: NativeMenuBar {
            window: window
            host: window
            entries: appMenus.structure
            states: appMenus.states
            dangerTexts: appMenus.dangerTexts
            onChosen: (action, arg) => appMenus.run(action, arg)
        }
    }
    // Paste goes under the pointer while it's over the canvas (Paste in Place
    // keeps the spot it was copied from), as a new layer too.
    function pasteHere() {
        if (canvasView.hovering)
            appEditor.pasteAt(canvasView.hoverPixel)
        else
            appEditor.paste()
    }
    function pasteHereAsNewLayer() {
        return canvasView.hovering ? appEditor.pasteAsNewLayerAt(canvasView.hoverPixel) : appEditor.pasteAsNewLayer()
    }
    // Cut, Copy, Paste and Select All go to a text field that has the keys,
    // else to the timeline or the canvas (whichever was clicked last).
    function editText(name) {
        const item = window.activeFocusItem
        if (window.typing && item && typeof item[name] === "function") {
            item[name]()
            return
        }
        if (name === "cut")
            window.timelineEdit ? appEditor.cutTimeline() : appEditor.cut()
        else if (name === "copy")
            window.timelineEdit ? appEditor.copyTimeline() : appEditor.copy()
        else if (name === "paste")
            window.timelineEdit ? appEditor.pasteTimeline() : window.pasteHere()
        else
            window.selectAllHere()
    }
    // Select All: every frame when the timeline was clicked last, else every pixel.
    function selectAllHere() {
        if (window.timelineEdit)
            appEditor.selectFrames(0, appEditor.frameCount - 1)
        else
            appEditor.selectAll()
    }
    // `--show action[:arg]` (with --screenshot): opens that dialog or menu
    // action once the window is up, to look at it. "unsaved" is the
    // Save / Don't Save question.
    Timer {
        id: showTimer
        property string spec
        interval: 400
        onTriggered: {
            const parts = spec.split(":")
            if (parts[0] === "unsaved")
                unsavedDialog.open()
            else if (parts[0] === "stale") {
                staleDialog.layerName = "Sparks"
                staleDialog.open()
            } else if (parts[0] === "menu")
                menuBarRow.openTitled(parts[1])
            else if (parts[0] === "popup") {
                toolOptions.showPopup(parts[1])
                colorPanel.showPopup(parts[1])
                if (parts[1] === "grid")
                    statusBar.showGrid()
            } else if (parts[0] === "recovery") {
                recoveryDialog.documents = [{ name: "sprite.aseprite", saved: "yesterday" }]
                recoveryDialog.open()
            } else
                window.menuAction(parts[0], parts[1])
        }
    }
    // Opens the manual (a page of it, or the front page) in the browser.
    function openManual(page) {
        if (Manual.open(page))
            return
        manualMissingDialog.open()
    }
    AppDialog {
        id: manualMissingDialog
        title: qsTr("The manual isn't in this build")
        text: qsTr("This copy of Smalto came without its manual. Build it from the manual folder of the source with mdBook (see the README).")
    }
    Shortcut { enabled: !window.nativeMenus; sequence: "F1"; onActivated: window.openManual("") }
    function menuAction(action, arg) {
        switch (action) {
        case "new": newSpriteDialog.open(); break
        case "open": window.openDocument(); break
        case "openPath": window.openPath(arg); break
        case "import": importDialog.open(); break
        case "exportTileset": tilesetExportDialog.start(); break
        case "importTileset": tilesetImportFile.open(); break
        case "importSpriteSheet": spriteSheetDialog.open(); break
        case "importFolder": importFolderChoice.open(); break
        case "save": window.save(); break
        case "saveAs": window.saveAs(); break
        case "export": exportPopup.open(); break
        case "preferences": preferencesDialog.open(); break
        case "quit": window.close(); break
        case "about": aboutDialog.open(); break
        case "help": window.openManual(""); break
        case "helpShortcuts": window.openManual("reference/shortcuts.html"); break
        case "tilemapNew": tilesetDialog.openFor(false); break
        case "tilemapConvert": tilesetDialog.openFor(true); break
        case "frameDuration": frameDurationDialog.open(); break
        case "closeTab": window.closeDocument(appEditor.currentDocument); break
        case "minimize": window.showMinimized(); break
        case "zoomWindow": window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized(); break
        case "nextTab": appEditor.currentDocument = (appEditor.currentDocument + 1) % appEditor.documents.length; break
        case "previousTab": appEditor.currentDocument = (appEditor.currentDocument + appEditor.documents.length - 1) % appEditor.documents.length; break
        case "cut": case "copy": case "paste": case "selectAll": window.editText(action); break
        case "spriteSize": spriteSizeDialog.open(); break
        case "canvasSize": canvasSizer.start(); break
        case "colorMode": colorModeDialog.open(); break
        case "effect": window.startEffect(arg); break
        case "tagsPanel": preferencesDialog.showTags = !preferencesDialog.showTags; break
        case "layersPanel": preferencesDialog.showLayers = !preferencesDialog.showLayers; break
        case "colorsPanel": preferencesDialog.showColors = !preferencesDialog.showColors; break
        case "hidePanels": ui.hidden = true; break
        }
    }

    // Outline / Replace Color / Hue / Saturation on the current cel.
    function startEffect(kind) {
        if (!effectDialog.start(kind)) {
            noPixelsDialog.text = qsTr("The current layer has no pixels on this frame to change, or it's hidden, locked or a group.")
            noPixelsDialog.open()
        }
    }
    // Floating: see floatingX.
    EffectDialog {
        id: effectDialog
        editor: appEditor
        canvas: canvasView
        x: window.floatingX
        y: window.floatingY(height)
    }
    AppDialog {
        id: noPixelsDialog
        title: qsTr("Nothing to change")
    }

    TilesetDialog {
        id: tilesetDialog
        editor: appEditor
    }

    TilesetExportDialog {
        id: tilesetExportDialog
        editor: appEditor
        canvas: canvasView
    }

    TilesetImportDialog {
        id: tilesetImportDialog
        editor: appEditor
        canvas: canvasView
    }

    AboutDialog {
        id: aboutDialog
    }
    FrameDurationDialog {
        id: frameDurationDialog
        editor: appEditor
    }

    PreferencesDialog {
        id: preferencesDialog
        editor: appEditor
        canvas: canvasView
        timelineShown: !ui.timelineHidden
        onTimelineRequested: shown => ui.timelineHidden = !shown
        // Panels back where they started, at their default sizes.
        onResetLayoutRequested: {
            ui.panelLayout = ""
            ui.leftWidth = 230
            ui.sidebarWidth = 248
            ui.layersHeight = 320
            ui.colorsHeight = -1
        }
    }

    AppDialog {
        id: errorDialog
        title: qsTr("Something went wrong")
    }

    Connections {
        target: appEditor
        function onErrorOccurred(message) {
            errorDialog.text = message
            errorDialog.open()
        }
    }

    // Frameless windows have no border to grab: resize from the edges.
    WindowEdges {
        visible: Qt.platform.os !== "osx"
        anchors.fill: parent
        z: 100
        window: window
    }

    // Drop a sprite or image onto the window to open it.
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: drag => drag.accepted = drag.urls.some(u => /\.(aseprite|ase|png|gif|jpe?g|bmp|tga)$/i.test(u.toString()))
        onDropped: drop => {
            const url = drop.urls.find(u => /\.(aseprite|ase|png|gif|jpe?g|bmp|tga)$/i.test(u.toString()))
            if (url) {
                drop.accept(Qt.CopyAction)
                window.openPath(appEditor.toLocalPath(url))
            }
        }
    }

    CanvasView {
        id: canvasView
        anchors.fill: parent
        editor: appEditor
        backgroundColor: Theme.canvasBackground
        themeCheckerA: Theme.checkerA
        themeCheckerB: Theme.checkerB
        borderColor: Theme.canvasBorder
        // Docked panels are opaque, so the canvas fits and centers in what's
        // between them.
        viewArea: !ui.docked ? Qt.rect(0, 0, 0, 0)
                  : Qt.rect((toolStrip.visible ? toolStrip.width : 0) + (columns.hasLeft ? columns.leftWidth : 0),
                            toolOptions.y + toolOptions.height,
                            ui.width - (toolStrip.visible ? toolStrip.width : 0) - (columns.hasLeft ? columns.leftWidth : 0)
                                     - (columns.hasRight ? columns.rightWidth : 0),
                            timelineSpace.y - toolOptions.y - toolOptions.height)
        focus: true
        onPressed: window.timelineEdit = false

        // Trackpad / touch pinch zoom.
        PinchHandler {
            target: null
            onScaleChanged: delta => canvasView.zoomBy(delta, centroid.position)
        }
    }

    // All chrome floats over the canvas. Shift+Tab hides it all (the corner
    // button brings it back), Tab only the timeline.
    Item {
        id: ui

        property bool hidden: false
        readonly property bool docked: preferencesDialog.panelStyle === 1
        // Panel sizes the owner has dragged them to (remembered).
        property real sidebarWidth: 248
        property real leftWidth: 230 // the left column of panels
        property real layersHeight: 320
        property real colorsHeight: -1 // dragged smaller than its contents (-1: not)
        property string panelLayout: "" // which panel is in which column, as JSON
        property string liveSections: "{}" // the folded sections of the particle settings, as JSON
        property string smearSections: "{}" // ... and of a Smear group's
        property bool timelineHidden: false
        // A live layer becoming current brings its settings forward when they are a tab.
        readonly property bool liveNow: appEditor.liveLayer
        readonly property bool smearNow: appEditor.smearState.current ?? false
        onSmearNowChanged: if (smearNow) columns.reveal("live")
        property int liveField: -1 // the field being edited, shared by the panel and the canvas
        property int liveOverlay: 0 // the particle and smear controls on the canvas: 0 shown, 1 faded, 2 hidden
        onLiveNowChanged: if (liveNow) columns.reveal("live")
        // 1 shown, 0 hidden; the timeline slides down, the sidebar and the
        // size label above it follow.
        property real timelineShown: timelineHidden ? 0 : 1
        Behavior on timelineShown { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

        anchors.fill: parent
        opacity: hidden ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

        // The top row replaces the system title bar: the tabs on the left,
        // the main bar on the right (the tabs scroll when they don't fit).
        // Without the macOS menu bar, the menus get a strip above it.
        TitleBar {
            id: titleBar
            window: window
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            menuStrip: !window.nativeMenus
            stripContent: MenuBarRow {
                id: menuBarRow
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                menus: appMenus.menus
                onChosen: (action, arg) => appMenus.run(action, arg)
            }

            DocumentTabs {
                id: documentTabs
                x: titleBar.leftInset + Theme.margin
                anchors.verticalCenter: parent.verticalCenter
                // Always leaves a gap to drag the window by.
                width: Math.max(0, Math.min(implicitWidth, topBar.x - x - Theme.margin - 120))
                editor: appEditor
                onCloseRequested: index => window.closeDocument(index)
                onCloseOthersRequested: index => window.closeOthers(index)
                onCloseToTheRightRequested: index => window.closeToTheRight(index)
                onNewRequested: newSpriteDialog.open()
            }

            TopBar {
                id: topBar
                x: parent.width - titleBar.rightInset - Theme.margin - width
                anchors.verticalCenter: parent.verticalCenter
                editor: appEditor
                nativeMenus: window.nativeMenus
                onChosen: (action, arg) => appMenus.run(action, arg)
            }
        }

        // Over the canvas, under the panels: the size Sprite Size is showing.
        Rectangle {
            visible: spriteSizeDialog.visible && spriteSizeDialog.valid
            x: canvasView.offset.x - 1
            y: canvasView.offset.y - 1
            width: spriteSizeDialog.spriteWidth * canvasView.zoom + 2
            height: spriteSizeDialog.spriteHeight * canvasView.zoom + 2
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
        }
        TransformHandles {
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
        }
        LassoOverlay {
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
        }
        StabilizerOverlay {
            anchors.fill: parent
            canvas: canvasView
        }
        // The size of the shape or selection being dragged out, by the pointer
        // (below right of it; flipped near the canvas's right / bottom edge).
        Rectangle {
            id: dragSize
            visible: appEditor.dragInfo !== ""
            readonly property point at: canvasView.hoverPosition
            x: at.x + 16 + width > parent.width ? at.x - 12 - width : at.x + 16
            y: at.y + 20 + height > parent.height ? at.y - 12 - height : at.y + 20
            z: 20
            width: dragSizeText.implicitWidth + 12
            height: dragSizeText.implicitHeight + 6
            radius: 4
            color: Theme.panelSolid
            border.color: Theme.panelBorder
            Text {
                id: dragSizeText
                anchors.centerIn: parent
                text: appEditor.dragInfo
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall
                font.features: { "tnum": 1 } // figures don't jiggle as they change
            }
        }
        GradientOverlay {
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
        }
        SliceOverlay {
            id: sliceOverlay
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
        }
        GridHandles {
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
            visible: !canvasSizer.active && !appEditor.floating
        }
        SymmetryHandles {
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
            enabledHere: !canvasSizer.active && !appEditor.floating
        }
        LiveEmitterHandle {
            anchors.fill: parent
            field: ui.liveField
            mode: ui.liveOverlay
            onFieldPicked: index => ui.liveField = index
            editor: appEditor
            canvas: canvasView
            enabledHere: !canvasSizer.active && !appEditor.floating
        }
        SmearHandles {
            anchors.fill: parent
            mode: ui.liveOverlay // shared with the particle controls (the same panel spot and key)
            editor: appEditor
            canvas: canvasView
            enabledHere: !canvasSizer.active && !appEditor.floating
        }
        SpriteSheetHandles {
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
            dialog: spriteSheetDialog
        }
        // Below every panel (its shade and click guard fill the window).
        CanvasSizeHandles {
            id: canvasSizer
            z: -1
            anchors.fill: parent
            editor: appEditor
            canvas: canvasView
        }

        ToolOptions {
            id: toolOptions
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            editor: appEditor
            canvas: canvasView
            suppressed: canvasSizer.active
        }
        CanvasSizePanel {
            anchors.top: titleBar.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: Theme.spacing
            sizer: canvasSizer
        }

        // Docked: the columns' backgrounds, flush with the window's edges
        // (under the panels, which then draw no cards of their own).
        Rectangle {
            visible: ui.docked && toolStrip.visible
            x: 0
            y: columns.columnTop
            width: toolStrip.width
            height: columns.columnBottom - columns.columnTop
            color: Theme.panel
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.separator }
        }
        Rectangle {
            visible: ui.docked && columns.hasLeft
            x: columns.leftX
            y: columns.columnTop
            width: columns.leftWidth
            height: columns.columnBottom - columns.columnTop
            color: Theme.panel
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.separator }
        }
        Rectangle {
            visible: ui.docked && columns.hasRight
            x: columns.rightX - columns.rightWidth
            y: columns.columnTop
            width: columns.rightWidth
            height: columns.columnBottom - columns.columnTop
            color: Theme.panel
            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.separator }
        }

        // The tools: a rail on the left edge; wraps into more columns when
        // the window is short.
        ToolStrip {
            id: toolStrip
            visible: preferencesDialog.showTools
            preferredColumns: preferencesDialog.toolColumns
            onColumnsRequested: columns => preferencesDialog.toolColumns = columns
            docked: ui.docked
            x: ui.docked ? 0 : Theme.margin
            y: columns.columnTop
            availableHeight: columns.columnBottom - columns.columnTop
            editor: appEditor
        }

        // The side panels: two columns (arranged by PanelColumns, which gives
        // each its place). The tools stay a rail on the left edge, and the left
        // column starts beside it.
        LayersPanel {
            id: layersPanel
            visible: preferencesDialog.showLayers && columns.isActive("layers")
            preferredHeight: ui.layersHeight
            canvas: canvasView
            x: columns.xOf("layers")
            y: columns.yOf("layers")
            width: columns.widthOf("layers")
            docked: ui.docked
            dockEdge: ui.docked && !columns.firstOf("layers") ? Qt.TopEdge : 0
            height: columns.heightOf("layers")
            z: columns.dragId === "layers" ? 50 : 0
            opacity: columns.dragId === "layers" ? 0.85 : 1
            editor: appEditor
            onTilesetRequested: convert => tilesetDialog.openFor(convert)
        }
        TagsPanel {
            id: tagsPanel
            visible: preferencesDialog.showTags && columns.isActive("tags")
            x: columns.xOf("tags")
            y: columns.yOf("tags")
            width: columns.widthOf("tags")
            docked: ui.docked
            dockEdge: ui.docked && !columns.firstOf("tags") ? Qt.TopEdge : 0
            height: columns.heightOf("tags")
            z: columns.dragId === "tags" ? 50 : 0
            opacity: columns.dragId === "tags" ? 0.85 : 1
            editor: appEditor
        }
        ColorPanel {
            id: colorPanel
            visible: preferencesDialog.showColors && columns.isActive("colors")
            userHeight: ui.colorsHeight
            x: columns.xOf("colors")
            y: columns.yOf("colors")
            width: columns.widthOf("colors")
            docked: ui.docked
            dockEdge: ui.docked && !columns.firstOf("colors") ? Qt.TopEdge : 0
            height: columns.heightOf("colors")
            z: columns.dragId === "colors" ? 50 : 0
            opacity: columns.dragId === "colors" ? 0.85 : 1
            canvas: canvasView
            editor: appEditor
            onLoadPaletteRequested: loadPaletteDialog.open()
            onSavePaletteRequested: savePaletteDialog.open()
        }
        PreviewPanel {
            id: previewPanel
            visible: preferencesDialog.showPreview && columns.isActive("preview")
            x: columns.xOf("preview")
            y: columns.yOf("preview")
            width: columns.widthOf("preview")
            docked: ui.docked
            dockEdge: ui.docked && !columns.firstOf("preview") ? Qt.TopEdge : 0
            height: columns.heightOf("preview")
            z: columns.dragId === "preview" ? 50 : 0
            opacity: columns.dragId === "preview" ? 0.85 : 1
            maxHeight: columns.heightOf("preview")
            maxViewHeight: ui.height
            editor: appEditor
            canvas: canvasView
        }
        LivePanel {
            id: livePanel
            visible: appEditor.liveLayer && columns.isActive("live")
            x: columns.xOf("live")
            y: columns.yOf("live")
            width: columns.widthOf("live")
            docked: ui.docked
            dockEdge: ui.docked && !columns.firstOf("live") ? Qt.TopEdge : 0
            height: columns.heightOf("live")
            z: columns.dragId === "live" ? 50 : 0
            opacity: columns.dragId === "live" ? 0.85 : 1
            editor: appEditor
            field: ui.liveField
            onFieldPicked: index => ui.liveField = index
            overlayMode: ui.liveOverlay
            onOverlayCycled: ui.liveOverlay = (ui.liveOverlay + 1) % 3
            collapsedJson: ui.liveSections
            onCollapsedEdited: json => ui.liveSections = json
        }
        // A Smear group's settings, in the same place.
        SmearPanel {
            id: smearPanel
            visible: ui.smearNow && !appEditor.liveLayer && columns.isActive("live")
            x: columns.xOf("live")
            y: columns.yOf("live")
            width: columns.widthOf("live")
            docked: ui.docked
            dockEdge: ui.docked && !columns.firstOf("live") ? Qt.TopEdge : 0
            height: columns.heightOf("live")
            z: columns.dragId === "live" ? 50 : 0
            opacity: columns.dragId === "live" ? 0.85 : 1
            editor: appEditor
            overlayMode: ui.liveOverlay
            onOverlayCycled: ui.liveOverlay = (ui.liveOverlay + 1) % 3
            collapsedJson: ui.smearSections
            onCollapsedEdited: json => ui.smearSections = json
        }
        PanelColumns {
            id: columns
            anchors.fill: parent
            panels: ({ layers: layersPanel, colors: colorPanel, preview: previewPanel, live: ui.smearNow ? smearPanel : livePanel, tags: tagsPanel })
            wanted: ({ layers: preferencesDialog.showLayers, colors: preferencesDialog.showColors, preview: preferencesDialog.showPreview,
                       live: appEditor.liveLayer || ui.smearNow, tags: preferencesDialog.showTags })
            liveName: ui.smearNow ? qsTr("Smear") : qsTr("Particles")
            layoutJson: ui.panelLayout
            gap: ui.docked ? 0 : Theme.spacing
            fill: ui.docked
            docked: ui.docked
            columnTop: toolOptions.y + toolOptions.height + (ui.docked ? 0 : Theme.margin)
            columnBottom: timelineSpace.y - (ui.docked ? 0 : Theme.margin)
            leftX: ui.docked ? (toolStrip.visible ? toolStrip.width : 0)
                             : (toolStrip.visible ? toolStrip.x + toolStrip.width + Theme.spacing : Theme.margin)
            rightX: ui.width - (ui.docked ? 0 : Theme.margin)
            leftWidth: ui.leftWidth
            rightWidth: ui.sidebarWidth
            onLayoutJsonEdited: json => ui.panelLayout = json
            onLeftWidthRequested: v => ui.leftWidth = v
            onRightWidthRequested: v => ui.sidebarWidth = v
            onHeightRequested: (id, h) => {
                if (id === "layers")
                    ui.layersHeight = h
                else if (id === "colors")
                    ui.colorsHeight = h
                else if (id === "preview")
                    previewPanel.viewHeight = Math.round(Math.max(previewPanel.minView, h - previewPanel.headerOnlyHeight - 6))
            }
        }

        // What the timeline takes at the bottom (none while hidden).
        Item {
            id: timelineSpace
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: statusBar.top
            height: timeline.height * ui.timelineShown
        }

        StatusBar {
            id: statusBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            editor: appEditor
            canvas: canvasView
            onSpriteSizeRequested: spriteSizeDialog.open()
            onColorModeRequested: colorModeDialog.open()
        }

        // Docked: flush with the window's left, right and bottom edges.
        Timeline {
            id: timeline
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: timelineSpace.top
            docked: true
            visible: ui.timelineShown > 0
            maximumHeight: window.height * 0.6
            editor: appEditor
            layerProps: !layersPanel.visible
            focused: window.timelineEdit
            onInteracted: window.timelineEdit = true
            onTilesetRequested: convert => tilesetDialog.openFor(convert)
        }
    }

    // Tile mode: the cell the pencil / eraser / eyedropper / fill / line /
    // rectangle would use (as big as the stamp for the pencil), with what the
    // pencil would place faintly in it, or the cells the eyedropper drags over.
    Rectangle {
        id: tileCursor
        readonly property bool shown: appEditor.tilesActive && canvasView.hovering
            && (appEditor.tool === Editor.Pencil || appEditor.tool === Editor.Eraser || appEditor.tool === Editor.Eyedropper
                || appEditor.tool === Editor.Fill || appEditor.tool === Editor.Line || appEditor.tool === Editor.Rectangle
                || appEditor.tool === Editor.Ellipse)
        readonly property rect picked: appEditor.tilePickRect
        readonly property bool picking: picked.width > 0
        readonly property bool placing: appEditor.tool === Editor.Pencil
        readonly property size stamp: appEditor.tileStamp
        readonly property rect cell: shown ? appEditor.tileCellAt(canvasView.hoverPixel) : Qt.rect(0, 0, 0, 0)
        readonly property rect area: picking ? picked
            : Qt.rect(cell.x, cell.y, cell.width * (placing && stamp.width > 1 ? stamp.width : 1),
                      cell.height * (placing && stamp.height > 1 ? stamp.height : 1))
        visible: (shown || picking) && area.width > 0
        x: Math.round(canvasView.offset.x + area.x * canvasView.zoom)
        y: Math.round(canvasView.offset.y + area.y * canvasView.zoom)
        width: Math.round(area.width * canvasView.zoom)
        height: Math.round(area.height * canvasView.zoom)
        color: "transparent"
        border.color: Theme.accent
        border.width: 2

        ImagePreview {
            anchors.fill: parent
            anchors.margins: 2
            opacity: 0.6
            visible: tileCursor.placing && !tileCursor.picking
            image: {
                const revision = appEditor.tilesetRevision
                const size = appEditor.tileStamp // also changes when a stamp is picked
                const flips = appEditor.tileFlips
                return revision >= 0 && size.width >= 0 && flips >= 0 ? appEditor.tileCursorImage() : appEditor.tileCursorImage()
            }
        }
    }

    // The text being typed: where the next letter goes, blinking while typing.
    Rectangle {
        id: textCaret
        readonly property rect box: appEditor.textCaret
        visible: appEditor.textActive && box.width > 0 && blink
        property bool blink: true
        x: Math.round(canvasView.offset.x + box.x * canvasView.zoom)
        y: Math.round(canvasView.offset.y + box.y * canvasView.zoom)
        width: Math.max(1, Math.round(canvasView.zoom))
        height: Math.round(box.height * canvasView.zoom)
        color: Theme.accent
        // Steady while the typing changes, then blinks.
        Timer {
            interval: 530
            repeat: true
            running: appEditor.textActive
            onTriggered: textCaret.blink = !textCaret.blink
        }
        Connections {
            target: appEditor
            function onTextChanged() { textCaret.blink = true }
        }
    }

    // Cmd / Ctrl held over the canvas: the layer a click would pick.
    Rectangle {
        visible: canvasView.layerPicking
        x: Math.round(canvasView.hoverPosition.x + 16)
        y: Math.round(canvasView.hoverPosition.y + 18)
        width: pickLabel.implicitWidth + 14
        height: pickLabel.implicitHeight + 8
        radius: Theme.radiusSmall - 2
        color: Theme.tooltip
        border.color: Theme.panelBorder
        Text {
            id: pickLabel
            anchors.centerIn: parent
            text: canvasView.layerPickName !== "" ? canvasView.layerPickName : qsTr("No layer here")
            color: canvasView.layerPickName !== "" ? Theme.text : Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }
    }

    // Dragging the brush size (Ctrl+Alt): the size, next to where the drag began.
    Rectangle {
        visible: canvasView.resizingBrush
        x: Math.round(canvasView.hoverPosition.x + 16)
        y: Math.round(canvasView.hoverPosition.y + 18)
        width: sizeLabel.implicitWidth + 14
        height: sizeLabel.implicitHeight + 8
        radius: Theme.radiusSmall - 2
        color: Theme.tooltip
        border.color: Theme.panelBorder
        Text {
            id: sizeLabel
            anchors.centerIn: parent
            text: canvasView.turningBrush ? qsTr("Brush angle %1\u00b0").arg(appEditor.brushAngle)
                  : appEditor.brushShape === Editor.CustomBrush ? qsTr("Brush scale %1").arg(appEditor.customBrushScaleText)
                  : qsTr("Brush size %1").arg(appEditor.brushSize)
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }
    }

    // With every panel hidden, the one way back that's always in sight.
    Panel {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.margin
        width: Theme.buttonSize + 2 * padding
        height: Theme.buttonSize + 2 * padding
        padding: 4
        opacity: ui.hidden ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

        IconButton {
            iconName: "minimize"
            tip: "Show panels" + Shortcuts.hints.hideUi
            onClicked: ui.hidden = false
        }
    }

    // Rebindable shortcuts take their keys from Shortcuts (Preferences);
    // Esc, Enter, Delete / Backspace and the arrows below are fixed.
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.pencil; onActivated: appEditor.tool = Editor.Pencil }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.eraser; onActivated: appEditor.tool = Editor.Eraser }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.fill; onActivated: appEditor.tool = Editor.Fill }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.eyedropper; onActivated: appEditor.tool = Editor.Eyedropper }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.hand; onActivated: appEditor.tool = Editor.Hand }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.blurTool; onActivated: appEditor.tool = Editor.Blur }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.jumbleTool; onActivated: appEditor.tool = Editor.Jumble }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.text; onActivated: appEditor.tool = Editor.Text }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.slice; onActivated: appEditor.tool = Editor.SliceTool }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.sourceTool; onActivated: appEditor.tool = Editor.SourceTool }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.fieldTool; onActivated: appEditor.tool = Editor.FieldTool }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.gradient; onActivated: appEditor.tool = Editor.Gradient }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.tileMode; onActivated: appEditor.editTiles = !appEditor.editTiles }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.line; onActivated: appEditor.tool = Editor.Line }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.rectangle; onActivated: appEditor.tool = Editor.Rectangle }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.ellipse; onActivated: appEditor.tool = Editor.Ellipse }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.select; onActivated: appEditor.tool = Editor.Select }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.lasso; onActivated: appEditor.tool = Editor.Lasso }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.magicWand; onActivated: appEditor.tool = Editor.MagicWand }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.move; onActivated: appEditor.tool = Editor.Move }
    Shortcut { sequences: Shortcuts.keys.selectAll; onActivated: window.selectAllHere() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.transform; onActivated: appEditor.startTransform() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.brushFromSelection; onActivated: appEditor.brushFromSelection() }
    Shortcut { sequences: Shortcuts.keys.spriteSize; onActivated: spriteSizeDialog.open() }
    Shortcut { sequences: Shortcuts.keys.canvasSize; onActivated: canvasSizer.start() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.flipHorizontal; onActivated: appEditor.flipHorizontal() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.flipVertical; onActivated: appEditor.flipVertical() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.rotateClockwise; onActivated: appEditor.rotate90(true) }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.rotateCounterclockwise; onActivated: appEditor.rotate90(false) }
    Shortcut {
        enabled: !window.typing
        sequences: Shortcuts.keys.copy
        onActivated: window.timelineEdit ? appEditor.copyTimeline() : appEditor.copy()
    }
    Shortcut {
        enabled: !window.typing
        sequences: Shortcuts.keys.cut
        onActivated: window.timelineEdit ? appEditor.cutTimeline() : appEditor.cut()
    }
    Shortcut {
        enabled: !window.typing
        sequences: Shortcuts.keys.paste
        onActivated: window.timelineEdit ? appEditor.pasteTimeline() : window.pasteHere()
    }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.copyMerged; onActivated: appEditor.copyMerged() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.pasteNewLayer; onActivated: window.pasteHereAsNewLayer() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.pasteInPlace; onActivated: appEditor.paste() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.pasteNewSprite; onActivated: appEditor.pasteAsNewSprite() }
    Shortcut { sequences: Shortcuts.keys.deselect; onActivated: appEditor.deselect() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.outline; onActivated: window.startEffect("outline") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.replaceColor; onActivated: window.startEffect("replace") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.adjustHsl; onActivated: window.startEffect("adjust") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.blur; onActivated: window.startEffect("blur") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.convolve; onActivated: window.startEffect("convolve") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.cleanup; onActivated: window.startEffect("cleanup") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.curves; onActivated: window.startEffect("curves") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.posterize; onActivated: window.startEffect("posterize") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.dropShadow; onActivated: window.startEffect("shadow") }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.invertSelection; onActivated: appEditor.invertSelection() }
    Shortcut {
        enabled: !window.typing
        sequence: "Escape"
        onActivated: canvasSizer.active ? canvasSizer.cancel()
                   : livePanel.stop >= 0 ? livePanel.disarm()
                   : appEditor.polygonActive ? appEditor.cancelPolygon()
                   : appEditor.gradientEditable ? appEditor.cancelGradient()
                   // Lifted pixels (Cmd+T, a moved selection) land on Esc; a paste is dropped.
                   : appEditor.floating ? (appEditor.floatingLifted ? appEditor.commitFloating() : appEditor.cancelFloating())
                   // The timeline clicked last: its selection goes (the canvas's stays).
                   : (window.timelineEdit && appEditor.rangeType !== Editor.NoRange) || appEditor.layerPicks > 1
                       ? appEditor.clearRange()
                   : appEditor.deselect()
    }
    Shortcut {
        enabled: !window.typing
        sequences: ["Delete", "Backspace"]
        // Backspace also takes back a polygon lasso's last corner; with the
        // slice tool it deletes the picked slice.
        onActivated: appEditor.polygonActive ? appEditor.removePolygonCorner()
                   : appEditor.tool === Editor.SliceTool && appEditor.currentSlice >= 0 ? appEditor.removeSlice(appEditor.currentSlice)
                   // What was clicked last: the timeline's frames / cels, or the canvas's
                   // pixels (never the other one's selection).
                   : window.timelineEdit && !appEditor.floating ? appEditor.deleteInTimeline()
                   : appEditor.deleteOnCanvas()
    }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.swapColors; onActivated: appEditor.swapColors() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.brushSmaller; onActivated: appEditor.brushSize -= 1 }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.brushBigger; onActivated: appEditor.brushSize += 1 }

    Shortcut { sequences: Shortcuts.keys.newSprite; onActivated: newSpriteDialog.open() }
    Shortcut { sequences: Shortcuts.keys.preferences; onActivated: preferencesDialog.open() }
    Shortcut { sequences: Shortcuts.keys.closeTab; onActivated: window.closeDocument(appEditor.currentDocument) }
    Shortcut {
        sequences: Shortcuts.keys.nextTab
        onActivated: appEditor.currentDocument = (appEditor.currentDocument + 1) % appEditor.documents.length
    }
    Shortcut {
        sequences: Shortcuts.keys.previousTab
        onActivated: appEditor.currentDocument = (appEditor.currentDocument + appEditor.documents.length - 1) % appEditor.documents.length
    }
    Shortcut { sequences: Shortcuts.keys.open; onActivated: window.openDocument() }
    Shortcut { sequences: Shortcuts.keys.save; onActivated: window.save() }
    Shortcut { sequences: Shortcuts.keys.saveAs; onActivated: window.saveAs() }
    Shortcut { sequences: Shortcuts.keys.export; onActivated: exportPopup.open() }

    Shortcut { sequences: Shortcuts.keys.undo; onActivated: appEditor.undo() }
    Shortcut { sequences: Shortcuts.keys.redo; onActivated: appEditor.redo() }

    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.zoomIn; onActivated: canvasView.zoomIn() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.zoomOut; onActivated: canvasView.zoomOut() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.fitToView; onActivated: canvasView.fitToView() }
    // Off -> both -> X -> Y -> off.
    Shortcut {
        enabled: !window.typing
        sequences: Shortcuts.keys.tiledView
        onActivated: appEditor.tileMode = [3, 2, 0, 1][appEditor.tileMode]
    }
    Shortcut { sequences: Shortcuts.keys.snapToGrid; onActivated: appEditor.snapToGrid = !appEditor.snapToGrid }
    Shortcut { sequences: Shortcuts.keys.customGrid; onActivated: canvasView.showCustomGrid = !canvasView.showCustomGrid }
    Shortcut { sequences: Shortcuts.keys.pixelGrid; onActivated: canvasView.showGrid = !canvasView.showGrid }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.hideUi; onActivated: ui.hidden = !ui.hidden }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.layersPanel; onActivated: preferencesDialog.showLayers = !preferencesDialog.showLayers }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.tagsPanel; onActivated: preferencesDialog.showTags = !preferencesDialog.showTags }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.colorsPanel; onActivated: preferencesDialog.showColors = !preferencesDialog.showColors }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.liveOverlay; onActivated: ui.liveOverlay = (ui.liveOverlay + 1) % 3 }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.previewPanel; onActivated: preferencesDialog.showPreview = !preferencesDialog.showPreview }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.hideTimeline; onActivated: ui.timelineHidden = !ui.timelineHidden }

    Shortcut {
        enabled: !window.typing
        sequences: ["Return", "Enter"]
        onActivated: canvasSizer.active ? canvasSizer.apply()
                   : appEditor.polygonActive ? appEditor.closePolygon()
                   : appEditor.gradientEditable ? appEditor.finishGradient()
                   : appEditor.floating ? appEditor.commitFloating() : (appEditor.playing = !appEditor.playing)
    }
    // Arrows nudge the selected pixels (Shift: 10 px; Alt: only the selection)
    // unless the timeline was clicked last; otherwise Left / Right step frames and
    // Up / Down move to the layer above / below (the list runs bottom to top).
    function arrow(dx, dy, step, outline) {
        if ((appEditor.floating || !window.timelineEdit) && appEditor.nudge(dx * step, dy * step, outline))
            return
        if (step !== 1 || outline)
            return
        if (dy === 0)
            dx < 0 ? appEditor.previousFrame() : appEditor.nextFrame()
        else if (dx === 0)
            appEditor.currentLayer = Math.max(0, Math.min(appEditor.layers.length - 1, appEditor.currentLayer - dy))
    }
    Shortcut { enabled: !window.typing; sequence: "Left"; onActivated: window.arrow(-1, 0, 1, false) }
    Shortcut { enabled: !window.typing; sequence: "Right"; onActivated: window.arrow(1, 0, 1, false) }
    Shortcut { enabled: !window.typing; sequence: "Up"; onActivated: window.arrow(0, -1, 1, false) }
    Shortcut { enabled: !window.typing; sequence: "Down"; onActivated: window.arrow(0, 1, 1, false) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Left"; onActivated: window.arrow(-1, 0, 10, false) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Right"; onActivated: window.arrow(1, 0, 10, false) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Up"; onActivated: window.arrow(0, -1, 10, false) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Down"; onActivated: window.arrow(0, 1, 10, false) }
    Shortcut { enabled: !window.typing; sequence: "Alt+Left"; onActivated: window.arrow(-1, 0, 1, true) }
    Shortcut { enabled: !window.typing; sequence: "Alt+Right"; onActivated: window.arrow(1, 0, 1, true) }
    Shortcut { enabled: !window.typing; sequence: "Alt+Up"; onActivated: window.arrow(0, -1, 1, true) }
    Shortcut { enabled: !window.typing; sequence: "Alt+Down"; onActivated: window.arrow(0, 1, 1, true) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Alt+Left"; onActivated: window.arrow(-1, 0, 10, true) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Alt+Right"; onActivated: window.arrow(1, 0, 10, true) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Alt+Up"; onActivated: window.arrow(0, -1, 10, true) }
    Shortcut { enabled: !window.typing; sequence: "Shift+Alt+Down"; onActivated: window.arrow(0, 1, 10, true) }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.newFrame; onActivated: appEditor.addFrame() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.duplicateFrame; onActivated: appEditor.duplicateFrame() }
    Shortcut {
        enabled: !window.typing
        sequences: Shortcuts.keys.alphaLock
        onActivated: {
            const layer = appEditor.layers[appEditor.currentLayer]
            if (layer && layer.kind === "image")
                appEditor.setLayerAlphaLock(appEditor.currentLayer, !layer.alphaLock)
        }
    }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.groupLayers; onActivated: appEditor.groupLayers() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.ungroupLayer; onActivated: appEditor.ungroupLayer() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.newLayer; onActivated: appEditor.addLayer() }
    Shortcut { enabled: !window.typing; sequences: Shortcuts.keys.onionSkin; onActivated: appEditor.onionSkin = !appEditor.onionSkin }
}
