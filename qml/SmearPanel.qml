pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// The settings of the current Smear group (it shares its place with the particle settings, see
// PanelColumns' "live" slot): the pose on this frame (each change sets its key, as a drag on the canvas
// does), the keys with their easing, and the smear itself. Everything goes through
// Editor.setSmearPose / setSmearSetting / setSmearKeyEase / removeSmearKey: one undo step each, a drag
// of a number one step.
Panel {
    id: root

    required property Editor editor

    // What the window arranges (PanelColumns), see ColorPanel.
    readonly property real wantHeight: 520
    readonly property real minHeight: 160
    readonly property int shrinkRank: 1
    movable: true
    padding: 10

    readonly property var s: editor.smearState
    readonly property var pose: s.pose ?? {}
    readonly property var keyList: s.keyList ?? []
    readonly property int inset: 18 // where the labels start: everything else lines up with them

    // How much of the pose handles shows on the canvas (0 all, 1 faded, 2 none); the window keeps it.
    property int overlayMode: 0
    signal overlayCycled()

    // Sections the user folded, as JSON {id: true} (remembered by the window).
    property string collapsedJson: "{}"
    signal collapsedEdited(string json)
    readonly property var folded: {
        try { return JSON.parse(collapsedJson) } catch (e) { return {} }
    }
    function toggleSection(id: string) {
        const next = Object.assign({}, folded)
        if (next[id])
            delete next[id]
        else
            next[id] = true
        collapsedEdited(JSON.stringify(next))
    }

    // The built-in values (core::SmearRecipe, core::SmearPose): a value that differs has a reset dot.
    readonly property var defaults: ({ x: 0, y: 0, rotation: 0, scaleX: 1, scaleY: 1, skewX: 0, skewY: 0,
                                       trail: 1, behind: 1, style: 0, taper: 0, fade: 0, fadePattern: 2,
                                       outline: 0, smoothPath: 0, facePath: 0 })
    readonly property var poseKeys: ["x", "y", "rotation", "scaleX", "scaleY", "skewX", "skewY"]
    function valueOf(key: string): real { return poseKeys.indexOf(key) >= 0 ? (pose[key] ?? defaults[key]) : (s[key] ?? defaults[key]) }
    function differs(key: string): bool { return Math.abs(valueOf(key) - defaults[key]) > 1e-6 }
    function changed(keys: var): bool { return keys.some(k => differs(k)) }
    function reset(keys: var) { arming = ""; editor.resetSmearSettings(keys) }

    function setPose(key: string, value: real) {
        const o = {}
        o[key] = value
        editor.setSmearPose(o, true)
    }

    // The color being picked for (empty: none): click a swatch, then pick a color anywhere.
    property string arming: ""
    Timer {
        id: settle
        interval: 1200
        onTriggered: root.arming = ""
    }
    Connections {
        target: root.editor
        function onColorsChanged() {
            if (root.arming === "")
                return
            root.editor.setSmearSetting(root.arming, root.editor.primaryColor, true)
            settle.restart()
        }
        function onCurrentLayerChanged() { root.arming = "" }
    }
    function arm(key: string) {
        if (arming === key) {
            arming = ""
            return
        }
        arming = key
        editor.primaryColor = s[key]
    }

    // A labelled line: the label, then the controls (right aligned).
    // The dot of a value that differs from its built-in one: click it (or double-click the label) to reset.
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

    // A labelled line: its reset dot (in the inset), the label, then the controls (right aligned).
    component Line: RowLayout {
        id: line
        property string label
        property var keys: []
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
                onDoubleClicked: if (line.keys.length > 0) root.reset(line.keys)
            }
        }
        RowLayout {
            id: controls
            spacing: 4
        }
    }

    // A section's header: click to fold it; the reset puts its values back.
    component Heading: RowLayout {
        id: heading
        required property string sectionId
        property string text
        property var keys: []
        readonly property bool open: !root.folded[sectionId]
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
                    source: heading.open ? "qrc:/icons/chevron-down.svg" : "qrc:/icons/chevron-right.svg"
                    width: 14
                    height: 14
                    sourceSize: Qt.size(28, 28)
                    opacity: 0.6
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: heading.text
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.DemiBold
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleSection(heading.sectionId)
            }
        }
        IconButton {
            visible: root.changed(heading.keys)
            implicitWidth: 22
            implicitHeight: 22
            iconName: "rotate-ccw"
            tip: "Reset this section to its built-in values"
            onClicked: root.reset(heading.keys)
        }
    }

    component Rule: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.separator
    }

    // The preset gallery: each one playing on this art; click to apply, right-click your own to rename or
    // delete it; save the current motion as one of your own.
    Popup {
        id: gallery
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Panel { solid: true }
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }
        readonly property var presets: root.editor.smearPresets
        property var previews: ({})
        // The preset picked (it plays; Apply or a double-click uses it).
        property string chosen: ""
        function use(id: string) {
            root.arming = ""
            root.editor.applySmearPreset(id)
            close()
        }
        onAboutToShow: {
            const out = {}
            for (const p of presets)
                out[p.id] = root.editor.smearPresetPreview(p.id)
            previews = out
            chosen = ""
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Presets"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                Layout.maximumWidth: 3 * 96
                wrapMode: Text.WordWrap
                text: "Click one to see it move, double-click it (or Apply) to use it. From this frame, on this art: it replaces the keys, the pivot and the smear settings (the colors stay)."
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
            }
            Flickable {
                Layout.preferredWidth: 3 * 96 + 12 // room for the scroll bar
                Layout.preferredHeight: Math.min(grid.implicitHeight, 3 * 108)
                contentWidth: width
                contentHeight: grid.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                AppScrollBar.vertical: AppScrollBar {}
                Grid {
                    id: grid
                    columns: 3
                    spacing: 6
                    Repeater {
                        model: gallery.presets.length
                        delegate: Rectangle {
                            id: card
                            required property int index
                            readonly property var info: gallery.presets[index] ?? {}
                            readonly property var preview: gallery.previews[info.id] ?? ({})
                            // At rest the frame that tells most (the smear at its widest); pointed at, it
                            // plays, holding its last frame a moment before it starts again.
                            readonly property bool picked: gallery.chosen === (info.id ?? "")
                            readonly property bool playing: picked || hover.hovered
                            property int step: 0
                            function show() {
                                // Read from the preview itself: the properties made from it may not have
                                // caught up yet when it has just changed.
                                const count = preview.count ?? 0
                                if (count === 0)
                                    return
                                const at = playing ? Math.min(count - 1, step % (count + 4)) : (preview.still ?? 0)
                                picture.image = root.editor.smearPresetFrame(info.id ?? "", at)
                            }
                            onPreviewChanged: show()
                            onPlayingChanged: {
                                step = 0
                                show()
                            }
                            Timer { // its own clock, running only while it plays
                                running: card.playing && gallery.visible
                                interval: 90
                                repeat: true
                                onTriggered: {
                                    card.step++
                                    card.show()
                                }
                            }
                            width: 90
                            height: 102
                            radius: Theme.radiusSmall
                            color: card.picked ? Theme.accentSoft : hover.hovered ? Theme.hover : "transparent"
                            border.color: card.picked || hover.hovered ? Theme.accent : Theme.separator
                            border.width: card.picked ? 2 : 1
                            HoverHandler {
                                id: hover
                                cursorShape: Qt.PointingHandCursor
                            }
                            ImagePreview {
                                id: picture
                                x: 5
                                y: 5
                                width: parent.width - 10
                                height: 70
                            }
                            Text {
                                x: 5
                                y: 80
                                width: parent.width - 10
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: card.info.name ?? ""
                                color: card.info.user ? Theme.accent : Theme.text
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: mouse => {
                                    if (mouse.button === Qt.RightButton) {
                                        if (card.info.user) {
                                            userMenu.presetId = card.info.id
                                            userMenu.presetName = card.info.name
                                            userMenu.popup()
                                        }
                                        return
                                    }
                                    gallery.chosen = card.info.id ?? ""
                                }
                                onDoubleClicked: mouse => {
                                    if (mouse.button === Qt.LeftButton)
                                        gallery.use(card.info.id ?? "")
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Right-click your own to rename or delete it"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
                AppButton {
                    text: "Save as preset\u2026"
                    enabled: (root.s.keys ?? []).length > 0
                    onClicked: nameBox.start("save", "", "")
                }
                AppButton {
                    text: "Apply"
                    primary: true
                    enabled: gallery.chosen !== ""
                    onClicked: gallery.use(gallery.chosen)
                }
            }
        }
    }
    AppMenu {
        id: userMenu
        property string presetId
        property string presetName
        AppMenuItem { text: "Rename\u2026"; onTriggered: nameBox.start("rename", userMenu.presetId, userMenu.presetName) }
        AppMenuItem { text: "Delete"; danger: true; onTriggered: root.editor.deleteSmearPreset(userMenu.presetId) }
    }
    // A name for the motion being saved as a preset, or for one being renamed.
    Popup {
        id: nameBox
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        padding: 22
        closePolicy: Popup.CloseOnEscape
        background: Panel { solid: true }
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }
        property string mode: "save"
        property string presetId
        onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
        function start(how: string, id: string, name: string) {
            mode = how
            presetId = id
            nameField.text = name
            open()
        }
        function done() {
            const ok = mode === "save" ? root.editor.saveSmearPreset(nameField.text)
                                       : root.editor.renameSmearPreset(presetId, nameField.text)
            if (!ok)
                return
            close()
            if (gallery.visible) // the new one shows in the gallery
                gallery.previews = Object.assign({}, gallery.previews, (() => {
                    const o = {}
                    for (const p of gallery.presets)
                        if (!(p.id in gallery.previews))
                            o[p.id] = root.editor.smearPresetPreview(p.id)
                    return o
                })())
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: nameBox.mode === "save" ? "Save as a preset" : "Rename the preset"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                visible: nameBox.mode === "save"
                Layout.maximumWidth: 320
                wrapMode: Text.WordWrap
                text: "This motion, for art of any size. The same name replaces the one saved before."
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            AppTextField {
                id: nameField
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                maximumLength: 40
                onAccepted: nameBox.done()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; onClicked: nameBox.close() }
                AppButton { text: nameBox.mode === "save" ? "Save" : "Rename"; primary: true; enabled: nameField.text.trim() !== ""; onClicked: nameBox.done() }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { // the group's name: which one is being posed
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: (root.editor.layers[root.editor.currentLayer] ?? {}).name ?? "Smear"
                color: Theme.text
                font.pixelSize: Theme.fontSize
                font.weight: Font.DemiBold
            }
            // Ready-made motions (and your own): replace the keys (from this frame), the pivot and the
            // smear settings. A gallery, each previewed on this art.
            OptionPill {
                id: presetPill
                indicator: false
                text: "Preset  \u25be"
                tip: "Start over from a ready-made motion, from this frame: it replaces the keys, the pivot and the smear settings"
                onToggled: gallery.open()
            }
            // The posing handles on the canvas: shown, faded or hidden (shared with the particle controls).
            IconButton {
                implicitWidth: 26
                implicitHeight: 26
                iconName: root.overlayMode === 2 ? "eye-off" : "eye"
                active: root.overlayMode === 1
                tip: (root.overlayMode === 0 ? "Pose handles on the canvas: shown"
                      : root.overlayMode === 1 ? "Pose handles on the canvas: faded (full when you point at them)"
                      : "Pose handles on the canvas: hidden") + " \u00b7 click to change"
                       + (Shortcuts.labels.liveOverlay ? " (" + Shortcuts.labels.liveOverlay + ")" : "")
                onClicked: root.overlayCycled()
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

            ColumnLayout {
                id: body
                width: view.width - 12 // room for the scroll bar
                spacing: 6

                // --- the pose on this frame ---
                Heading {
                    id: frameHeading
                    sectionId: "frame"
                    text: "Frame " + ((root.s.frame ?? 0) + 1)
                    keys: root.poseKeys
                }
                ColumnLayout {
                visible: frameHeading.open
                Layout.fillWidth: true
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.inset
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.s.key ? "A key: the pose here." : "Between keys: a change makes a key here."
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    OptionPill {
                        visible: !(root.s.key ?? false)
                        indicator: false
                        text: "Add key"
                        tip: "A key on this frame with the pose it has now"
                        onToggled: root.editor.setSmearPose({}, false)
                    }
                    OptionPill {
                        visible: root.s.key ?? false
                        indicator: false
                        text: "Remove"
                        tip: "Remove this frame's key (the motion goes from the key before to the key after)"
                        onToggled: root.editor.removeSmearKey(root.s.frame ?? 0)
                    }
                }
                Line {
                    label: "Move"
                    keys: ["x", "y"]
                    NumberControl {
                        value: Math.round(root.pose.x ?? 0)
                        from: -4096
                        to: 4096
                        fieldWidth: 44
                        tip: "Pixels right"
                        onMoved: v => root.setPose("x", v)
                    }
                    Text { text: ","; color: Theme.textFaint }
                    NumberControl {
                        value: Math.round(root.pose.y ?? 0)
                        from: -4096
                        to: 4096
                        fieldWidth: 44
                        tip: "Pixels down"
                        onMoved: v => root.setPose("y", v)
                    }
                }
                Line {
                    label: "Turn"
                    keys: ["rotation"]
                    NumberControl {
                        value: Math.round(root.pose.rotation ?? 0)
                        from: -3600
                        to: 3600
                        suffix: "°"
                        fieldWidth: 56
                        tip: "Degrees clockwise about the pivot (past 360 spins more than once)"
                        onMoved: v => root.setPose("rotation", v)
                    }
                }
                Line {
                    label: "Scale"
                    keys: ["scaleX", "scaleY"]
                    NumberControl {
                        value: Math.round((root.pose.scaleX ?? 1) * 100)
                        from: -1000
                        to: 1000
                        suffix: "%"
                        fieldWidth: 44
                        tip: "Width, about the pivot (negative flips it)"
                        onMoved: v => root.setPose("scaleX", v / 100)
                    }
                    Text { text: "×"; color: Theme.textFaint }
                    NumberControl {
                        value: Math.round((root.pose.scaleY ?? 1) * 100)
                        from: -1000
                        to: 1000
                        suffix: "%"
                        fieldWidth: 44
                        tip: "Height, about the pivot (negative flips it)"
                        onMoved: v => root.setPose("scaleY", v / 100)
                    }
                }
                Line {
                    label: "Skew"
                    keys: ["skewX", "skewY"]
                    NumberControl {
                        value: Math.round(root.pose.skewX ?? 0)
                        from: -85
                        to: 85
                        suffix: "°"
                        fieldWidth: 44
                        tip: "Slants it sideways (or Alt-drag the top or bottom handle on the canvas)"
                        onMoved: v => root.setPose("skewX", v)
                    }
                    Text { text: ","; color: Theme.textFaint }
                    NumberControl {
                        value: Math.round(root.pose.skewY ?? 0)
                        from: -85
                        to: 85
                        suffix: "°"
                        fieldWidth: 44
                        tip: "Slants it up or down (or Alt-drag a side handle on the canvas)"
                        onMoved: v => root.setPose("skewY", v)
                    }
                }
                }
                Rule {}

                // --- the keys ---
                Heading {
                    id: keysHeading
                    sectionId: "keys"
                    text: "Keys"
                    keys: ["smoothPath", "facePath"]
                }
                ColumnLayout {
                visible: keysHeading.open
                Layout.fillWidth: true
                spacing: 6
                Text {
                    visible: root.keyList.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: root.inset
                    wrapMode: Text.WordWrap
                    text: "None yet: pose the art on the canvas (or change a number above) on a frame to make one."
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
                Flow { // (they wrap: side by side they are wider than the panel)
                    Layout.fillWidth: true
                    Layout.leftMargin: root.inset
                    spacing: 6
                    OptionPill {
                        text: "Smooth path"
                        tip: "The move between keys follows a curve through their places instead of straight lines"
                        checked: (root.s.smoothPath ?? 0) === 1
                        onToggled: root.editor.setSmearSetting("smoothPath", (root.s.smoothPath ?? 0) === 1 ? 0 : 1, false)
                    }
                    OptionPill {
                        text: "Face the path"
                        tip: "The art turns along the way it goes (art drawn pointing right points ahead), on top of the keys' turn"
                        checked: (root.s.facePath ?? 0) === 1
                        onToggled: root.editor.setSmearSetting("facePath", (root.s.facePath ?? 0) === 1 ? 0 : 1, false)
                    }
                }
                Repeater {
                    model: root.keyList.length
                    delegate: ColumnLayout {
                        id: keyRow
                        required property int index
                        readonly property var info: root.keyList[index] ?? {}
                        readonly property bool last: index === root.keyList.length - 1
                        readonly property var curve: info.curve ?? [1 / 3, 1 / 3, 2 / 3, 2 / 3]
                        Layout.fillWidth: true
                        Layout.leftMargin: root.inset
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: "Frame " + ((keyRow.info.frame ?? 0) + 1)
                                color: (keyRow.info.frame ?? -1) === root.s.frame ? Theme.accent : Theme.text
                                font.pixelSize: Theme.fontSizeSmall
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.editor.currentFrame = keyRow.info.frame ?? 0
                                }
                            }
                            AppComboBox {
                                visible: !keyRow.last // the last key has nothing after it to ease into
                                implicitWidth: 90
                                model: ["Linear", "In", "Out", "In-out", "Custom"]
                                currentIndex: keyRow.info.ease ?? 0
                                onActivated: index => root.editor.setSmearKeyEase(keyRow.info.frame ?? 0, index)
                            }
                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                iconName: "x"
                                danger: true
                                tip: "Remove this key"
                                onClicked: root.editor.removeSmearKey(keyRow.info.frame ?? 0)
                            }
                        }
                        EasingCurveEditor {
                            visible: !keyRow.last && (keyRow.info.ease ?? 0) === 4
                            Layout.fillWidth: true
                            x1: keyRow.curve[0]
                            y1: keyRow.curve[1]
                            x2: keyRow.curve[2]
                            y2: keyRow.curve[3]
                            onEdited: (a, b, c, d, continuous) => root.editor.setSmearKeyCurve(keyRow.info.frame ?? 0, a, b, c, d, continuous)
                        }
                    }
                }
                }
                Rule {}

                // --- the smear ---
                Heading {
                    id: smearHeading
                    sectionId: "smear"
                    text: "Smear"
                    keys: ["trail", "behind", "style", "taper", "fade", "fadePattern", "outline"]
                }
                ColumnLayout {
                visible: smearHeading.open
                Layout.fillWidth: true
                spacing: 6
                Line {
                    label: "Trail"
                    keys: ["trail"]
                    NumberControl {
                        value: Math.round((root.s.trail ?? 1) * 100)
                        to: 100
                        suffix: "%"
                        fieldWidth: 56
                        tip: "How far back it reaches: 100% is all the way to where the art was a frame before"
                        onMoved: v => root.editor.setSmearSetting("trail", v / 100, true)
                    }
                }
                Segmented {
                    Layout.leftMargin: root.inset
                    model: ["Behind", "In front"]
                    currentIndex: (root.s.behind ?? 1) === 1 ? 0 : 1
                    onActivated: index => root.editor.setSmearSetting("behind", index === 0 ? 1 : 0, false)
                }
                Line { label: "Style"; keys: ["style"] }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.inset
                    spacing: 8
                    Segmented {
                        model: ["Streak", "Flat"]
                        currentIndex: root.s.style ?? 0
                        onActivated: index => root.editor.setSmearSetting("style", index, false)
                    }
                    PickSwatch {
                        visible: (root.s.style ?? 0) === 1
                        implicitWidth: 24
                        implicitHeight: 24
                        color: root.s.color ?? "white"
                        armed: root.arming === "color"
                        onClicked: root.arm("color")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.inset
                    wrapMode: Text.WordWrap
                    text: (root.s.style ?? 0) === 0 ? "The art's own colors, dragged along."
                                                    : "One color filling where it swept: click the swatch, then pick a color."
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
                Line {
                    label: "Taper"
                    keys: ["taper"]
                    NumberControl {
                        value: Math.round((root.s.taper ?? 0) * 100)
                        to: 100
                        suffix: "%"
                        fieldWidth: 56
                        tip: "How much it narrows toward its tail (toward the pivot)"
                        onMoved: v => root.editor.setSmearSetting("taper", v / 100, true)
                    }
                }
                Line {
                    label: "Fade"
                    keys: ["fade"]
                    NumberControl {
                        value: Math.round((root.s.fade ?? 0) * 100)
                        to: 100
                        suffix: "%"
                        fieldWidth: 56
                        tip: "How much it fades toward its tail"
                        onMoved: v => root.editor.setSmearSetting("fade", v / 100, true)
                    }
                }
                Line {
                    visible: (root.s.fade ?? 0) > 0
                    label: "Fade style"
                    keys: ["fadePattern"]
                    AppComboBox {
                        implicitWidth: 110
                        model: ["Smooth", "Bayer 2×2", "Bayer 4×4", "Bayer 8×8", "Blue noise"]
                        currentIndex: root.s.fadePattern ?? 2
                        onActivated: index => root.editor.setSmearSetting("fadePattern", index, false)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.inset
                    spacing: 8
                    OptionPill {
                        text: "Outline"
                        tip: "A one pixel outline around the smear (not around the art)"
                        checked: (root.s.outline ?? 0) === 1
                        onToggled: root.editor.setSmearSetting("outline", (root.s.outline ?? 0) === 1 ? 0 : 1, false)
                    }
                    PickSwatch {
                        visible: (root.s.outline ?? 0) === 1
                        implicitWidth: 24
                        implicitHeight: 24
                        color: root.s.outlineColor ?? "black"
                        armed: root.arming === "outlineColor"
                        onClicked: root.arm("outlineColor")
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    visible: root.arming !== ""
                    Layout.leftMargin: root.inset
                    text: "Pick a color"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
                }
            }
        }
    }
}
