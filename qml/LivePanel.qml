pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// The settings of the current live layer's particle emitter. Shown only while
// a live layer is current. Aiming things (the emitter, later the cone) live on
// the canvas; this holds the rest. A value that differs from its built-in
// default has an accent dot: click it (or double-click the label) to put it
// back; a section header resets the whole section. Every change goes through
// Editor::setParticleField / setParticleColors / resetParticleFields, so it is
// one undo step (a drag of a number is one step).
Panel {
    id: root

    required property Editor editor

    // What the window arranges (PanelColumns), see ColorPanel.
    readonly property real wantHeight: 520
    readonly property real minHeight: 160
    readonly property int shrinkRank: 1
    movable: true
    padding: 10

    readonly property var s: editor.particleSettings
    // What the reset dots compare with: the preset it started from, else the built-in values.
    readonly property var d: editor.particleReference
    readonly property bool stream: s.emission === 1
    readonly property bool sub: s.context === 1 // editing the sub-particles, not the particles
    readonly property var colors: s.colors ?? []
    // What the emission needs of the sprite: the frames it emits on, and how long the last particles live.
    readonly property bool looping: s.loop === 1
    readonly property bool untilEnd: stream && s.untilEnd === 1
    readonly property bool perFrame: stream && s.perFrame === 1
    readonly property int preRoll: Math.max(0, -(s.startFrame ?? 0))
    // The frames it emits over: to the end of the sprite, or to the end frame.
    readonly property int emitSpan: untilEnd ? Math.max(1, editor.frameCount - (s.startFrame ?? 0))
                                             : Math.max(1, (s.endFrame ?? 0) - (s.startFrame ?? 0) + 1)
    readonly property int lastEmit: stream ? (s.endFrame ?? 0) : (s.startFrame ?? 0)
    readonly property int framesNeeded: looping ? emitSpan
        : untilEnd ? 1
        : lastEmit + 1 + Math.ceil((s.lifetime ?? 1) * (1 + (s.lifetimeRandom ?? 0) / 100)) - 1
    readonly property bool tooShort: framesNeeded > editor.frameCount
    readonly property string emitNote: {
        const first = Math.max(0, s.startFrame ?? 0) + 1
        if (looping)
            return "Repeats every " + emitSpan + " frames."
        const before = preRoll > 0 ? "Began " + preRoll + " frames before the first. " : ""
        if (untilEnd)
            return before + "Emits from frame " + first + " to the last."
        const when = stream ? "Emits on frames " + first + "\u2013" + (lastEmit + 1) : "Emits on frame " + first
        return before + when + "; the last particles are gone by frame " + framesNeeded + "."
    }

    // Sections the user folded, as JSON {id: true} (remembered by the window).
    property string collapsedJson: "{}"
    signal collapsedEdited(string json)
    readonly property var folded: {
        try { return JSON.parse(collapsedJson) } catch (e) { return {} }
    }
    function toggleSection(id: string) {
        stop = -1
        const next = Object.assign({}, folded)
        if (next[id])
            delete next[id]
        else
            next[id] = true
        collapsedEdited(JSON.stringify(next))
    }

    // Where the labels of the lines start (past the reset dot): everything else in a section lines up with them.
    readonly property int inset: 18
    readonly property bool ownColors: s.shape === 4 && (s.brushWidth ?? 0) > 0 && s.brushColors === 1
    property bool brushHint: false // shown after choosing the brush shape with no brush to take
    readonly property var sectionIds: ["emission", "source", "motion", "fields", "life", "look", "subs"]
    readonly property bool allFolded: sectionIds.every(id => folded[id] === true)
    // Folds every section, or opens them all when they are folded already.
    function foldAll() {
        stop = -1
        const next = {}
        if (!allFolded)
            for (const id of sectionIds)
                next[id] = true
        collapsedEdited(JSON.stringify(next))
    }

    // How much of the particle controls shows on the canvas (0 all, 1 faded, 2 none); the window keeps it.
    property int overlayMode: 0
    signal overlayCycled()

    // The field being edited (-1: none); the canvas shows the same one (the window keeps it).
    property int field: -1
    signal fieldPicked(int index)
    readonly property var fieldList: s.fields ?? []
    readonly property var fieldNow: field >= 0 && field < fieldList.length ? fieldList[field] : null
    onFieldListChanged: if (field >= fieldList.length) fieldPicked(fieldList.length - 1)
    function putField(key: string, value: real, continuous: bool) {
        stop = -1
        editor.setParticleFieldValue(field, key, value, continuous)
    }

    // The color stop being edited (-1: none). While one is chosen, the primary color is its color.
    property int stop: -1

    // Any other edit in the panel ends the picking of a color.
    function put(key: string, value: real, continuous: bool) {
        stop = -1
        root.editor.setParticleField(key, value, continuous)
    }
    function differs(key: string): bool {
        const a = s[key], b = d[key]
        if (a === undefined)
            return false
        if (key === "colors")
            return a.length !== b.length || a.some((c, i) => !Qt.colorEqual(c, b[i]))
        if (key === "fields")
            return JSON.stringify(a) !== JSON.stringify(b)
        return Math.abs(a - b) > 1e-9
    }
    function changed(keys: var): bool { return keys.some(k => differs(k)) }
    function reset(keys: var) { stop = -1; root.editor.resetParticleFields(keys) }
    readonly property var allKeys: Object.keys(d).filter(k => k !== "seed")

    onColorsChanged: if (stop >= colors.length) stop = -1

    // A color is set once, then the stop lets go: a little after the last change (a drag in the color
    // picker is a stream of them), so there is no need to click it again to leave.
    Timer {
        id: settle
        interval: 1200
        onTriggered: root.stop = -1
    }
    onStopChanged: settle.stop() // arming another one must not inherit the wait

    function disarm() { stop = -1 }
    // Arms a color: it becomes the primary color, and from now on what is picked sets it.
    function armStop(index: int) {
        stop = index
        editor.primaryColor = colors[index]
    }
    function addStop() {
        const base = stop >= 0 ? colors[stop] : editor.primaryColor
        const at = stop >= 0 ? stop + 1 : colors.length // next to the armed one
        const next = colors.slice()
        next.splice(at, 0, base)
        editor.setParticleColors(next)
        stop = at
        editor.primaryColor = base
    }
    function removeStop(index: int) {
        if (colors.length < 2 || index < 0 || index >= colors.length)
            return
        const next = colors.slice()
        next.splice(index, 1)
        stop = stop === index ? -1 : stop > index ? stop - 1 : stop
        editor.setParticleColors(next)
    }
    Connections {
        target: root.editor
        // Picking a color anywhere (the Colors panel, the palette, the eyedropper) sets the chosen stop.
        function onColorsChanged() {
            const list = root.colors
            if (root.stop < 0 || root.stop >= list.length)
                return
            if (!Qt.colorEqual(root.editor.primaryColor, list[root.stop])) {
                const next = list.slice()
                next[root.stop] = root.editor.primaryColor
                root.editor.setParticleColors(next)
                settle.restart() // armed until the picking pauses
            }
        }
        function onCurrentLayerChanged() { root.stop = -1; root.fieldPicked(-1) }
    }

    // The same easings as the engine (core/Particles.cpp).
    // A custom easing's curve [x1, y1, x2, y2] at t, as the engine has it (core/Easing): the s where the
    // bezier's x is t, then its y.
    function curveAt(c: var, t: real): real {
        const bez = (a, b, s) => 3 * (1 - s) * (1 - s) * s * a + 3 * (1 - s) * s * s * b + s * s * s
        let lo = 0, hi = 1, s = t
        for (let i = 0; i < 40; ++i) {
            s = (lo + hi) / 2
            if (bez(c[0], c[2], s) < t) lo = s
            else hi = s
        }
        return bez(c[1], c[3], s)
    }
    function eased(ease: int, t: real, curve: var): real {
        if (ease === 4)
            return curveAt(curve ?? [1 / 3, 1 / 3, 2 / 3, 2 / 3], t)
        switch (ease) {
        case 1: return t * t
        case 2: return 1 - (1 - t) * (1 - t)
        case 3: return t * t * (3 - 2 * t)
        default: return t
        }
    }

    // A particle at eight moments of its life: squares as big as it is, or as solid as it is, so what
    // the numbers and the easing do can be seen at a glance.
    component LifeStrip: Row {
        id: strip
        required property string kind // "size" or "opacity"
        readonly property int steps: 8
        readonly property real big: Math.max(1, root.s.sizeStart ?? 1, root.s.sizeEnd ?? 1)
        Layout.alignment: Qt.AlignRight
        spacing: 2
        Repeater {
            model: strip.steps
            delegate: Item {
                id: moment
                required property int index
                readonly property real t: index / (strip.steps - 1)
                width: 20
                height: 20
                Rectangle {
                    anchors.centerIn: parent
                    readonly property real amount: strip.kind === "size"
                        ? ((root.s.sizeStart ?? 1) + ((root.s.sizeEnd ?? 1) - (root.s.sizeStart ?? 1))
                           * root.eased(root.s.sizeEase ?? 0, moment.t, root.s.sizeCurve)) / strip.big
                        : ((root.s.opacityStart ?? 100) + ((root.s.opacityEnd ?? 100) - (root.s.opacityStart ?? 100))
                           * root.eased(root.s.opacityEase ?? 0, moment.t, root.s.opacityCurve)) / 100
                    width: strip.kind === "size" ? (amount > 0.02 ? Math.max(2, Math.round(amount * 18)) : 0) : 14
                    height: width
                    radius: strip.kind === "size" ? 1 : 3
                    color: Theme.accent
                    opacity: strip.kind === "opacity" ? Math.max(0, Math.min(1, amount)) : 1
                    border.color: strip.kind === "opacity" ? Theme.separator : "transparent"
                    border.width: 1
                }
            }
        }
    }

    // A name for the settings being saved as a preset.
    Popup {
        id: presetNameBox
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        padding: 22
        closePolicy: Popup.CloseOnEscape
        background: Panel { solid: true }
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }
        onOpened: { presetName.forceActiveFocus(); presetName.selectAll() }
        function start(name: string) {
            presetName.text = name
            open()
        }
        function save() {
            if (root.editor.saveParticlePreset(presetName.text))
                close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Save as a preset"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                Layout.maximumWidth: 320
                wrapMode: Text.Wrap
                text: "These settings, as a preset for sprites of any size. The same name replaces the one saved before."
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            AppTextField {
                id: presetName
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                maximumLength: 40
                onAccepted: presetNameBox.save()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; onClicked: presetNameBox.close() }
                AppButton { text: "Save"; primary: true; enabled: presetName.text.trim() !== ""; onClicked: presetNameBox.save() }
            }
        }
    }

    // The dot that shows a value is not at its default; clicking it resets.
    component ResetDot: Item {
        id: dot
        property bool shown: false
        signal clicked()
        implicitWidth: 14
        implicitHeight: 20
        Rectangle {
            anchors.centerIn: parent
            width: 7
            height: 7
            radius: 3.5
            color: Theme.accent
            visible: dot.shown
        }
        MouseArea {
            anchors.fill: parent
            enabled: dot.shown
            cursorShape: Qt.PointingHandCursor
            onClicked: dot.clicked()
        }
    }

    // A labelled line: the reset dot, the label, then the controls (right aligned).
    component Line: RowLayout {
        id: line
        property string label
        property var keys: []
        property string tip: ""
        default property alias content: controls.data
        Layout.fillWidth: true
        spacing: 4
        ResetDot {
            shown: root.changed(line.keys)
            onClicked: root.reset(line.keys)
        }
        Text {
            Layout.fillWidth: true
            text: line.label
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
            MouseArea {
                anchors.fill: parent
                onDoubleClicked: root.reset(line.keys)
            }
        }
        RowLayout {
            id: controls
            spacing: 4
        }
    }

    // One number. `divisor` > 1 shows tenths (or hundredths) of the stored value.
    component Field: Line {
        id: field
        required property string key
        property int from: 0
        property int to: 100
        property int divisor: 1
        property int unit: 1 // stored value times this is what the number shows (100: a fraction as a percent)
        property string suffix: ""
        keys: [key]
        NumberControl {
            value: Math.round((root.s[field.key] ?? 0) * field.divisor * field.unit)
            from: field.from
            to: field.to
            divisor: field.divisor
            suffix: field.suffix
            tip: field.tip
            onMoved: v => root.put(field.key, v / (field.divisor * field.unit), true)
        }
    }

    // Two numbers that are the ends of one range.
    component Range: Line {
        id: range
        required property string lowKey
        required property string highKey
        property int from: 0
        property int to: 100
        property int divisor: 1
        property string suffix: ""
        property bool single: false // only the low end (a burst has no end frame)
        property int offset: 0 // added to what is shown (frames count from 1 on the timeline)
        property bool clampLow: false // the start shows as the first frame when it is before it (a pre-roll)
        keys: [lowKey, highKey]
        NumberControl {
            value: Math.round(Math.max(range.clampLow ? 0 : -1e9, root.s[range.lowKey] ?? 0) * range.divisor) + range.offset
            from: range.from
            to: range.to
            divisor: range.divisor
            suffix: range.suffix
            fieldWidth: 44
            tip: range.tip
            onMoved: v => root.put(range.lowKey, (v - range.offset) / range.divisor, true)
        }
        Text {
            visible: !range.single
            text: "–"
            color: Theme.textFaint
        }
        NumberControl {
            visible: !range.single
            value: Math.round((root.s[range.highKey] ?? 0) * range.divisor) + range.offset
            from: range.from
            to: range.to
            divisor: range.divisor
            suffix: range.suffix
            fieldWidth: 44
            tip: range.tip
            onMoved: v => root.put(range.highKey, (v - range.offset) / range.divisor, true)
        }
    }

    // A folding group with its own reset.
    component Section: ColumnLayout {
        id: section
        required property string sectionId
        property string title
        property var keys: []
        default property alias content: inner.data
        readonly property bool open: !root.folded[sectionId]
        Layout.fillWidth: true
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 4
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    Image {
                        source: section.open ? "qrc:/icons/chevron-down.svg" : "qrc:/icons/chevron-right.svg"
                        width: 14
                        height: 14
                        sourceSize: Qt.size(28, 28)
                        opacity: 0.6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: section.title
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleSection(section.sectionId)
                }
            }
            IconButton {
                visible: root.changed(section.keys)
                implicitWidth: 22
                implicitHeight: 22
                iconName: "rotate-ccw"
                tip: "Reset this section to its built-in values"
                onClicked: root.reset(section.keys)
            }
        }
        ColumnLayout {
            id: inner
            visible: section.open
            Layout.fillWidth: true
            spacing: 6
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.separator
        }
    }

    // Gravity as a point on a small pad: the middle is none, the edges +-0.5 px per frame squared.
    component GravityPad: Rectangle {
        id: pad
        readonly property real reach: 0.5
        implicitWidth: 64
        implicitHeight: 64
        radius: Theme.radiusSmall
        color: Theme.controlTrack
        border.color: root.changed(["gravityX", "gravityY"]) ? Theme.accent : Theme.controlBorder
        border.width: 1

        function setFrom(x: real, y: real) {
            const gx = Math.round((x / width * 2 - 1) * reach * 100) / 100
            const gy = Math.round((y / height * 2 - 1) * reach * 100) / 100
            root.put("gravityX", Math.max(-reach, Math.min(reach, gx)), true)
            root.put("gravityY", Math.max(-reach, Math.min(reach, gy)), true)
        }
        Rectangle { x: parent.width / 2; width: 1; height: parent.height; color: Theme.separator }
        Rectangle { y: parent.height / 2; height: 1; width: parent.width; color: Theme.separator }
        Rectangle { // from the middle to the dot
            readonly property real dx: pad.width / 2 * (root.s.gravityX ?? 0) / pad.reach
            readonly property real dy: pad.height / 2 * (root.s.gravityY ?? 0) / pad.reach
            x: pad.width / 2
            y: pad.height / 2 - 0.75
            width: Math.min(Math.hypot(dx, dy), pad.width / 2 * 1.4)
            height: 1.5
            transformOrigin: Item.Left
            rotation: Math.atan2(dy, dx) * 180 / Math.PI
            color: Theme.accent
            opacity: 0.7
            visible: width > 1
        }
        Rectangle {
            readonly property real gx: Math.max(-pad.reach, Math.min(pad.reach, root.s.gravityX ?? 0))
            readonly property real gy: Math.max(-pad.reach, Math.min(pad.reach, root.s.gravityY ?? 0))
            x: pad.width / 2 * (1 + gx / pad.reach) - 4
            y: pad.height / 2 * (1 + gy / pad.reach) - 4
            width: 8
            height: 8
            radius: 4
            color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: m => pad.setFrom(m.x, m.y)
            onPositionChanged: m => { if (pressed) pad.setFrom(m.x, m.y) }
            onDoubleClicked: root.reset(["gravityX", "gravityY"])
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            // The preset this started from (or Custom), and a menu of the others to start over from.
            Text {
                visible: root.sub
                text: "Sub-particles"
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
            OptionPill {
                id: presetPill
                visible: !root.sub
                indicator: false
                text: (root.s.presetName ? root.s.presetName + (root.s.presetEdited === 1 ? " \u00b7 edited" : "") : "Custom") + "  \u25be"
                tip: "Start over from a preset: it replaces the settings here, not where the emitter is"
                onToggled: if (!presetMenu.visible && Date.now() - presetMenu.closedAt > 250)
                               presetMenu.popup(presetPill, 0, presetPill.height + 4)

                ModelMenu {
                    id: presetMenu
                    property double closedAt: 0
                    onClosed: closedAt = Date.now()
                    items: {
                        const saved = root.editor.userPresets // the menu is made again when these change
                        const list = [{ text: "Plain emitter", action: "preset", arg: "", checked: !root.s.preset }, { separator: true }]
                        const all = root.editor.particlePresetList()
                        for (const p of all.filter(p => !p.user))
                            list.push({ text: p.name, action: "preset", arg: p.id, checked: root.s.preset === p.id })
                        if (saved.length > 0)
                            list.push({ separator: true })
                        for (const p of all.filter(p => p.user))
                            list.push({ text: p.name, action: "preset", arg: p.id, checked: root.s.preset === p.id })
                        list.push({ separator: true }, { text: "Save as preset\u2026", action: "savePreset" })
                        if (saved.length > 0)
                            list.push({ text: "Delete a preset", submenu: saved.map(u => ({ text: u.name, action: "deletePreset", arg: u.id, danger: true })) })
                        return list
                    }
                    onChosen: (action, arg) => {
                        root.stop = -1
                        if (action === "savePreset") {
                            presetNameBox.start(root.s.preset && root.s.preset.startsWith("user:") ? (root.s.presetName ?? "") : "")
                        } else if (action === "deletePreset") {
                            root.editor.deleteUserPreset(arg)
                        } else {
                            root.editor.applyParticlePreset(arg)
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                implicitWidth: 26
                implicitHeight: 26
                iconName: "shuffle"
                tip: "A new random spread of the same effect (seed " + (root.s.seed ?? 0) + "; Emission has the number)"
                onClicked: { root.stop = -1; root.editor.reseedParticles() }
            }
            IconButton {
                implicitWidth: 26
                implicitHeight: 26
                iconName: root.allFolded ? "unfold-vertical" : "fold-vertical"
                tip: root.allFolded ? "Open every section" : "Fold every section"
                onClicked: root.foldAll()
            }
            IconButton {
                implicitWidth: 26
                implicitHeight: 26
                iconName: root.overlayMode === 2 ? "eye-off" : "eye"
                active: root.overlayMode === 1
                tip: (root.overlayMode === 0 ? "Particle controls on the canvas: shown"
                      : root.overlayMode === 1 ? "Particle controls on the canvas: faded (full when you point at them)"
                      : "Particle controls on the canvas: hidden") + " \u00b7 click to change"
                       + (Shortcuts.labels.liveOverlay ? " (" + Shortcuts.labels.liveOverlay + ")" : "")
                onClicked: root.overlayCycled()
            }
            IconButton {
                visible: root.changed(root.allKeys)
                implicitWidth: 26
                implicitHeight: 26
                iconName: "rotate-ccw"
                tip: "Reset everything to the built-in values"
                onClicked: root.reset(root.allKeys)
            }
        }

        Flickable {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            AppScrollBar.vertical: AppScrollBar {}

            // A click on the empty space between the controls ends the picking of a color.
            MouseArea {
                width: view.contentWidth
                height: view.contentHeight
                onClicked: root.stop = -1
            }

            ColumnLayout {
                id: body
                width: view.width - 12 // room for the scroll bar
                spacing: 4

                // Which particles the sections below are about, once there are sub-particles.
                Segmented {
                    visible: root.s.hasSub === 1 && root.s.subTrigger !== 0
                    Layout.topMargin: 4
                    model: ["Particles", "Sub-particles"]
                    currentIndex: root.sub ? 1 : 0
                    onActivated: index => { root.stop = -1; root.editor.particleContext = index }
                }

                Section {
                    sectionId: "emission"
                    title: "Emission"
                    keys: ["emission", "count", "perFrame", "rate", "startFrame", "endFrame", "untilEnd", "loop"]
                    Segmented {
                        Layout.leftMargin: root.inset
                        model: ["Burst", "Stream"]
                        currentIndex: root.stream ? 1 : 0
                        onActivated: index => root.put("emission", index, false)
                    }
                    Line {
                        visible: root.stream && !root.sub
                        label: "Count"
                        keys: ["perFrame"]
                        tip: "In all: that many, spread over the frames. A frame: that many every frame, so a longer run is not a thinner one"
                        Segmented {
                            model: ["In all", "A frame"]
                            currentIndex: root.perFrame ? 1 : 0
                            onActivated: index => root.put("perFrame", index, false)
                        }
                    }
                    Line {
                        label: "Particles"
                        keys: root.perFrame ? ["rate"] : ["count"]
                        tip: root.perFrame ? "Particles every frame" : root.stream ? "Particles in all, spread over the frames" : "Particles in the burst"
                        NumberControl {
                            value: root.perFrame ? Math.round((root.s.rate ?? 0) * 10) : (root.s.count ?? 0)
                            from: 1
                            to: 5000
                            divisor: root.perFrame ? 10 : 1
                            suffix: root.perFrame ? " / f" : ""
                            onMoved: v => root.perFrame ? root.put("rate", v / 10, true) : root.put("count", v, true)
                        }
                    }
                    Text {
                        visible: root.stream
                        Layout.fillWidth: true
                        text: root.perFrame ? "About " + Math.round((root.s.rate ?? 0) * root.emitSpan) + " in all"
                                            : "About " + (Math.round((root.s.count ?? 0) / root.emitSpan * 10) / 10) + " a frame"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        horizontalAlignment: Text.AlignRight
                    }
                    Line {
                        visible: root.sub && root.stream
                        label: "Born over"
                        keys: ["endFrame"]
                        tip: "How many frames a round of them takes to be born, from when its particle dies or lands"
                        NumberControl {
                            value: (root.s.endFrame ?? 0) - (root.s.startFrame ?? 0) + 1
                            from: 1
                            to: 1000
                            suffix: " f"
                            fieldWidth: 56
                            onMoved: v => root.put("endFrame", (root.s.startFrame ?? 0) + v - 1, true)
                        }
                    }
                    // The frames it emits on: from, and to a frame or (the arrow) to the sprite's last.
                    Line {
                        visible: !root.sub
                        label: root.stream ? "Frames" : "At frame"
                        keys: ["startFrame", "endFrame", "untilEnd"]
                        tip: "The frames the effect emits on (also a bar on the timeline)"
                        NumberControl {
                            value: Math.max(0, root.s.startFrame ?? 0) + 1 // a pre-roll shows as the first frame
                            from: 1
                            to: 10000
                            fieldWidth: 44
                            tip: "The first frame it emits on"
                            onMoved: v => root.put("startFrame", v - 1, true)
                        }
                        Text {
                            visible: root.stream
                            text: "–"
                            color: Theme.textFaint
                        }
                        NumberControl {
                            visible: root.stream && !root.untilEnd
                            value: (root.s.endFrame ?? 0) + 1
                            from: 1
                            to: 10000
                            fieldWidth: 44
                            tip: "The last frame it emits on"
                            onMoved: v => root.put("endFrame", v - 1, true)
                        }
                        Text {
                            visible: root.untilEnd
                            Layout.preferredWidth: 44
                            text: "End"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSize
                            horizontalAlignment: Text.AlignHCenter
                        }
                        IconButton {
                            visible: root.stream && !root.looping
                            implicitWidth: 26
                            implicitHeight: 26
                            iconName: "arrow-right-to-line"
                            active: root.untilEnd
                            tip: root.untilEnd ? "Emits until the last frame of the sprite, however long it is: click for an end frame"
                                               : "Emit until the last frame of the sprite, however long it is"
                            onClicked: root.put("untilEnd", root.untilEnd ? 0 : 1, false)
                        }
                    }
                    OptionPill {
                        visible: !root.sub
                        Layout.leftMargin: root.inset
                        text: "Loop"
                        tip: "The effect repeats every period, so frame 1 already has particles and the animation tiles"
                        checked: root.looping
                        onToggled: root.put("loop", root.looping ? 0 : 1, false)
                    }
                    Line {
                        visible: !root.sub && !root.looping
                        label: "Pre-roll"
                        keys: ["startFrame"]
                        tip: "Frames the effect has already run when the animation begins, so it is under way at the first frame"
                        NumberControl {
                            value: root.preRoll
                            from: 0
                            to: 2000
                            suffix: " f"
                            fieldWidth: 56
                            onMoved: v => root.put("startFrame", v > 0 ? -v : 0, true)
                        }
                    }
                    Line {
                        label: "Seed"
                        tip: "The scatter: a different number is a different spread of the same effect (the shuffle at the top picks one)"
                        NumberControl {
                            value: root.s.seed ?? 0
                            from: 0
                            to: 2147483647
                            fieldWidth: 80
                            tip: "The scatter"
                            onMoved: v => root.put("seed", v, true)
                        }
                    }
                    Text {
                        visible: !root.sub
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        wrapMode: Text.WordWrap
                        text: root.emitNote + (root.tooShort ? " The sprite has only " + root.editor.frameCount + "." : "")
                        color: root.tooShort ? Theme.error : Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    OptionPill {
                        visible: root.tooShort && !root.sub
                        Layout.leftMargin: root.inset
                        indicator: false
                        text: "Add " + (root.framesNeeded - root.editor.frameCount) + " frames"
                        tip: "Add empty frames at the end so the whole effect plays"
                        onToggled: root.editor.addFramesUpTo(root.framesNeeded)
                    }
                }

                Section {
                    sectionId: "source"
                    title: "Source"
                    keys: ["source", "sourceWidth", "sourceHeight", "sourceAngle", "sourceFill"]
                    Segmented {
                        Layout.leftMargin: root.inset
                        model: ["Point", "Line", "Rect", "Oval"]
                        currentIndex: root.s.source ?? 0
                        onActivated: index => root.put("source", index, false)
                    }
                    Field {
                        visible: root.s.source === 1
                        label: "Length"
                        key: "sourceWidth"
                        to: 4096
                        suffix: " px"
                        tip: "Particles are born along a line through the emitter (also the handle on the canvas)"
                    }
                    Line {
                        visible: root.s.source >= 2
                        label: "Size"
                        keys: ["sourceWidth", "sourceHeight"]
                        tip: "Width and height of the area around the emitter (also the handles on the canvas)"
                        NumberControl {
                            value: root.s.sourceWidth ?? 0
                            from: 1
                            to: 4096
                            fieldWidth: 44
                            onMoved: v => root.put("sourceWidth", v, true)
                        }
                        Text { text: "×"; color: Theme.textFaint }
                        NumberControl {
                            value: root.s.sourceHeight ?? 0
                            from: 1
                            to: 4096
                            fieldWidth: 44
                            onMoved: v => root.put("sourceHeight", v, true)
                        }
                    }
                    Field {
                        visible: root.s.source >= 1
                        label: "Angle"
                        key: "sourceAngle"
                        from: -360
                        to: 360
                        suffix: "°"
                        tip: "Turns the source about the emitter (a line's angle is also its handle on the canvas)"
                    }
                    OptionPill {
                        visible: root.s.source >= 2
                        Layout.leftMargin: root.inset
                        text: "Fill the area"
                        tip: "Born anywhere inside; off, only on the edge"
                        checked: root.s.sourceFill === 1
                        onToggled: root.put("sourceFill", root.s.sourceFill === 1 ? 0 : 1, false)
                    }
                }

                Section {
                    sectionId: "motion"
                    title: "Motion"
                    keys: ["direction", "spread", "speed", "speedRandom", "drag", "gravityX", "gravityY"]
                    Field { label: "Direction"; key: "direction"; from: -360; to: 360; suffix: "°"; tip: "0 is right, 90 down, -90 up (also the handle on the canvas)" }
                    Field { label: "Spread"; key: "spread"; to: 360; suffix: "°"; tip: "The cone's total width (also its edges on the canvas)" }
                    Line {
                        label: "Speed"
                        keys: ["speed", "speedRandom"]
                        NumberControl {
                            value: Math.round((root.s.speed ?? 0) * 10)
                            to: 2000
                            divisor: 10
                            fieldWidth: 44
                            tip: "Pixels per frame at birth"
                            onMoved: v => root.put("speed", v / 10, true)
                        }
                        Text { text: "±"; color: Theme.textFaint }
                        NumberControl {
                            value: root.s.speedRandom ?? 0
                            to: 100
                            suffix: "%"
                            fieldWidth: 44
                            tip: "How much each particle's speed varies"
                            onMoved: v => root.put("speedRandom", v, true)
                        }
                    }
                    Field { label: "Slow down"; key: "drag"; to: 100; unit: 100; suffix: "%"; tip: "The part of the speed lost every frame (drag)" }
                    Line {
                        label: "Gravity"
                        keys: ["gravityX", "gravityY"]
                        tip: "Pixels per frame, every frame: drag the dot, or double-click the pad to clear it"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        spacing: 10
                        GravityPad {}
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "x"; color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
                                Item { Layout.fillWidth: true }
                                NumberControl {
                                    value: Math.round((root.s.gravityX ?? 0) * 100)
                                    from: -2000
                                    to: 2000
                                    divisor: 100
                                    fieldWidth: 48
                                    onMoved: v => root.put("gravityX", v / 100, true)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "y"; color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
                                Item { Layout.fillWidth: true }
                                NumberControl {
                                    value: Math.round((root.s.gravityY ?? 0) * 100)
                                    from: -2000
                                    to: 2000
                                    divisor: 100
                                    fieldWidth: 48
                                    onMoved: v => root.put("gravityY", v / 100, true)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "down is +"
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSizeSmall
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }

                Section {
                    visible: !root.sub
                    sectionId: "fields"
                    title: "Fields"
                    keys: ["fields"]
                    Text {
                        Layout.leftMargin: root.inset
                        visible: root.fieldList.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Places that pull the particles in or push them away, and lines they can't cross."
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    Repeater {
                        model: root.fieldList.length
                        delegate: Rectangle {
                            id: row
                            Layout.leftMargin: root.inset
                            required property int index
                            readonly property var info: root.fieldList[index]
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            radius: Theme.radiusSmall
                            color: root.field === index ? Theme.accentSoft : rowArea.containsMouse ? Theme.hover : "transparent"
                            border.color: root.field === index ? Theme.accent : "transparent"
                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.fieldPicked(root.field === row.index ? -1 : row.index)
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: (row.info?.type === 4 ? "Collider " : row.info?.type === 3 ? "Floor "
                                           : row.info?.type === 2 ? "Wind "
                                           : row.info?.type === 1 ? "Vortex "
                                           : (row.info?.strength ?? 0) < 0 ? "Repeller " : "Attractor ") + (row.index + 1)
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                                IconButton {
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    iconName: "x"
                                    tip: "Remove this field"
                                    onClicked: { root.stop = -1; root.editor.removeParticleField(row.index) }
                                }
                            }
                        }
                    }
                    Flow {
                        Layout.leftMargin: root.inset
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Add"; color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall; height: 28; verticalAlignment: Text.AlignVCenter }
                        OptionPill {
                            indicator: false
                            text: "Attractor"
                            enabled: root.fieldList.length < 8
                            tip: "A point that pulls particles toward it (make it a repeller to push them away)"
                            onToggled: root.fieldPicked(root.editor.addParticleField(0))
                        }
                        OptionPill {
                            indicator: false
                            text: "Vortex"
                            enabled: root.fieldList.length < 8
                            tip: "A point the particles swirl around"
                            onToggled: root.fieldPicked(root.editor.addParticleField(1))
                        }
                        OptionPill {
                            indicator: false
                            text: "Wind"
                            enabled: root.fieldList.length < 8
                            tip: "A push along one direction everywhere, with gusts if you like"
                            onToggled: root.fieldPicked(root.editor.addParticleField(2))
                        }
                        OptionPill {
                            indicator: false
                            text: "Floor"
                            enabled: root.fieldList.length < 8
                            tip: "A line the particles can't fall through: they bounce, stay, or vanish"
                            onToggled: root.fieldPicked(root.editor.addParticleField(3))
                        }
                        OptionPill {
                            indicator: false
                            text: "Collider"
                            enabled: root.fieldList.length < 8
                            tip: "A line of any shape the particles can't cross (a roof, a ledge): they bounce, slide, stay or vanish"
                            onToggled: root.fieldPicked(root.editor.addParticleField(4))
                        }
                    }
                    ColumnLayout {
                        visible: root.fieldNow !== null
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            visible: root.fieldNow?.type === 4
                            Layout.fillWidth: true
                            Layout.leftMargin: root.inset
                            wrapMode: Text.WordWrap
                            text: "Drag its corners on the canvas. Press on the line to add one, double-click a corner to remove it."
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Segmented {
                            Layout.leftMargin: root.inset
                            visible: (root.fieldNow?.type ?? 0) <= 1
                            model: root.fieldNow?.type === 1 ? ["Clockwise", "Counter"] : ["Attract", "Repel"]
                            currentIndex: (root.fieldNow?.strength ?? 0) < 0 ? 1 : 0
                            onActivated: index => root.putField("strength", (index === 1 ? -1 : 1) * Math.abs(root.fieldNow?.strength ?? 0.2), false)
                        }
                        Segmented {
                            Layout.leftMargin: root.inset
                            visible: (root.fieldNow?.type ?? 0) >= 3
                            model: ["Bounce", "Stick", "Vanish"]
                            currentIndex: root.fieldNow?.mode ?? 0
                            onActivated: index => root.putField("mode", index, false)
                        }
                        Line {
                            visible: (root.fieldNow?.type ?? 0) >= 3 && (root.fieldNow?.mode ?? 0) === 0
                            label: "Bounciness"
                            tip: "How much of its speed into the floor or line a particle keeps when it hits (0: it slides along a collider)"
                            NumberControl {
                                value: root.fieldNow?.bounciness ?? 0
                                to: 100
                                suffix: "%"
                                fieldWidth: 56
                                onMoved: v => root.putField("bounciness", v, true)
                            }
                        }
                        Line {
                            visible: (root.fieldNow?.type ?? 0) >= 3 && (root.fieldNow?.mode ?? 0) === 0
                            label: "Friction"
                            tip: "How much of its speed along the floor or line a particle loses at each bounce"
                            NumberControl {
                                value: root.fieldNow?.friction ?? 0
                                to: 100
                                suffix: "%"
                                fieldWidth: 56
                                onMoved: v => root.putField("friction", v, true)
                            }
                        }
                        OptionPill {
                            Layout.leftMargin: root.inset
                            visible: (root.fieldNow?.type ?? 0) >= 3 && (root.fieldNow?.mode ?? 0) === 1
                            text: "Pile up"
                            tip: "Later particles come to rest on top of the ones that landed before, like snow; a steep slope sheds what a gentle one holds (not in a looping effect)"
                            checked: root.fieldNow?.stack === 1
                            onToggled: root.putField("stack", root.fieldNow?.stack === 1 ? 0 : 1, false)
                        }
                        Line {
                            visible: root.fieldNow?.type === 3
                            label: "Height"
                            tip: "The row of the canvas the floor is on (also its handle); particles above it can't cross"
                            NumberControl {
                                value: Math.floor(root.fieldNow?.y ?? 0)
                                from: -4096
                                to: 4096
                                fieldWidth: 56
                                onMoved: v => root.putField("y", v + 0.5, true)
                            }
                        }
                        Line {
                            visible: (root.fieldNow?.type ?? 0) <= 2
                            label: "Strength"
                            tip: "Pixels per frame squared, at its strongest"
                            NumberControl {
                                value: Math.round(Math.abs(root.fieldNow?.strength ?? 0) * 100)
                                to: 1000
                                divisor: 100
                                fieldWidth: 56
                                onMoved: v => root.putField("strength", ((root.fieldNow?.strength ?? 0) < 0 ? -1 : 1) * v / 100, true)
                            }
                        }
                        Line {
                            visible: root.fieldNow?.type === 2
                            label: "Direction"
                            tip: "Degrees: 0 blows right, 90 down, -90 up (also the arrow on the canvas)"
                            NumberControl {
                                value: Math.round(root.fieldNow?.angle ?? 0)
                                from: -360
                                to: 360
                                suffix: "\u00b0"
                                fieldWidth: 56
                                onMoved: v => root.putField("angle", v, true)
                            }
                        }
                        Line {
                            visible: root.fieldNow?.type === 2
                            label: "Turbulence"
                            tip: "How far the wind wanders from its direction, from none to half a turn either way"
                            NumberControl {
                                value: root.fieldNow?.turbulence ?? 0
                                to: 100
                                suffix: "%"
                                fieldWidth: 56
                                onMoved: v => root.putField("turbulence", v, true)
                            }
                        }
                        Line {
                            visible: root.fieldNow?.type === 2 && (root.fieldNow?.turbulence ?? 0) > 0
                            label: "Gust size"
                            tip: "About how many pixels across a gust is"
                            NumberControl {
                                value: root.fieldNow?.scale ?? 24
                                from: 1
                                to: 512
                                suffix: " px"
                                fieldWidth: 56
                                onMoved: v => root.putField("scale", v, true)
                            }
                        }
                        Line {
                            visible: (root.fieldNow?.type ?? 0) <= 1
                            label: "Reach"
                            tip: "How far from it the field acts, in pixels; 0 is everywhere (also the ring on the canvas)"
                            NumberControl {
                                value: root.fieldNow?.radius ?? 0
                                to: 4096
                                suffix: " px"
                                zeroText: "Everywhere"
                                fieldWidth: 80
                                onMoved: v => root.putField("radius", v, true)
                            }
                        }
                        Line {
                            visible: (root.fieldNow?.type ?? 0) <= 1
                            label: "Falloff"
                            tip: "Fade out: strongest in the middle, nothing at the edge. Constant: the same everywhere in reach"
                            AppComboBox {
                                implicitWidth: 100
                                model: ["Fade out", "Constant"]
                                currentIndex: root.fieldNow?.falloff ?? 0
                                onActivated: index => root.putField("falloff", index, false)
                            }
                        }
                        Line {
                            visible: root.fieldNow?.type !== 3
                            label: (root.fieldNow?.type ?? 0) >= 2 ? "Handle at" : "At"
                            tip: root.fieldNow?.type === 4 ? "Where its handle sits on the canvas; dragging it moves the whole line"
                                 : root.fieldNow?.type === 2 ? "Where its handle sits on the canvas; it blows everywhere" : "Canvas pixels (also the handle on the canvas)"
                            NumberControl {
                                value: Math.floor(root.fieldNow?.x ?? 0)
                                from: -4096
                                to: 4096
                                fieldWidth: 44
                                onMoved: v => root.putField("x", v + 0.5, true)
                            }
                            Text { text: ","; color: Theme.textFaint }
                            NumberControl {
                                value: Math.floor(root.fieldNow?.y ?? 0)
                                from: -4096
                                to: 4096
                                fieldWidth: 44
                                onMoved: v => root.putField("y", v + 0.5, true)
                            }
                        }
                    }
                }

                Section {
                    sectionId: "life"
                    title: "Life"
                    keys: ["lifetime", "lifetimeRandom", "sizeStart", "sizeEnd", "sizeRandom", "sizeEase",
                           "opacityStart", "opacityEnd", "opacityEase", "fadePattern", "colorMode", "colors"]
                    Line {
                        label: "Lifetime"
                        keys: ["lifetime", "lifetimeRandom"]
                        NumberControl {
                            value: root.s.lifetime ?? 0
                            from: 1
                            to: 1000
                            suffix: " f"
                            fieldWidth: 44
                            tip: "Frames a particle lives"
                            onMoved: v => root.put("lifetime", v, true)
                        }
                        Text { text: "±"; color: Theme.textFaint }
                        NumberControl {
                            value: root.s.lifetimeRandom ?? 0
                            to: 100
                            suffix: "%"
                            fieldWidth: 44
                            tip: "How much the lifetime varies"
                            onMoved: v => root.put("lifetimeRandom", v, true)
                        }
                    }

                    // Each value over a life: at birth, at death, eased between (the strip shows it).
                    Line {
                        Layout.topMargin: 4
                        label: "Size"
                        keys: ["sizeStart", "sizeEnd"]
                        tip: "Pixels at birth and at death (0 shrinks away)"
                        NumberControl {
                            value: Math.round((root.s.sizeStart ?? 0) * 10)
                            to: 2560
                            divisor: 10
                            fieldWidth: 44
                            tip: "Size at birth, in pixels"
                            onMoved: v => root.put("sizeStart", v / 10, true)
                        }
                        Text { text: "→"; color: Theme.textFaint }
                        NumberControl {
                            value: Math.round((root.s.sizeEnd ?? 0) * 10)
                            to: 2560
                            divisor: 10
                            fieldWidth: 44
                            tip: "Size at death, in pixels; 0 shrinks away"
                            onMoved: v => root.put("sizeEnd", v / 10, true)
                        }
                    }
                    Field { label: "Random"; key: "sizeRandom"; to: 100; suffix: "%"; tip: "How much each particle's whole size varies" }
                    Line {
                        label: "Easing"
                        keys: ["sizeEase"]
                        tip: "How the size gets from birth to death"
                        AppComboBox {
                            implicitWidth: 90
                            model: ["Linear", "In", "Out", "In-out", "Custom"]
                            currentIndex: root.s.sizeEase ?? 0
                            onActivated: index => root.put("sizeEase", index, false)
                        }
                    }
                    EasingCurveEditor {
                        visible: (root.s.sizeEase ?? 0) === 4
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        x1: (root.s.sizeCurve ?? [])[0] ?? 1 / 3
                        y1: (root.s.sizeCurve ?? [])[1] ?? 1 / 3
                        x2: (root.s.sizeCurve ?? [])[2] ?? 2 / 3
                        y2: (root.s.sizeCurve ?? [])[3] ?? 2 / 3
                        onEdited: (a, b, c, d, continuous) => { root.stop = -1; root.editor.setParticleCurve("sizeCurve", a, b, c, d, continuous) }
                    }
                    LifeStrip { kind: "size" }

                    Line {
                        Layout.topMargin: 4
                        label: "Opacity"
                        keys: ["opacityStart", "opacityEnd"]
                        tip: "Percent at birth and at death (0 fades away)"
                        NumberControl {
                            value: root.s.opacityStart ?? 0
                            to: 100
                            suffix: "%"
                            fieldWidth: 44
                            tip: "Opacity at birth"
                            onMoved: v => root.put("opacityStart", v, true)
                        }
                        Text { text: "→"; color: Theme.textFaint }
                        NumberControl {
                            value: root.s.opacityEnd ?? 0
                            to: 100
                            suffix: "%"
                            fieldWidth: 44
                            tip: "Opacity at death; 0 fades away"
                            onMoved: v => root.put("opacityEnd", v, true)
                        }
                    }
                    Line {
                        label: "Easing"
                        keys: ["opacityEase"]
                        tip: "How the opacity gets from birth to death"
                        AppComboBox {
                            implicitWidth: 90
                            model: ["Linear", "In", "Out", "In-out", "Custom"]
                            currentIndex: root.s.opacityEase ?? 0
                            onActivated: index => root.put("opacityEase", index, false)
                        }
                    }
                    EasingCurveEditor {
                        visible: (root.s.opacityEase ?? 0) === 4
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        x1: (root.s.opacityCurve ?? [])[0] ?? 1 / 3
                        y1: (root.s.opacityCurve ?? [])[1] ?? 1 / 3
                        x2: (root.s.opacityCurve ?? [])[2] ?? 2 / 3
                        y2: (root.s.opacityCurve ?? [])[3] ?? 2 / 3
                        onEdited: (a, b, c, d, continuous) => { root.stop = -1; root.editor.setParticleCurve("opacityCurve", a, b, c, d, continuous) }
                    }
                    LifeStrip { kind: "opacity" }
                    Line {
                        label: "Fade style"
                        keys: ["fadePattern"]
                        tip: "Smooth scales the alpha; the others show or hide whole pixels by a dither pattern"
                        AppComboBox {
                            implicitWidth: 110
                            model: ["Smooth", "Bayer 2×2", "Bayer 4×4", "Bayer 8×8", "Blue noise"]
                            currentIndex: root.s.fadePattern ?? 0
                            onActivated: index => root.put("fadePattern", index, false)
                        }
                    }

                    Text {
                        visible: root.ownColors
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        wrapMode: Text.WordWrap
                        text: "The brush keeps its own colors, so the colors below aren't used."
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.topMargin: 4
                    }
                    Line {
                        Layout.topMargin: 4
                        label: "Colors"
                        keys: ["colorMode", "colors"]
                        tip: "Over life: each particle passes through the colors in order. Random: each gets one of them"
                    }
                    Segmented {
                        Layout.leftMargin: root.inset
                        enabled: !root.ownColors
                        opacity: root.ownColors ? 0.35 : 1
                        model: ["Over life", "Random"]
                        currentIndex: root.s.colorMode ?? 0
                        onActivated: index => root.put("colorMode", index, false)
                    }

                    // The colors, in order. Click one to arm it (a ring and a pipette): the next color
                    // chosen on the canvas or in the Colors panel goes to it. Right-click removes it,
                    // drag reorders.
                    Item {
                        Layout.leftMargin: root.inset
                        id: strip
                        enabled: !root.ownColors
                        opacity: root.ownColors ? 0.35 : 1
                        readonly property int slot: 30
                        readonly property int perRow: Math.max(1, Math.floor(width / slot))
                        property int dragFrom: -1
                        property int dragTo: -1
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(Math.max(1, root.colors.length) / perRow) * slot + 4

                        function indexAtPoint(x: real, y: real): int {
                            const col = Math.max(0, Math.min(perRow - 1, Math.floor(x / slot)))
                            const row = Math.max(0, Math.floor(y / slot))
                            return Math.min(root.colors.length - 1, row * perRow + col)
                        }
                        function moved(list: var, from: int, to: int): var {
                            const next = list.slice()
                            next.splice(to, 0, next.splice(from, 1)[0])
                            return next
                        }

                        Repeater {
                            model: root.colors.length
                            delegate: PickSwatch {
                                id: swatch
                                required property int index
                                x: 3 + (index % strip.perRow) * strip.slot
                                y: 3 + Math.floor(index / strip.perRow) * strip.slot
                                implicitWidth: strip.slot - 6
                                implicitHeight: strip.slot - 6
                                armed: root.stop === index
                                color: root.colors[index] ?? "white"
                                opacity: strip.dragFrom === index ? 0.4 : 1

                                MouseArea {
                                    id: swatchArea
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    preventStealing: true
                                    cursorShape: Qt.PointingHandCursor
                                    property point pressAt
                                    property bool dragging: false
                                    onPressed: m => { pressAt = Qt.point(m.x, m.y); dragging = false }
                                    onPositionChanged: m => {
                                        if (!(m.buttons & Qt.LeftButton))
                                            return
                                        if (!dragging && Math.hypot(m.x - pressAt.x, m.y - pressAt.y) > 6) {
                                            dragging = true
                                            strip.dragFrom = swatch.index
                                        }
                                        if (dragging) {
                                            const p = mapToItem(strip, m.x, m.y)
                                            strip.dragTo = strip.indexAtPoint(p.x, p.y)
                                        }
                                    }
                                    onReleased: {
                                        if (dragging && strip.dragTo >= 0 && strip.dragTo !== strip.dragFrom) {
                                            root.editor.setParticleColors(strip.moved(root.colors, strip.dragFrom, strip.dragTo))
                                            if (root.stop >= 0)
                                                root.stop = strip.dragTo
                                        }
                                        strip.dragFrom = -1
                                        strip.dragTo = -1
                                    }
                                    onClicked: m => {
                                        if (dragging)
                                            return
                                        if (m.button === Qt.RightButton)
                                            root.removeStop(swatch.index)
                                        else if (root.stop === swatch.index)
                                            root.stop = -1
                                        else
                                            root.armStop(swatch.index)
                                    }
                                    onCanceled: { dragging = false; strip.dragFrom = -1; strip.dragTo = -1 }
                                }
                            }
                        }
                        // Where a dragged color would land.
                        Rectangle {
                            visible: strip.dragFrom >= 0 && strip.dragTo >= 0 && strip.dragTo !== strip.dragFrom
                            x: 3 + (strip.dragTo % strip.perRow) * strip.slot - 4
                            y: 3 + Math.floor(strip.dragTo / strip.perRow) * strip.slot
                            width: 2
                            height: strip.slot - 6
                            color: Theme.accent
                        }
                    }

                    RowLayout {
                        Layout.leftMargin: root.inset
                        Layout.fillWidth: true
                        spacing: 2
                        IconButton {
                            implicitWidth: 26
                            implicitHeight: 26
                            iconName: "plus"
                            enabled: root.colors.length < 16
                            tip: "Add a color (a copy of the armed one, else the primary color)"
                            onClicked: root.addStop()
                        }
                        IconButton {
                            implicitWidth: 26
                            implicitHeight: 26
                            iconName: "x"
                            enabled: root.stop >= 0 && root.colors.length > 1
                            tip: "Remove the armed color (or right-click a color)"
                            onClicked: root.removeStop(root.stop)
                        }
                        IconButton {
                            implicitWidth: 26
                            implicitHeight: 26
                            iconName: "arrow-left-right"
                            enabled: root.colors.length > 1
                            tip: "Reverse the order"
                            onClicked: {
                                root.editor.setParticleColors(root.colors.slice().reverse())
                                if (root.stop >= 0)
                                    root.stop = root.colors.length - 1 - root.stop
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.stop >= 0 ? "Pick a color" : ""
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }

                    // Over life, what a particle passes through (each color spread evenly).
                    Row {
                        Layout.leftMargin: root.inset
                        visible: root.s.colorMode === 0 && root.colors.length > 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        Repeater {
                            model: Math.max(0, root.colors.length - 1)
                            delegate: Rectangle {
                                id: band
                                required property int index
                                width: (parent?.width ?? 0) / Math.max(1, root.colors.length - 1)
                                height: 8
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: root.colors[band.index] ?? "white" }
                                    GradientStop { position: 1; color: root.colors[band.index + 1] ?? "white" }
                                }
                            }
                        }
                    }
                }

                Section {
                    sectionId: "look"
                    title: "Look"
                    keys: ["shape", "brushColors", "angle", "angleRandom", "spin", "spinRandom", "faceMotion", "angleSnap"]
                    Line {
                        label: "Shape"
                        keys: ["shape"]
                        AppComboBox {
                            id: shapeBox
                            implicitWidth: 110
                            model: ["Square", "Circle", "Plus", "Diamond", "Custom brush"]
                            currentIndex: root.s.shape ?? 0
                            onActivated: index => {
                                if (index !== 4) {
                                    root.put("shape", index, false)
                                } else if ((root.s.brushWidth ?? 0) > 0) {
                                    root.put("shape", 4, false)
                                } else if (!root.editor.useCustomBrushAsParticle()) {
                                    currentIndex = Qt.binding(() => root.s.shape ?? 0) // nothing to take
                                    root.brushHint = true
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.s.shape === 4 || root.brushHint
                        Layout.leftMargin: root.inset
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: (root.s.brushWidth ?? 0) > 0
                                  ? "Brush " + root.s.brushWidth + "\u00d7" + root.s.brushHeight
                                    + ". The size is its longest side."
                                  : "Make a custom brush first: select pixels on another layer and press "
                                    + (Shortcuts.labels.brushFromSelection ?? "the brush shortcut") + "."
                            color: root.brushHint && (root.s.brushWidth ?? 0) === 0 ? Theme.error : Theme.textFaint
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        OptionPill {
                            indicator: false
                            text: "Use the current brush"
                            visible: root.editor.hasCustomBrush
                            tip: "Take the shape from the custom brush now in hand"
                            onToggled: { root.brushHint = false; root.editor.useCustomBrushAsParticle() }
                        }
                    }
                    Segmented {
                        Layout.leftMargin: root.inset
                        visible: root.s.shape === 4 && (root.s.brushWidth ?? 0) > 0
                        model: ["Tint", "Brush colors"]
                        currentIndex: root.s.brushColors === 1 ? 1 : 0
                        onActivated: index => root.put("brushColors", index, false)
                    }

                    // Turning a circle shows nothing, so these are for the other shapes.
                    Text {
                        visible: (root.s.shape ?? 0) !== 1
                        Layout.leftMargin: root.inset
                        text: "Rotation"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        Layout.topMargin: 4
                    }
                    Line {
                        visible: (root.s.shape ?? 0) !== 1
                        label: "Angle"
                        keys: ["angle", "angleRandom"]
                        NumberControl {
                            value: Math.round(root.s.angle ?? 0)
                            from: -360
                            to: 360
                            suffix: "\u00b0"
                            fieldWidth: 44
                            tip: "Clockwise, 0 is the shape as drawn"
                            onMoved: v => root.put("angle", v, true)
                        }
                        Text { text: "\u00b1"; color: Theme.textFaint }
                        NumberControl {
                            value: root.s.angleRandom ?? 0
                            to: 180
                            suffix: "\u00b0"
                            fieldWidth: 44
                            tip: "How far each particle's angle may differ"
                            onMoved: v => root.put("angleRandom", v, true)
                        }
                    }
                    Line {
                        visible: (root.s.shape ?? 0) !== 1
                        label: "Spin"
                        keys: ["spin", "spinRandom"]
                        NumberControl {
                            value: Math.round((root.s.spin ?? 0) * 10)
                            from: -3600
                            to: 3600
                            divisor: 10
                            suffix: "\u00b0"
                            fieldWidth: 44
                            tip: "Degrees per frame"
                            onMoved: v => root.put("spin", v / 10, true)
                        }
                        Text { text: "\u00b1"; color: Theme.textFaint }
                        NumberControl {
                            value: root.s.spinRandom ?? 0
                            to: 100
                            suffix: "%"
                            fieldWidth: 44
                            tip: "How much each particle's spin varies"
                            onMoved: v => root.put("spinRandom", v, true)
                        }
                    }
                    Line {
                        visible: (root.s.shape ?? 0) !== 1
                        label: "Snap to"
                        keys: ["angleSnap"]
                        tip: "Round the angle to steps, for crisp pixel art"
                        AppComboBox {
                            implicitWidth: 90
                            model: ["Free", "15\u00b0", "45\u00b0", "90\u00b0"]
                            currentIndex: root.s.angleSnap ?? 0
                            onActivated: index => root.put("angleSnap", index, false)
                        }
                    }
                    OptionPill {
                        visible: (root.s.shape ?? 0) !== 1
                        Layout.leftMargin: root.inset
                        text: "Face the motion"
                        tip: "Point along the way it moves (the shape's right side leads); the angle turns it from there"
                        checked: root.s.faceMotion === 1
                        onToggled: root.put("faceMotion", root.s.faceMotion === 1 ? 0 : 1, false)
                    }
                }

                Section {
                    visible: !root.sub
                    sectionId: "subs"
                    title: "Sub-particles"
                    keys: ["subTrigger", "subInherit"]
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        wrapMode: Text.WordWrap
                        text: "Each particle can give birth to more where it ends or where it hits a floor: a splash, a burst of sparks."
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    Segmented {
                        Layout.leftMargin: root.inset
                        model: ["Off", "On death", "On hit"]
                        currentIndex: root.s.subTrigger ?? 0
                        onActivated: index => root.put("subTrigger", index, false)
                    }
                    Field {
                        visible: (root.s.subTrigger ?? 0) !== 0
                        label: "Carry speed"
                        key: "subInherit"
                        to: 100
                        suffix: "%"
                        tip: "How much of the parent's speed they start with"
                    }
                    OptionPill {
                        visible: (root.s.subTrigger ?? 0) !== 0
                        Layout.leftMargin: root.inset
                        indicator: false
                        text: "Edit the sub-particles"
                        tip: "The sections above then change them (count, speed, spread, colors, ...)"
                        onToggled: { root.stop = -1; root.editor.particleContext = 1 }
                    }
                }
            }
        }
    }
}

