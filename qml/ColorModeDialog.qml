pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Color mode: converts the sprite to RGBA, grayscale or indexed. Going to
// indexed, pixels take the nearest color of the current palette or of a
// palette made from the sprite's own colors, optionally dithered. A floating
// window like the effects: every choice shows at once on the canvas (the
// current frame; nothing changes until Convert), Esc or Cancel puts it back.
// Drag it by its title. The palette choice is kept while the app runs.
FloatingDialog {
    id: root

    required property Editor editor

    readonly property var modes: ["rgba", "grayscale", "indexed"]
    readonly property var dithers: ["none", "bayer2", "ordered", "bayer8", "blue", "floyd", "atkinson", "sierra", "stucki", "riemersma"]
    readonly property var ditherNames: [qsTr("None"), qsTr("Ordered 2 \u00d7 2"), qsTr("Ordered 4 \u00d7 4"),
                                        qsTr("Ordered 8 \u00d7 8"), qsTr("Blue noise"), qsTr("Floyd-Steinberg"),
                                        qsTr("Atkinson"), qsTr("Sierra"), qsTr("Stucki"), qsTr("Riemersma")]
    readonly property string target: modes[modeChoice.currentIndex]
    property string originalMode: "rgba"
    property bool canChange: true
    property int colorCount: 0
    // The palette the preview would give (indexed).
    property var resultPalette: []
    property bool applied: false
    // Only an entry besides the transparent one makes the current palette usable.
    readonly property bool hasPalette: editor.palette.length > 1
    // Where the palette comes from: 0 the sprite's own colors, 1 the current
    // palette, 2 one chosen from presets / Lospec (kept while the app runs).
    readonly property var sources: ["sprite", "current", "given"]
    readonly property int source: paletteChoice.currentIndex
    readonly property string sourceName: sources[source]
    property int colorLimit: 255
    property var chosenColors: []
    property string chosenName: ""
    // Indexed to indexed remakes the palette (its own colors, fewer, or another).
    readonly property bool rebuild: originalMode === "indexed" && target === "indexed"
    readonly property bool toIndexed: target === "indexed"
    property bool keepAlpha: false // see-through shades stay as palette entries with alpha
    readonly property bool paletteReady: !toIndexed || (source === 1 ? hasPalette : source === 2 ? chosenColors.length > 0 : true)
    readonly property bool valid: canChange && paletteReady && (target !== originalMode || (rebuild && source !== 1))
    readonly property string ditherName: toIndexed ? dithers[ditherChoice.currentIndex] : "none"
    // How the dither is shaped (see core::DitherOptions): how strong, running back and forth along
    // the rows, and finding the nearest palette color in OKLab.
    property int ditherStrength: 100
    property bool serpentine: false
    property bool oklab: false
    readonly property bool diffuses: ["floyd", "atkinson", "sierra", "stucki"].indexOf(ditherName) >= 0
    function ditherOptions() {
        const o = {}
        o.strength = ditherStrength
        o.serpentine = serpentine
        o.oklab = oklab
        return o
    }

    function apply() {
        if (!valid)
            return
        applied = true
        editor.cancelColorModePreview()
        editor.setColorMode(target, sourceName, ditherName, colorLimit, chosenColors, keepAlpha && source === 0,
                            ditherOptions())
        close()
    }
    function preview() {
        if (!visible || !editor.effectActive)
            return
        // Nothing to show until a palette is chosen (or there's none to use).
        const ready = paletteReady
        const colors = editor.previewColorMode(ready ? target : originalMode, sourceName, ditherName,
                                               colorLimit, chosenColors, keepAlpha && source === 0, ditherOptions())
        if (!samePalette(colors, resultPalette))
            resultPalette = colors // swatches are rebuilt only when it changed
    }
    function samePalette(a: var, b: var): bool {
        if (a.length !== b.length)
            return false
        for (let i = 0; i < a.length; ++i)
            if (a[i] !== b[i])
                return false
        return true
    }
    // After the change has reached every binding (a handler can run first).
    function changed() { Qt.callLater(preview) }

    onAboutToShow: {
        applied = false
        originalMode = editor.colorMode
        canChange = editor.canChangeColorMode()
        colorCount = editor.spriteColorCount(255)
        modeChoice.currentIndex = modes.indexOf(originalMode)
        resultPalette = []
    }
    onOpened: {
        editor.beginColorModePreview()
        preview()
    }
    onClosed: {
        chooser.close()
        if (!applied)
            editor.cancelColorModePreview()
    }
    onTargetChanged: changed()
    onSourceChanged: {
        changed()
        if (source === 2 && chosenColors.length === 0 && toIndexed)
            Qt.callLater(chooser.open)
    }
    onColorLimitChanged: changed()
    onKeepAlphaChanged: changed()
    onChosenColorsChanged: changed()
    onDitherNameChanged: changed()
    onDitherStrengthChanged: changed()
    onSerpentineChanged: changed()
    onOklabChanged: changed()

    Connections {
        target: root.editor
        // Another edit (undo, a tab switch, an effect, ...) ended the preview.
        function onEffectChanged() {
            if (root.visible && !root.editor.effectActive && !root.applied)
                root.close()
        }
        // The preview shows the current frame.
        function onCurrentFrameChanged() { root.changed() }
    }

    dragHeight: title.height + 8

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    component Note: Text {
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
        Layout.maximumWidth: 280
    }

    contentItem: ColumnLayout {
        spacing: 14
        focus: true
        // While it has the keys, window shortcuts (tools, flips, ...) stay
        // out of the preview; Esc and Enter still reach the dialog.
        Keys.onShortcutOverride: event => event.accepted = event.key !== Qt.Key_Escape
                                 && event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter
        Keys.onReturnPressed: root.apply()
        Keys.onEnterPressed: root.apply()

        // The window is dragged by the strip behind this (see background).
        Text {
            id: title
            text: qsTr("Color Mode")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 12

            Label { text: qsTr("Mode") }
            Segmented {
                id: modeChoice
                enabled: root.canChange
                model: [qsTr("RGBA"), qsTr("Grayscale"), qsTr("Indexed")]
            }

            Label {
                text: qsTr("Palette")
                visible: root.toIndexed
            }
            Segmented {
                id: paletteChoice
                visible: root.toIndexed
                model: [qsTr("Sprite's colors"), qsTr("Current"), qsTr("Choose\u2026")]
            }

            Label {
                text: qsTr("Colors")
                visible: root.toIndexed && root.source === 0
            }
            NumberControl {
                visible: root.toIndexed && root.source === 0
                value: root.colorLimit
                from: 2
                to: 255
                buttons: true
                tip: qsTr("At most this many colors besides transparent; fewer if the sprite has fewer")
                onMoved: v => root.colorLimit = v
            }

            Label {
                text: qsTr("Transparency")
                visible: root.toIndexed && root.source === 0
            }
            OptionPill {
                visible: root.toIndexed && root.source === 0
                text: qsTr("Keep see-through shades")
                tip: qsTr("Partly transparent pixels become palette colors with alpha instead of opaque ones")
                checked: root.keepAlpha
                onToggled: root.keepAlpha = !root.keepAlpha
            }

            Label {
                text: qsTr("Palette")
                visible: root.toIndexed && root.source === 2
            }
            AppButton {
                visible: root.toIndexed && root.source === 2
                text: root.chosenName !== "" ? root.chosenName : qsTr("Pick a palette\u2026")
                onClicked: chooser.open()
            }

            Label {
                text: qsTr("Dither")
                visible: root.toIndexed
            }
            AppComboBox {
                id: ditherChoice
                visible: root.toIndexed
                implicitWidth: 170
                model: root.ditherNames
            }
            Label {
                text: qsTr("Strength")
                visible: root.toIndexed && root.ditherName !== "none"
            }
            NumberControl {
                visible: root.toIndexed && root.ditherName !== "none"
                value: root.ditherStrength
                from: 0
                to: 100
                bigStep: 10
                suffix: "%"
                tip: qsTr("How much the dither spreads: 100% is the full effect, 0% none")
                onMoved: v => root.ditherStrength = v
            }
            Item {
                implicitWidth: 1
                implicitHeight: 1
                visible: root.toIndexed
            }
            RowLayout {
                visible: root.toIndexed
                spacing: 8
                OptionPill {
                    visible: root.diffuses
                    text: qsTr("Serpentine")
                    tip: qsTr("Error diffusion runs back along every other row, which hides its streaks")
                    checked: root.serpentine
                    onToggled: root.serpentine = !root.serpentine
                }
                OptionPill {
                    text: qsTr("OKLab")
                    tip: qsTr("Find the nearest palette color the way colors look (OKLab), not by distance in RGB")
                    checked: root.oklab
                    onToggled: root.oklab = !root.oklab
                }
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
                visible: strip.visible
            }
            // The palette the sprite would get: one painted item (up to 255
            // swatches), repainted only when the palette changes.
            Canvas {
                id: strip
                visible: root.toIndexed && root.resultPalette.length > 1
                readonly property int cell: 9
                readonly property int perRow: Math.floor(280 / cell)
                Layout.preferredWidth: perRow * cell
                Layout.preferredHeight: Math.ceil(root.resultPalette.length / perRow) * cell
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const colors = root.resultPalette
                    for (let i = 1; i < colors.length; ++i) { // not the transparent entry
                        ctx.fillStyle = colors[i]
                        ctx.fillRect((i % perRow) * cell, Math.floor(i / perRow) * cell, cell - 1, cell - 1)
                    }
                }
                Connections {
                    target: root
                    function onResultPaletteChanged() { strip.requestPaint() }
                }
            }

            Item {
                implicitWidth: 1
                implicitHeight: 1
            }
            Note {
                text: root.target === root.originalMode && !root.rebuild ? qsTr("The sprite is already in this mode.")
                      : root.rebuild && root.source === 1 ? qsTr("The sprite already uses the current palette. Pick its own colors or another palette to remake it.")
                      : root.toIndexed && root.source === 1 && !root.hasPalette ? qsTr("The current palette has no colors to use.")
                      : root.toIndexed && root.source === 1
                      ? qsTr("Each pixel takes the nearest of the palette's %1 colors.").arg(root.editor.palette.length)
                      : root.toIndexed && root.source === 2
                      ? (root.chosenColors.length > 0 ? qsTr("Each pixel takes the nearest of the %1 colors chosen.").arg(root.chosenColors.length)
                                                      : qsTr("Pick a palette to convert to."))
                      : root.toIndexed && root.colorCount <= root.colorLimit
                      ? qsTr("A palette of the sprite's %1 colors, kept exactly.").arg(root.colorCount)
                      : root.toIndexed ? qsTr("More than %1 colors: reduced to %1 (median cut), plus transparent.").arg(root.colorLimit)
                      : root.target === "grayscale" ? qsTr("Colors become their brightness; alpha is kept.")
                      : qsTr("Pixels take their palette colors.")
            }
        }

        Text {
            visible: root.valid
            text: qsTr("The preview shows the current frame; Convert changes every frame.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: root.rebuild ? qsTr("Rebuild") : qsTr("Convert")
                primary: true
                enabled: root.valid
                onClicked: root.apply()
            }
        }
    }

    // "Choose": the presets and Lospec, handing back the colors.
    PalettePopup {
        id: chooser
        parent: Overlay.overlay
        editor: root.editor
        picking: true
        // What was chosen before it opened, for Cancel.
        property var keptColors: []
        property string keptName: ""
        // Beside this window, on whichever side has room; then it's draggable.
        onAboutToShow: {
            keptColors = root.chosenColors
            keptName = root.chosenName
            const right = root.x + root.width + 12
            x = right + width <= Overlay.overlay.width - 8 ? right : Math.max(8, root.x - width - 12)
            y = Math.max(8, Math.min(root.y, Overlay.overlay.height - 420))
        }
        // A click shows the palette on the canvas straight away.
        onSelected: (colors, name) => {
            root.chosenColors = colors
            root.chosenName = name
        }
        onCancelled: {
            root.chosenColors = keptColors
            root.chosenName = keptName
        }
    }
}
