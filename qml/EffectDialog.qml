pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Smalto

// Outline, Replace Color and Hue / Saturation: options shown live on the
// current cel (inside the selection, if any); Apply keeps it as one undo
// step (also on more cels, by "Apply to"), Cancel / Esc puts it back.
// A floating window: drag it by its title; it isn't modal, so the canvas
// stays in view. Colors are its own: click a swatch, then pick a color on
// the canvas or in the color panel. Open with start(kind); false when the
// cel has nothing to change.
FloatingDialog {
    id: root

    required property Editor editor
    required property CanvasView canvas

    property string kind: "outline"
    readonly property var titles: ({ outline: qsTr("Outline"), replace: qsTr("Replace Color"),
                                     adjust: qsTr("Hue / Saturation"), blur: qsTr("Blur"),
                                     convolve: qsTr("Convolution Matrix"), cleanup: qsTr("Pixel Cleanup"),
                                     posterize: qsTr("Posterize"), quantize: qsTr("Reduce Colors"), curves: qsTr("Color Curves"), shadow: qsTr("Drop Shadow") })
    readonly property var scopes: ["cel", "range", "tag", "layer", "sprite"]
    property bool applied: false

    // Outline: colorA. Replace: colorA -> colorB.
    property color colorA
    property color colorB
    // The swatch being picked for ("" none, "a", "b").
    property string picking: ""
    property bool inside: false
    property bool square: false
    property int tolerance: 0
    property int hue: 0
    property int saturation: 0
    property int lightness: 0
    property int radius: 1
    property bool gaussian: true
    property bool keepShape: true
    property int levels: 4
    // Reduce Colors: how many colors the sprite keeps, and how they are spread.
    property int colorCount: 16
    readonly property var dithers: ["none", "bayer2", "ordered", "bayer8", "blue", "floyd", "atkinson", "sierra", "stucki", "riemersma"]
    readonly property var ditherNames: [qsTr("None"), qsTr("Ordered 2 \u00d7 2"), qsTr("Ordered 4 \u00d7 4"),
                                        qsTr("Ordered 8 \u00d7 8"), qsTr("Blue noise"), qsTr("Floyd-Steinberg"),
                                        qsTr("Atkinson"), qsTr("Sierra"), qsTr("Stucki"), qsTr("Riemersma")]
    property int ditherIndex: 0
    // How the dither is shaped (see core::DitherOptions).
    property int ditherStrength: 100
    property bool serpentine: false
    property bool oklab: false
    readonly property bool diffuses: ["floyd", "atkinson", "sierra", "stucki"].indexOf(dithers[ditherIndex]) >= 0
    // Curves: the control points (flat) of each channel, and the one shown.
    readonly property var identityCurve: [0, 0, 255, 255]
    readonly property var channels: ["master", "red", "green", "blue"]
    property var curveMaster: identityCurve
    property var curveRed: identityCurve
    property var curveGreen: identityCurve
    property var curveBlue: identityCurve
    property int channel: 0
    readonly property var channelPoints: [curveMaster, curveRed, curveGreen, curveBlue][channel]
    function setChannelPoints(p: var) {
        if (channel === 0) curveMaster = p
        else if (channel === 1) curveRed = p
        else if (channel === 2) curveGreen = p
        else curveBlue = p
    }
    function resetCurves() {
        curveMaster = curveRed = curveGreen = curveBlue = identityCurve
    }
    // Drop shadow: colorA, with its opacity, and the offset.
    property int shadowX: 1
    property int shadowY: 1
    property int shadowOpacity: 50
    // Cleanup
    property bool orphans: true
    property bool pinholes: true
    property bool strays: true
    property bool doubles: true
    // Convolution: the kernel row by row (kernelSize x kernelSize).
    property int kernelSize: 3
    property var kernel: [0, -1, 0, -1, 5, -1, 0, -1, 0]
    property int divisor: 0
    property int bias: 0
    readonly property var presets: [
        { name: qsTr("Sharpen"), kernel: [0, -1, 0, -1, 5, -1, 0, -1, 0], divisor: 0, bias: 0 },
        { name: qsTr("Sharpen more"), kernel: [-1, -1, -1, -1, 9, -1, -1, -1, -1], divisor: 0, bias: 0 },
        { name: qsTr("Edge detect"), kernel: [-1, -1, -1, -1, 8, -1, -1, -1, -1], divisor: 1, bias: 0 },
        { name: qsTr("Emboss"), kernel: [-2, -1, 0, -1, 1, 1, 0, 1, 2], divisor: 0, bias: 0 },
        { name: qsTr("Soften"), kernel: [1, 2, 1, 2, 4, 2, 1, 2, 1], divisor: 0, bias: 0 },
        { name: qsTr("Identity"), kernel: [0, 0, 0, 0, 1, 0, 0, 0, 0], divisor: 0, bias: 0 },
    ]
    function usePreset(i: int) {
        const p = presets[i]
        kernelSize = 3
        kernel = p.kernel.slice()
        divisor = p.divisor
        bias = p.bias
    }
    function setKernelValue(i: int, v: int) {
        const k = kernel.slice()
        k[i] = v
        kernel = k
    }
    // 3 x 3 <-> 5 x 5: the middle stays, the new ring is zeros.
    function setKernelSize(n: int) {
        if (n === kernelSize)
            return
        const k = []
        const shift = (n - kernelSize) / 2
        for (let y = 0; y < n; ++y)
            for (let x = 0; x < n; ++x) {
                const sx = x - shift, sy = y - shift
                k.push(sx >= 0 && sy >= 0 && sx < kernelSize && sy < kernelSize ? kernel[sy * kernelSize + sx] : 0)
            }
        kernelSize = n
        kernel = k
    }
    property int scope: 0

    function start(effect: string): bool {
        if (!editor.beginEffect())
            return false
        kind = effect
        applied = false
        // The effects with swatches open ready to pick: Replace Color its
        // output (the input starts as the primary color), the others their one.
        picking = effect === "replace" ? "b" : effect === "outline" || effect === "shadow" ? "a" : ""
        colorA = editor.primaryColor
        colorB = editor.secondaryColor
        lastPrimary = editor.primaryColor
        lastSecondary = editor.secondaryColor
        editor.setEffectScope(scopes[scope])
        open()
        preview()
        return true
    }
    function options(): var {
        if (kind === "outline")
            return { kind: kind, color: colorA, inside: inside, square: square }
        if (kind === "replace")
            return { kind: kind, from: colorA, to: colorB, tolerance: tolerance }
        if (kind === "blur")
            return { kind: kind, radius: radius, gaussian: gaussian, keepShape: keepShape }
        if (kind === "curves")
            return { kind: kind, master: curveMaster, red: curveRed, green: curveGreen, blue: curveBlue }
        if (kind === "posterize")
            return { kind: kind, levels: levels }
        if (kind === "quantize")
            return { kind: kind, colors: colorCount, dither: dithers[ditherIndex], strength: ditherStrength,
                     serpentine: serpentine, oklab: oklab }
        if (kind === "shadow") {
            const c = Qt.rgba(colorA.r, colorA.g, colorA.b, shadowOpacity / 100)
            return { kind: kind, color: c, dx: shadowX, dy: shadowY }
        }
        if (kind === "cleanup")
            return { kind: kind, orphans: orphans, pinholes: pinholes, strays: strays, doubles: doubles }
        if (kind === "convolve")
            return { kind: kind, kernel: kernel, size: kernelSize, divisor: divisor, bias: bias }
        return { kind: kind, hue: hue, saturation: saturation, lightness: lightness }
    }
    function preview() {
        if (visible && editor.effectActive)
            editor.previewEffect(options())
    }
    // After the change has reached every binding (a handler can run first).
    function changed() { Qt.callLater(preview) }
    function apply() {
        applied = true
        editor.applyEffect(titles[kind], scopes[scope])
        close()
    }
    function pick(c: color) {
        if (picking === "a")
            colorA = c
        else if (picking === "b")
            colorB = c
    }

    onColorAChanged: changed()
    onColorBChanged: changed()
    onInsideChanged: changed()
    onSquareChanged: changed()
    onToleranceChanged: changed()
    onHueChanged: changed()
    onSaturationChanged: changed()
    onLightnessChanged: changed()
    onRadiusChanged: changed()
    onGaussianChanged: changed()
    onKeepShapeChanged: changed()
    onLevelsChanged: changed()
    onColorCountChanged: changed()
    onDitherIndexChanged: changed()
    onDitherStrengthChanged: changed()
    onSerpentineChanged: changed()
    onOklabChanged: changed()
    onCurveMasterChanged: changed()
    onCurveRedChanged: changed()
    onCurveGreenChanged: changed()
    onCurveBlueChanged: changed()
    onShadowXChanged: changed()
    onShadowYChanged: changed()
    onShadowOpacityChanged: changed()
    onKernelChanged: changed()
    onOrphansChanged: changed()
    onPinholesChanged: changed()
    onStraysChanged: changed()
    onDoublesChanged: changed()
    onDivisorChanged: changed()
    onBiasChanged: changed()
    onClosed: {
        picking = ""
        if (!applied)
            editor.cancelEffect()
    }

    // Another edit (undo, a palette change, ...) ended the preview.
    Connections {
        target: root.editor
        function onEffectChanged() {
            if (root.visible && !root.editor.effectActive && !root.applied)
                root.close()
        }
    }

    // Picking: a click on the canvas takes the cel's color there; a color
    // chosen in the color panel (either button) goes to the swatch too.
    property color lastPrimary
    property color lastSecondary
    Connections {
        target: root.canvas
        function onEffectPicked(pixel) { root.pick(root.editor.effectColorAt(pixel)) }
    }
    Connections {
        target: root.editor
        function onColorsChanged() {
            if (!Qt.colorEqual(root.editor.primaryColor, root.lastPrimary))
                root.pick(root.editor.primaryColor)
            else if (!Qt.colorEqual(root.editor.secondaryColor, root.lastSecondary))
                root.pick(root.editor.secondaryColor)
            root.lastPrimary = root.editor.primaryColor
            root.lastSecondary = root.editor.secondaryColor
        }
    }

    dragHeight: title.height + 8

    // An on / off pill (as in the tool options).
    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }
    // A color of the dialog's own; click to pick it (see PickSwatch).
    component Swatch: PickSwatch {
        required property string slot
        armed: root.picking === slot
        color: slot === "a" ? root.colorA : root.colorB
        onClicked: root.picking = armed ? "" : slot
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
            text: root.titles[root.kind]
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 8

            // Outline
            Label { visible: root.kind === "outline"; text: qsTr("Color") }
            Swatch { visible: root.kind === "outline"; slot: "a" }
            Label { visible: root.kind === "outline"; text: qsTr("Place") }
            Segmented {
                visible: root.kind === "outline"
                model: [qsTr("Outside"), qsTr("Inside")]
                currentIndex: root.inside ? 1 : 0
                onActivated: i => root.inside = i === 1
            }
            Label { visible: root.kind === "outline"; text: qsTr("Corners") }
            Segmented {
                visible: root.kind === "outline"
                model: [qsTr("Round"), qsTr("Square")]
                currentIndex: root.square ? 1 : 0
                onActivated: i => root.square = i === 1
            }

            // Replace color
            Label { visible: root.kind === "replace"; text: qsTr("Colors") }
            RowLayout {
                visible: root.kind === "replace"
                spacing: 8
                Swatch { slot: "a" }
                Text { text: "→"; color: Theme.textDim; font.pixelSize: Theme.fontSize }
                Swatch { slot: "b" }
                IconButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    iconName: "arrow-left-right"
                    tip: qsTr("Swap")
                    onClicked: {
                        const a = root.colorA
                        root.colorA = root.colorB
                        root.colorB = a
                    }
                }
            }
            Label { visible: root.kind === "replace"; text: qsTr("Tolerance") }
            NumberControl {
                visible: root.kind === "replace"
                value: root.tolerance
                from: 0
                to: 255
                tip: qsTr("How different a color may be and still be replaced")
                onMoved: v => root.tolerance = v
            }

            // Hue / saturation / lightness
            Label { visible: root.kind === "adjust"; text: qsTr("Hue") }
            NumberControl {
                visible: root.kind === "adjust"
                value: root.hue
                from: -180
                to: 180
                suffix: "°"
                buttons: true
                step: 5
                onMoved: v => root.hue = v
            }
            Label { visible: root.kind === "adjust"; text: qsTr("Saturation") }
            NumberControl {
                visible: root.kind === "adjust"
                value: root.saturation
                from: -100
                to: 100
                suffix: "%"
                buttons: true
                step: 5
                onMoved: v => root.saturation = v
            }
            Label { visible: root.kind === "adjust"; text: qsTr("Lightness") }
            NumberControl {
                visible: root.kind === "adjust"
                value: root.lightness
                from: -100
                to: 100
                suffix: "%"
                buttons: true
                step: 5
                onMoved: v => root.lightness = v
            }

            // Blur
            Label { visible: root.kind === "blur"; text: qsTr("Radius") }
            NumberControl {
                visible: root.kind === "blur"
                value: root.radius
                from: 1
                to: 16
                suffix: " px"
                buttons: true
                onMoved: v => root.radius = v
            }
            Label { visible: root.kind === "blur"; text: qsTr("Kind") }
            Segmented {
                visible: root.kind === "blur"
                model: [qsTr("Gaussian"), qsTr("Box")]
                currentIndex: root.gaussian ? 0 : 1
                onActivated: i => root.gaussian = i === 0
            }
            Label { visible: root.kind === "blur"; text: qsTr("Edges") }
            Segmented {
                visible: root.kind === "blur"
                model: [qsTr("Keep shape"), qsTr("Spread")]
                currentIndex: root.keepShape ? 0 : 1
                onActivated: i => root.keepShape = i === 0
            }

            // Color curves
            Label { visible: root.kind === "curves"; text: qsTr("Channel") }
            Segmented {
                visible: root.kind === "curves"
                model: [qsTr("RGB"), qsTr("R"), qsTr("G"), qsTr("B")]
                currentIndex: root.channel
                onActivated: i => root.channel = i
            }
            Label { visible: root.kind === "curves"; text: qsTr("Curve"); Layout.alignment: Qt.AlignRight | Qt.AlignTop }
            CurveEditor {
                visible: root.kind === "curves"
                points: root.channelPoints
                table: root.kind === "curves" ? root.editor.curveTable(root.channelPoints) : []
                onEdited: p => root.setChannelPoints(p)
            }
            Item { visible: root.kind === "curves"; implicitWidth: 1; implicitHeight: 1 }
            AppButton {
                visible: root.kind === "curves"
                text: qsTr("Reset curves")
                onClicked: root.resetCurves()
            }

            // Posterize
            Label { visible: root.kind === "quantize"; text: qsTr("Colors") }
            NumberControl {
                visible: root.kind === "quantize"
                value: root.colorCount
                from: 2
                to: 255
                bigStep: 8
                buttons: true
                tip: qsTr("How many colors the sprite keeps, chosen from all its frames (its own colors, merged)")
                onMoved: v => root.colorCount = v
            }
            Label { visible: root.kind === "quantize"; text: qsTr("Dither") }
            AppComboBox {
                visible: root.kind === "quantize"
                implicitWidth: 170
                model: root.ditherNames
                currentIndex: root.ditherIndex
                onActivated: index => root.ditherIndex = index
            }
            Label { visible: root.kind === "quantize" && root.ditherIndex > 0; text: qsTr("Strength") }
            NumberControl {
                visible: root.kind === "quantize" && root.ditherIndex > 0
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
                visible: root.kind === "quantize"
            }
            RowLayout {
                visible: root.kind === "quantize"
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

            Label { visible: root.kind === "posterize"; text: qsTr("Levels") }
            NumberControl {
                visible: root.kind === "posterize"
                value: root.levels
                from: 2
                to: 16
                buttons: true
                tip: qsTr("Values each color channel can take")
                onMoved: v => root.levels = v
            }

            // Drop shadow
            Label { visible: root.kind === "shadow"; text: qsTr("Color") }
            Swatch { visible: root.kind === "shadow"; slot: "a" }
            Label { visible: root.kind === "shadow"; text: qsTr("Opacity") }
            NumberControl {
                visible: root.kind === "shadow"
                value: root.shadowOpacity
                from: 0
                to: 100
                suffix: "%"
                buttons: true
                step: 5
                onMoved: v => root.shadowOpacity = v
            }
            Label { visible: root.kind === "shadow"; text: qsTr("Offset X") }
            NumberControl {
                visible: root.kind === "shadow"
                value: root.shadowX
                from: -64
                to: 64
                suffix: " px"
                buttons: true
                onMoved: v => root.shadowX = v
            }
            Label { visible: root.kind === "shadow"; text: qsTr("Offset Y") }
            NumberControl {
                visible: root.kind === "shadow"
                value: root.shadowY
                from: -64
                to: 64
                suffix: " px"
                buttons: true
                onMoved: v => root.shadowY = v
            }

            // Pixel cleanup
            Label { visible: root.kind === "cleanup"; text: qsTr("Fix"); Layout.alignment: Qt.AlignRight | Qt.AlignTop }
            GridLayout {
                visible: root.kind === "cleanup"
                columns: 2
                columnSpacing: 4
                rowSpacing: 4
                OptionPill {
                    text: qsTr("Orphan pixels")
                    checked: root.orphans
                    onToggled: root.orphans = !root.orphans
                }
                OptionPill {
                    text: qsTr("Pinholes")
                    checked: root.pinholes
                    onToggled: root.pinholes = !root.pinholes
                }
                OptionPill {
                    text: qsTr("Stray colors")
                    checked: root.strays
                    onToggled: root.strays = !root.strays
                }
                OptionPill {
                    text: qsTr("Line doubles")
                    checked: root.doubles
                    onToggled: root.doubles = !root.doubles
                }
            }

            // Convolution matrix
            Label { visible: root.kind === "convolve"; text: qsTr("Preset") }
            AppComboBox {
                visible: root.kind === "convolve"
                implicitWidth: 170
                model: root.presets.map(p => p.name)
                currentIndex: -1
                displayText: qsTr("Choose\u2026")
                onActivated: index => root.usePreset(index)
            }
            Label { visible: root.kind === "convolve"; text: qsTr("Size") }
            Segmented {
                visible: root.kind === "convolve"
                model: ["3 \u00d7 3", "5 \u00d7 5"]
                currentIndex: root.kernelSize === 5 ? 1 : 0
                onActivated: i => root.setKernelSize(i === 1 ? 5 : 3)
            }
            Label { visible: root.kind === "convolve"; text: qsTr("Kernel"); Layout.alignment: Qt.AlignRight | Qt.AlignTop }
            GridLayout {
                visible: root.kind === "convolve"
                columns: root.kernelSize
                columnSpacing: 2
                rowSpacing: 2
                Repeater {
                    model: root.kernelSize * root.kernelSize
                    delegate: NumberControl {
                        required property int index
                        value: root.kernel[index] ?? 0
                        from: -99
                        to: 99
                        fieldWidth: 34
                        onMoved: v => root.setKernelValue(index, v)
                    }
                }
            }
            Label { visible: root.kind === "convolve"; text: qsTr("Divide by") }
            NumberControl {
                visible: root.kind === "convolve"
                value: root.divisor
                from: 0
                to: 999
                zeroText: qsTr("Auto")
                tip: qsTr("Auto: the sum of the kernel (1 if that's 0)")
                onMoved: v => root.divisor = v
            }
            Label { visible: root.kind === "convolve"; text: qsTr("Offset") }
            NumberControl {
                visible: root.kind === "convolve"
                value: root.bias
                from: -255
                to: 255
                tip: qsTr("Added to every color channel after dividing")
                onMoved: v => root.bias = v
            }

            Label { text: qsTr("Apply to") }
            AppComboBox {
                implicitWidth: 170
                model: [qsTr("This cel"), qsTr("Selected cels"), qsTr("This tag (layer)"),
                        qsTr("This layer, all frames"), qsTr("Whole sprite")]
                currentIndex: root.scope
                onActivated: index => {
                    root.scope = index
                    root.editor.setEffectScope(root.scopes[index])
                }
            }
        }

        // A full picker for the swatch being picked (the canvas and the color panel work too).
        ColorPicker {
            visible: root.picking !== "" && (root.kind === "outline" || root.kind === "replace" || root.kind === "shadow")
            Layout.fillWidth: true
            squareHeight: 110
            current: root.picking === "b" ? root.colorB : root.colorA
            onEdited: c => root.pick(c)
        }
        Text {
            visible: root.kind === "outline" || root.kind === "replace" || root.kind === "shadow"
            text: root.picking !== "" ? qsTr("Pick here, click the canvas, or pick in the color panel.")
                                      : qsTr("Click a swatch to pick its color.")
            color: root.picking !== "" ? Theme.accent : Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }
        Text {
            text: (root.editor.hasSelection ? qsTr("Only inside the selection. ") : "")
                  + (root.scope === 0 ? "" : qsTr("The preview shows the current frame."))
            visible: text !== ""
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }

        RowLayout {
            spacing: 8
            AppButton {
                visible: root.kind === "adjust"
                text: qsTr("Reset")
                enabled: root.hue !== 0 || root.saturation !== 0 || root.lightness !== 0
                onClicked: {
                    root.hue = 0
                    root.saturation = 0
                    root.lightness = 0
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Apply")
                primary: true
                onClicked: root.apply()
            }
        }
    }
}
