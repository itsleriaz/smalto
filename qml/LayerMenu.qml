import QtQuick
import Smalto

// The right-click menu of a layer, for the timeline's layer column and the
// layers panel. Open it with `layerIndex` set (an index into Editor::layers()).
// Renaming and the tileset dialog belong to the window: it asks by signal.
AppMenu {
    id: root

    required property Editor editor
    property int layerIndex: -1

    signal renameRequested(int index)
    signal tilesetRequested(bool convert)

    AppMenuItem { text: "Rename"; onTriggered: root.renameRequested(root.layerIndex) }
    AppMenuItem { text: "New Layer"; hint: Shortcuts.labels.newLayer; onTriggered: root.editor.addLayer() }
    AppMenuItem { text: "New Group"; onTriggered: root.editor.addGroup() }
    AppMenuItem { text: "New Particle Layer"; onTriggered: root.editor.addParticleLayer() }
    AppMenuItem {
        text: "New Tilemap Layer\u2026"
        onTriggered: root.tilesetRequested(false)
    }
    AppMenuItem {
        readonly property var layerData: root.editor.layers[root.layerIndex]
        visible: layerData !== undefined && layerData.kind === "image" && !layerData.background
        text: "Convert to Tilemap\u2026"
        onTriggered: {
            root.editor.currentLayer = root.layerIndex
            root.tilesetRequested(true)
        }
    }
    AppMenuItem {
        readonly property var layerData: root.editor.layers[root.layerIndex]
        visible: layerData !== undefined && layerData.kind === "tilemap"
        text: "Convert to Normal Layer"
        onTriggered: {
            root.editor.currentLayer = root.layerIndex
            root.editor.convertToImageLayer()
        }
    }
    AppMenuItem {
        text: "Bake Layer"
        visible: root.editor.liveLayer || (root.editor.smearState.current ?? false)
        height: visible ? implicitHeight : 0
        onTriggered: root.editor.bakeLayer()
    }
    AppMenuItem {
        text: "Bake to New Layer"
        visible: root.editor.liveLayer || (root.editor.smearState.current ?? false)
        height: visible ? implicitHeight : 0
        onTriggered: root.editor.bakeToNewLayer()
    }
    AppMenuItem { text: "Duplicate Layer"; onTriggered: root.editor.duplicateLayer() }
    AppMenuItem { text: "Group Layers"; hint: Shortcuts.labels.groupLayers; onTriggered: root.editor.groupLayers() }
    AppMenuItem { text: "Smear"; onTriggered: root.editor.smearLayers() }
    AppMenuItem {
        text: "Ungroup"
        hint: Shortcuts.labels.ungroupLayer
        enabled: (root.editor.layers[root.editor.currentLayer] ?? {}).kind === "group"
        onTriggered: root.editor.ungroupLayer()
    }
    AppMenuItem { text: "Delete Layer"; enabled: root.editor.canRemoveLayer; onTriggered: root.editor.removeLayer() }
    AppMenuSeparator {}
    // Between documents: copy the selected layers here, paste them in this or another document.
    AppMenuItem { text: "Copy Layer"; onTriggered: root.editor.copyLayers() }
    AppMenuItem { text: "Paste Layer"; enabled: root.editor.canPasteLayers; onTriggered: root.editor.pasteLayers() }
    AppMenuSeparator {}
    AppMenuItem {
        // Not `layer`: every Item has a (final) `layer` property.
        readonly property var layerData: root.editor.layers[root.layerIndex]
        visible: layerData !== undefined && layerData.kind === "image"
        height: visible ? implicitHeight : 0 // a hidden menu item keeps its place otherwise
        text: layerData && layerData.alphaLock ? "Alpha Unlock" : "Alpha Lock (paint only on its pixels)"
        hint: Shortcuts.labels.alphaLock
        onTriggered: root.editor.setLayerAlphaLock(root.layerIndex, !layerData.alphaLock)
    }
    AppMenuItem { text: "Select Pixels"; onTriggered: root.editor.selectLayerPixels(root.layerIndex) }
    AppMenuItem {
        readonly property var layerData: root.editor.layers[root.layerIndex]
        text: "Clear Layer"
        enabled: layerData !== undefined && layerData.kind !== "group" && !layerData.locked
        onTriggered: {
            root.editor.currentLayer = root.layerIndex
            root.editor.clearLayer()
        }
    }
}
