import QtQuick
import Smalto

// The menus (File, Edit, Sprite, Effects, View) described once: the
// in-window ones (`ModelMenu`, Windows and Linux) and the macOS menu bar
// (`NativeMenuBar`) are both made from `menus`, so they can't drift apart.
// An entry is `{ text, action, arg, key, enabled, checked, role, hint }` or
// `{ separator: true }`; `key` is a shortcut id (the keys follow the user's
// rebinding), `checked` marks a switch, `role` ("preferences", "quit") lets
// the macOS menu bar move it to the app menu. `run(action, arg)` does what an
// entry says; what belongs to the window is passed on by `windowAction`.
QtObject {
    id: root

    required property Editor editor
    required property CanvasView canvas

    signal windowAction(string action, var arg)

    function run(action: string, arg: var) {
        switch (action) {
        case "copyMerged": editor.copyMerged(); break
        case "pasteNewLayer": editor.pasteAsNewLayer(); break
        case "pasteNewSprite": editor.pasteAsNewSprite(); break
        case "deselect": editor.deselect(); break
        case "invertSelection": editor.invertSelection(); break
        case "trimCanvas": editor.trimCanvas(); break
        case "zoomIn": canvas.zoomIn(); break
        case "zoomOut": canvas.zoomOut(); break
        case "fitToView": canvas.fitToView(); break
        case "pixelGrid": canvas.showGrid = !canvas.showGrid; break
        case "documentGrid": canvas.showCustomGrid = !canvas.showCustomGrid; break
        case "snapToGrid": editor.snapToGrid = !editor.snapToGrid; break
        case "clearRecent": RecentFiles.clear(); break
        case "tool": editor.tool = arg; break
        // layers (of the current one)
        case "newLayer": editor.addLayer(); break
        case "newGroup": editor.addGroup(); break
        case "newParticles": editor.addParticleLayer(); break
        case "quickEffect": editor.addQuickEffect(arg); break
        case "bakeLayer": editor.bakeLayer(); break
        case "bakeToNewLayer": editor.bakeToNewLayer(); break
        case "duplicateLayer": editor.duplicateLayer(); break
        case "copyLayers": editor.copyLayers(); break
        case "pasteLayers": editor.pasteLayers(); break
        case "deleteLayer": editor.removeLayer(); break
        case "groupLayers": editor.groupLayers(); break
        case "smearLayers": editor.smearLayers(); break
        case "ungroupLayer": editor.ungroupLayer(); break
        case "convertToImage": editor.convertToImageLayer(); break
        case "layerVisible": editor.setLayerVisible(editor.currentLayer, !(editor.layers[editor.currentLayer] ?? {}).visible); break
        case "layerLocked": editor.setLayerLocked(editor.currentLayer, !(editor.layers[editor.currentLayer] ?? {}).locked); break
        case "layerAlphaLock": editor.setLayerAlphaLock(editor.currentLayer, !(editor.layers[editor.currentLayer] ?? {}).alphaLock); break
        case "selectLayerPixels": editor.selectLayerPixels(editor.currentLayer); break
        // frames and cels
        case "newFrame": editor.addFrame(); break
        case "duplicateFrame": editor.duplicateFrame(); break
        case "deleteFrame": editor.removeFrame(); break
        case "reverseFrames": editor.reverseFrames(); break
        case "nextFrame": editor.nextFrame(); break
        case "previousFrame": editor.previousFrame(); break
        case "firstFrame": editor.currentFrame = 0; break
        case "lastFrame": editor.currentFrame = editor.frameCount - 1; break
        case "play": editor.playing = !editor.playing; break
        case "onionSkin": editor.onionSkin = !editor.onionSkin; break
        case "newTag": editor.addTag(); break
        case "clearCels": editor.clearCels(); break
        case "clearLayer": editor.clearLayer(); break
        case "linkCels": editor.linkCels(); break
        case "unlinkCels": editor.unlinkCels(); break
        case "tileMode": editor.tileMode = arg; break
        case "switchDocument": editor.currentDocument = arg; break
        default: windowAction(action, arg) // the window's own
        }
    }

    readonly property var menus: build()

    // For the macOS menu bar, which must not be remade when only a tick, a
    // name or an enabled state changes (the bar would flash on every shortcut):
    // `structure` is `menus` without those, and changes only when entries come
    // or go; `states` maps an entry's id to its current `{ text, enabled,
    // checked }`, and the bar's entries follow it in place.
    property var structure: []
    property var states: ({})
    property string structureJson: ""
    // The current texts of the destructive entries (the native menu bar colors them).
    property var dangerTexts: []
    onMenusChanged: refresh()
    Component.onCompleted: refresh()

    function entryId(item: var): string { return item.action + ":" + (item.arg === undefined ? "" : item.arg) }
    function refresh() {
        const nextStates = {}
        const dangerList = []
        const strip = (item) => {
            if (item.separator)
                return item
            const id = entryId(item)
            nextStates[id] = { text: item.text, enabled: item.enabled !== false, checked: item.checked === true }
            if (item.danger === true)
                dangerList.push(item.text)
            const out = { id: id, action: item.action, arg: item.arg, key: item.key, seq: item.seq, role: item.role,
                          hint: item.hint, checkable: item.checked !== undefined }
            if (item.submenu) {
                out.text = item.text // a submenu's name doesn't change
                out.submenu = item.submenu.map(strip)
            }
            return out
        }
        const next = menus.map(m => ({ title: m.title, nativeOnly: m.nativeOnly === true, items: m.items.map(strip) }))
        dangerTexts = dangerList
        states = nextStates // first, so entries made below start out right
        const json = JSON.stringify(next)
        if (json !== structureJson) {
            structureJson = json
            structure = next
        }
    }

    // The ready-made effects, then the user's own after a divider.
    function quickEffects(): var {
        const saved = editor.userPresets // made again when they change
        const out = []
        let divided = false
        for (const p of editor.quickEffectList()) {
            if (p.user && !divided) {
                out.push({ separator: true })
                divided = true
            }
            out.push({ text: qsTr("Quick %1").arg(p.name), action: "quickEffect", arg: p.id })
        }
        return out
    }

    function build(): var {
        const sep = { separator: true }
        const recent = RecentFiles.items.map(r => ({ text: r.name, hint: r.folder, action: "openPath", arg: r.path }))
        const layer = editor.layers[editor.currentLayer] ?? {}
        const frames = editor.rangeType === Editor.FrameRange ? editor.range.width : 1
        const manyFrames = frames > 1
        const cels = editor.rangeType === Editor.CelRange ? editor.range.width : 1
        const tool = (text, key, tool) => ({ text: text, action: "tool", arg: tool, key: key, checked: editor.tool === tool })
        const tile = (text, mode) => ({ text: text, action: "tileMode", arg: mode, checked: editor.tileMode === mode })
        const documents = editor.documents.map((d, i) => ({ text: d.name + (d.modified ? " \u2022" : ""), action: "switchDocument",
                                                             arg: i, checked: i === editor.currentDocument }))
        return [
            { title: qsTr("File"), items: [
                { text: qsTr("New Sprite\u2026"), action: "new", key: "newSprite" },
                { text: qsTr("Open\u2026"), action: "open", key: "open" },
                ...(recent.length > 0 ? [{ text: qsTr("Open Recent"), submenu: [...recent, sep, { text: qsTr("Clear Recent Files"), action: "clearRecent", danger: true }] }] : []),
                { text: qsTr("Import Image as Layer\u2026"), action: "import" },
                { text: qsTr("Import Tileset Image\u2026"), action: "importTileset" },
                sep,
                { text: qsTr("Save"), action: "save", key: "save" },
                { text: qsTr("Save As\u2026"), action: "saveAs", key: "saveAs" },
                sep,
                { text: qsTr("Export\u2026"), action: "export", key: "export" },
                { text: qsTr("Export Tileset\u2026"), action: "exportTileset", enabled: layer.kind === "tilemap" },
                sep,
                { text: qsTr("Close Tab"), action: "closeTab", key: "closeTab" },
                sep,
                { text: qsTr("Preferences\u2026"), action: "preferences", key: "preferences", role: "preferences" },
                { text: qsTr("About Smalto"), action: "about", role: "about" },
                { text: qsTr("Quit Smalto"), action: "quit", role: "quit" },
            ] },
            { title: qsTr("Edit"), items: [
                { text: qsTr("Cut"), action: "cut", key: "cut" },
                { text: qsTr("Copy"), action: "copy", key: "copy" },
                { text: qsTr("Copy Merged"), action: "copyMerged", key: "copyMerged" },
                { text: qsTr("Paste"), action: "paste", key: "paste", enabled: editor.canPaste },
                { text: qsTr("Paste as New Layer"), action: "pasteNewLayer", key: "pasteNewLayer", enabled: editor.canPaste },
                { text: qsTr("Paste as New Sprite"), action: "pasteNewSprite", key: "pasteNewSprite", enabled: editor.canPaste },
                sep,
                { text: qsTr("Select All"), action: "selectAll", key: "selectAll" },
                { text: qsTr("Deselect"), action: "deselect", key: "deselect", enabled: editor.hasSelection },
                { text: qsTr("Invert Selection"), action: "invertSelection", key: "invertSelection" },
            ] },
            { title: qsTr("Sprite"), items: [
                { text: qsTr("Sprite Size\u2026"), action: "spriteSize", key: "spriteSize" },
                { text: qsTr("Canvas Size\u2026"), action: "canvasSize", key: "canvasSize" },
                { text: qsTr("Trim Canvas to Content"), action: "trimCanvas" },
                { text: qsTr("Color Mode\u2026"), action: "colorMode" },
            ] },
            { title: qsTr("Layer"), items: [
                { text: qsTr("New Layer"), action: "newLayer", key: "newLayer" },
                { text: qsTr("New Group"), action: "newGroup" },
                { text: qsTr("New Particle Layer"), action: "newParticles" },
                { text: qsTr("New Tilemap Layer\u2026"), action: "tilemapNew" },
                { text: qsTr("Import Image as Layer\u2026"), action: "import" },
                sep,
                { text: qsTr("Duplicate Layer"), action: "duplicateLayer" },
                { text: qsTr("Copy Layer"), action: "copyLayers" },
                { text: qsTr("Paste Layer"), action: "pasteLayers", enabled: editor.canPasteLayers },
                sep,
                { text: qsTr("Group Layers"), action: "groupLayers", key: "groupLayers" },
                { text: qsTr("Ungroup"), action: "ungroupLayer", key: "ungroupLayer", enabled: layer.kind === "group" },
                { text: qsTr("Smear"), action: "smearLayers" },
                sep,
                { text: qsTr("Convert to Tilemap\u2026"), action: "tilemapConvert", enabled: layer.kind === "image" && !layer.background },
                { text: qsTr("Convert to Normal Layer"), action: "convertToImage", enabled: layer.kind === "tilemap" },
                { text: qsTr("Bake Layer"), action: "bakeLayer", enabled: editor.liveLayer || (editor.smearState.current ?? false) },
                { text: qsTr("Bake to New Layer"), action: "bakeToNewLayer", enabled: editor.liveLayer || (editor.smearState.current ?? false) },
                sep,
                { text: qsTr("Visible"), action: "layerVisible", checked: layer.visible === true },
                { text: qsTr("Locked"), action: "layerLocked", checked: layer.locked === true },
                { text: qsTr("Alpha Lock"), action: "layerAlphaLock", key: "alphaLock", checked: layer.alphaLock === true, enabled: layer.kind === "image" },
                sep,
                { text: qsTr("Select Pixels"), action: "selectLayerPixels" },
                sep,
                { text: qsTr("Clear Layer"), action: "clearLayer", danger: true, enabled: layer.kind !== "group" && layer.locked !== true },
                { text: qsTr("Delete Layer"), action: "deleteLayer", danger: true, enabled: editor.canRemoveLayer },
            ] },
            { title: qsTr("Frame"), items: [
                { text: qsTr("New Frame"), action: "newFrame", key: "newFrame" },
                { text: manyFrames ? qsTr("Duplicate Frames") : qsTr("Duplicate Frame"), action: "duplicateFrame", key: "duplicateFrame" },
                { text: qsTr("Reverse Frames"), action: "reverseFrames", enabled: manyFrames },
                { text: qsTr("Frame Duration\u2026"), action: "frameDuration" },
                sep,
                { text: qsTr("Next Frame"), action: "nextFrame", enabled: editor.frameCount > 1 },
                { text: qsTr("Previous Frame"), action: "previousFrame", enabled: editor.frameCount > 1 },
                { text: qsTr("First Frame"), action: "firstFrame", enabled: editor.currentFrame > 0 },
                { text: qsTr("Last Frame"), action: "lastFrame", enabled: editor.currentFrame < editor.frameCount - 1 },
                sep,
                { text: editor.playing ? qsTr("Stop Animation") : qsTr("Play Animation"), action: "play", enabled: editor.frameCount > 1 },
                { text: qsTr("Onion Skin"), action: "onionSkin", key: "onionSkin", checked: editor.onionSkin },
                sep,
                { text: qsTr("New Tag from Frames"), action: "newTag" },
                sep,
                { text: qsTr("Link Cels"), action: "linkCels", enabled: cels > 1 },
                { text: qsTr("Unlink Cels"), action: "unlinkCels" },
                sep,
                { text: cels > 1 ? qsTr("Clear Cels") : qsTr("Clear Cel"), action: "clearCels", danger: true },
                { text: manyFrames ? qsTr("Delete Frames") : qsTr("Delete Frame"), action: "deleteFrame", danger: true, enabled: frames < editor.frameCount },
            ] },
            { title: qsTr("Tools"), items: [
                tool(qsTr("Pencil"), "pencil", Editor.Pencil),
                tool(qsTr("Eraser"), "eraser", Editor.Eraser),
                tool(qsTr("Fill"), "fill", Editor.Fill),
                tool(qsTr("Gradient"), "gradient", Editor.Gradient),
                tool(qsTr("Eyedropper"), "eyedropper", Editor.Eyedropper),
                sep,
                tool(qsTr("Blur"), "blurTool", Editor.Blur),
                tool(qsTr("Jumble"), "jumbleTool", Editor.Jumble),
                sep,
                tool(qsTr("Line"), "line", Editor.Line),
                tool(qsTr("Rectangle"), "rectangle", Editor.Rectangle),
                tool(qsTr("Ellipse"), "ellipse", Editor.Ellipse),
                tool(qsTr("Text"), "text", Editor.Text),
                sep,
                tool(qsTr("Select"), "select", Editor.Select),
                tool(qsTr("Lasso"), "lasso", Editor.Lasso),
                tool(qsTr("Magic Wand"), "magicWand", Editor.MagicWand),
                tool(qsTr("Move"), "move", Editor.Move),
                tool(qsTr("Slice"), "slice", Editor.SliceTool),
                sep,
                tool(qsTr("Hand"), "hand", Editor.Hand),
            ] },
            { title: qsTr("Effects"), items: [
                { text: qsTr("Outline\u2026"), action: "effect", arg: "outline", key: "outline" },
                { text: qsTr("Replace Color\u2026"), action: "effect", arg: "replace", key: "replaceColor" },
                { text: qsTr("Hue / Saturation\u2026"), action: "effect", arg: "adjust", key: "adjustHsl" },
                { text: qsTr("Blur\u2026"), action: "effect", arg: "blur", key: "blur" },
                { text: qsTr("Pixel Cleanup\u2026"), action: "effect", arg: "cleanup", key: "cleanup" },
                { text: qsTr("Color Curves\u2026"), action: "effect", arg: "curves", key: "curves" },
                { text: qsTr("Posterize\u2026"), action: "effect", arg: "posterize", key: "posterize" },
                { text: qsTr("Reduce Colors\u2026"), action: "effect", arg: "quantize", enabled: editor.colorMode !== "indexed" },
                { text: qsTr("Drop Shadow\u2026"), action: "effect", arg: "shadow", key: "dropShadow" },
                { text: qsTr("Convolution Matrix\u2026"), action: "effect", arg: "convolve", key: "convolve" },
                sep,
                { text: qsTr("Quick Effects"), submenu: quickEffects() },
            ] },
            { title: qsTr("View"), items: [
                { text: qsTr("Zoom In"), action: "zoomIn", key: "zoomIn" },
                { text: qsTr("Zoom Out"), action: "zoomOut", key: "zoomOut" },
                { text: qsTr("Fit to View"), action: "fitToView", key: "fitToView" },
                sep,
                { text: qsTr("Pixel Grid"), action: "pixelGrid", key: "pixelGrid", checked: canvas.showGrid },
                { text: qsTr("Document Grid"), action: "documentGrid", key: "customGrid", checked: canvas.showCustomGrid },
                { text: qsTr("Snap to Grid"), action: "snapToGrid", key: "snapToGrid", checked: editor.snapToGrid },
                { text: qsTr("Tiled View"), submenu: [tile(qsTr("Off"), 0), tile(qsTr("Across X"), 1), tile(qsTr("Across Y"), 2), tile(qsTr("Both"), 3)] },
                sep,
                { text: qsTr("Layers List"), action: "layersPanel", key: "layersPanel" },
                { text: qsTr("Colors and Palette"), action: "colorsPanel", key: "colorsPanel" },
                { text: qsTr("Hide Panels"), action: "hidePanels", key: "hideUi" },
            ] },
            // The system's own place for window commands and the open sprites;
            // the in-window tabs do that on the other platforms.
            { title: qsTr("Window"), nativeOnly: true, items: [
                { text: qsTr("Minimize"), action: "minimize", seq: "Ctrl+M" },
                { text: qsTr("Zoom"), action: "zoomWindow" },
                sep,
                { text: qsTr("Next Tab"), action: "nextTab", key: "nextTab", enabled: editor.documents.length > 1 },
                { text: qsTr("Previous Tab"), action: "previousTab", key: "previousTab", enabled: editor.documents.length > 1 },
                sep,
                ...documents,
            ] },
            { title: qsTr("Help"), items: [
                { text: qsTr("Smalto Manual"), action: "help", seq: "F1" },
                { text: qsTr("Keyboard Shortcuts"), action: "helpShortcuts" },
            ] },
        ]
    }
}
