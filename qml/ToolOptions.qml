pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Options of the current tool, in their own panel under the top bar: the
// tool's icon and name first, then only what applies to it. Hidden for tools
// without options.
Panel {
    id: root

    required property Editor editor
    required property CanvasView canvas
    property bool suppressed: false // another panel (canvas size) has the spot

    // Indexed by Editor.Tool.
    readonly property var tools: [
        { icon: "pencil", name: "Pencil" },
        { icon: "eraser", name: "Eraser" },
        { icon: "paint-bucket", name: "Fill" },
        { icon: "pipette", name: "Eyedropper" },
        { icon: "hand", name: "Hand" },
        { icon: "slash", name: "Line" },
        { icon: "square", name: "Rectangle" },
        { icon: "circle", name: "Ellipse" },
        { icon: "square-dashed", name: "Select" },
        { icon: "move", name: "Move" },
        { icon: "lasso", name: "Lasso" },
        { icon: "wand-sparkles", name: "Magic Wand" },
        { icon: "droplet", name: "Blur" },
        { icon: "shuffle", name: "Jumble" },
        { icon: "type", name: "Text" },
        { icon: "slice", name: "Slice" },
        { icon: "gradient", name: "Gradient" },
        { icon: "shapes", name: "Source" },
        { icon: "magnet", name: "Field" },
        { icon: "scaling", name: "Pose" },
    ]
    readonly property var tool: transforming ? { icon: "scaling", name: "Transform" }
                              : editor.liveLayer && editor.tool === Editor.Move ? { icon: "crosshair", name: "Emitter" }
                              : tools[editor.tool] ?? tools[0]

    // Shown by the (i) button, indexed by Editor.Tool.
    readonly property string alt: Qt.platform.os === "osx" ? "Option" : "Alt"
    readonly property string cmd: Qt.platform.os === "osx" ? "\u2318" : "Ctrl+"
    // The brush size gesture (physical Control on a Mac, whose Cmd is `cmd`).
    readonly property string sizeDrag: "Ctrl+" + alt + " + drag sideways: brush size (custom brush: scale; + Shift: brush angle)"
    // Shared by the pencil, eraser and fill hints.
    readonly property string picks: alt + "-click: pick a color (right: the secondary) \u00b7 "
                                    + cmd + "click: select the layer under the cursor"
    // A rebindable action's first key, as shown on this platform.
    function key(id: string): string {
        const label = Shortcuts.labels[id]
        return label ? label : qsTr("(no key)")
    }
    readonly property var hints: [
        "<b>Pencil</b><br>Shift-click: straight line from the last point<br>Right mouse: secondary color<br>" + picks + "<br>" + key("brushSmaller") + " and " + key("brushBigger") + ": brush size \u00b7 " + sizeDrag + "<br>"
            + key("brushFromSelection") + ": the selected pixels become a custom brush<br>"
            + "Shading: moves colors one step along the palette (Shift-click swatches to pick a range; none: all of it)<br>"
            + "Shading, left mouse: toward later colors \u00b7 right: toward earlier \u00b7 other colors stay",
        "<b>Eraser</b><br>Shift-click: straight line from the last point<br>" + key("brushSmaller") + " and " + key("brushBigger") + ": brush size \u00b7 " + sizeDrag + "<br>" + picks,
        "<b>Fill</b><br>Contiguous: the connected area \u00b7 Global: every matching pixel of the cel<br>" + picks + "<br>"
            + "Tolerance: how different a color may be and still count (0: exact; indexed sprites match exact indices)<br>"
            + "Right mouse: secondary color \u00b7 stays inside the selection<br>"
            + "All layers: finds the area in what every visible layer shows, fills the current layer",
        "", "",
        "<b>Line</b><br>Shift: snap to 45\u00b0 steps<br>" + alt + ": from the center<br>Right mouse: secondary color",
        "<b>Rectangle</b><br>Shift: square<br>" + alt + ": from the center<br>Right mouse: secondary color",
        "<b>Ellipse</b><br>Shift: circle<br>" + alt + ": from the center<br>Right mouse: secondary color",
        "<b>Select</b><br>Drag: select \u00b7 " + modes + "<br>"
            + "Pressed during the drag: Shift square \u00b7 " + alt + " from the center<br>"
            + "Drag inside the selection: move its pixels<br>Click: deselect<br>"
            + "Double-click with the grid shown: select a grid cell<br>"
            + selectionKeys,
        "<b>Move</b><br>Drag: move the selected pixels, or the whole cel when nothing is selected<br>"
            + "A paste floats until Enter, a click or the next edit; Esc cancels<br>"
            + "Arrows: nudge the selected pixels (Shift: 10 px) · " + alt + "+arrows: move only the selection",
        "<b>Lasso</b><br>Freehand: drag around what to select; the outline closes by itself \u00b7 hold Alt while dragging for a straight segment<br>"
            + "Polygon: click the corners; click the first one, double-click or Enter to finish<br>"
            + "Backspace: remove the last corner \u00b7 Esc: cancel \u00b7 Shift on later corners: 45\u00b0 steps<br>"
            + modes + "<br>Click: deselect<br>" + selectionKeys,
        "<b>Magic Wand</b><br>Click: select the pixels of that color in the current layer<br>"
            + "Contiguous: the connected area \u00b7 Global: every matching pixel<br>"
            + "Tolerance: how different a color may be and still count<br>"
            + "All layers: looks at what every visible layer shows<br>"
            + modes + "<br>" + selectionKeys,
        "<b>Blur</b><br>Softens the pixels it passes over (each at most once per stroke)<br>"
            + key("brushSmaller") + " and " + key("brushBigger") + ": brush size \u00b7 " + sizeDrag + " \u00b7 stays inside the selection",
        "<b>Jumble</b><br>Shuffles the pixels it passes over with their neighbours, for texture and noise<br>"
            + key("brushSmaller") + " and " + key("brushBigger") + ": brush size \u00b7 " + sizeDrag + " \u00b7 a new pattern each stroke",
        "<b>Text</b><br>Click the canvas, then type in the field: the text floats in the primary color<br>"
            + "Shift+Enter: a new line \u00b7 Drag the text to move it \u00b7 Enter places it \u00b7 Esc drops it \u00b7 a click elsewhere starts a new one \u00b7 a click on the text just placed opens it again",
        "<b>Slice</b><br>Drag on empty canvas: a new slice \u00b7 click a slice: pick it \u00b7 drag inside: move it \u00b7 drag a handle: resize it<br>"
            + "Rename, recolor (the primary color) or delete it in the tool options \u00b7 Delete removes the picked slice<br>"
            + "Slices are named areas: sprite sheet regions, 9-slice panels, hitboxes; they're saved in the .aseprite file",
        "<b>Gradient</b><br>Drag: the primary color at the start, the secondary one at the end; across the selection, or the whole cel<br>"
            + "Linear or radial (around the start) \u00b7 Smooth, or dithered by a pattern: two colors only<br>"
            + "Make the secondary color transparent for a fade \u00b7 stays inside the selection and on an alpha-locked layer's pixels",
        "<b>Source</b><br>Drag out the shape particles are born in: a line, a rectangle or an oval; the emitter goes to its middle<br>"
            + "Shift: a square, a circle or a line in 15\u00b0 steps \u00b7 " + alt + ": from the middle outward<br>"
            + "The handles on the canvas and the Source section of the Particles panel change it afterwards",
        "<b>Field</b><br>Click to place the chosen kind of field there, drag to move it as you click<br>"
            + "Attractor: pulls (or pushes) \u00b7 Vortex: swirls \u00b7 Wind: a push everywhere \u00b7 Floor: a line they can't cross<br>"
            + "Its settings are in the Fields section of the Particles panel",
        "<b>Pose</b><br>Poses the Smear group on this frame: drag inside its box to move it, the round handle to turn it about the pivot (Shift: 15\u00b0 steps), "
            + "a square handle to scale it (Shift: keep proportions; " + alt + " on an edge handle: skew), the ring to move the pivot<br>"
            + "Each change sets the frame's key (made if there is none) \u00b7 the keys are diamonds on its timeline row<br>"
            + "To paint the art, pick a layer inside the group",
    ]
    // What Move does on a Smear group.
    readonly property string moveSmearHint: "<b>Move</b><br>Drag: move the whole Smear group, its art on every frame and its pivot, so the motion moves exactly<br>"
        + "(To move only the art, pick its layer inside the group: the pivot then stays where it is)"
    // Shared by the selection tools' hints.
    readonly property string modes: "Shift held at the start: add \u00b7 " + alt + ": subtract \u00b7 Both: intersect"
    readonly property string selectionKeys: key("selectAll") + " all \u00b7 " + key("deselect") + " none \u00b7 "
            + key("invertSelection") + " invert \u00b7 Delete clears<br>"
            + key("copy") + " / " + key("cut") + " / " + key("paste") + ": copy, cut, paste<br>"
            + key("transform") + ": transform \u00b7 " + key("flipHorizontal") + " / " + key("flipVertical") + ": flip<br>"
            + "Arrows: nudge the pixels (Shift: 10 px) \u00b7 " + alt + "+arrows: move only the selection"

    // Tile mode on a tilemap layer: the pencil / eraser / eyedropper work on
    // whole tiles, so the brush options don't apply.
    readonly property bool tilePlacing: editor.tilesActive
        && (editor.tool === Editor.Pencil || editor.tool === Editor.Eraser || editor.tool === Editor.Eyedropper)
    readonly property bool brushTool: editor.usesBrush(editor.tool) && !tilePlacing
    // Blur / jumble: sizes and round / square, no custom brush or dither.
    readonly property bool softTool: editor.tool === Editor.Blur || editor.tool === Editor.Jumble
    readonly property bool customBrush: editor.brushShape === Editor.CustomBrush
    readonly property bool freehandTool: (editor.tool === Editor.Pencil || editor.tool === Editor.Eraser) && !tilePlacing
    // Pencil, eraser, blur and jumble: the pen dynamics and the stabilizer apply.
    readonly property bool penTool: (freehandTool || softTool) && !transforming
    readonly property bool shapeTool: editor.tool === Editor.Rectangle || editor.tool === Editor.Ellipse
    readonly property bool selectionTool: editor.tool === Editor.Select || editor.tool === Editor.Move
                                          || editor.tool === Editor.Lasso || editor.tool === Editor.MagicWand
    readonly property bool wandTool: editor.tool === Editor.MagicWand
    // A float, but not text being typed (that keeps the text options).
    readonly property bool transforming: editor.floating && !editor.textActive
    readonly property bool fillTool: editor.tool === Editor.Fill
    // Typing a transform size sets the other one too, from the source's proportions.
    property bool keepRatio: false
    function ratioSize(v, fromWidth) {
        const s = root.editor.transformSourceSize
        if (s.width <= 0 || s.height <= 0)
            return fromWidth ? root.editor.transformBox.height : root.editor.transformBox.width
        return Math.max(1, Math.round(fromWidth ? v * s.height / s.width : v * s.width / s.height))
    }
    readonly property bool gradientTool: editor.tool === Editor.Gradient
    readonly property bool textTool: editor.tool === Editor.Text
    readonly property bool sliceTool: editor.tool === Editor.SliceTool
    readonly property bool sourceTool: editor.tool === Editor.SourceTool
    readonly property bool fieldTool: editor.tool === Editor.FieldTool
    readonly property var currentSliceData: editor.allSlices[editor.currentSlice] ?? null
    // The picked slice at this frame (null when it's hidden here).
    readonly property var currentSliceShown: editor.slices.find(s => s.index === editor.currentSlice) ?? null
    readonly property bool drawingTool: (brushTool || fillTool) && !softTool && !transforming
    readonly property bool hasOptions: !suppressed && (brushTool || selectionTool || fillTool || gradientTool || textTool || sliceTool
                                                        || sourceTool || fieldTool
                                                        || tilePlacing || transforming)

    // A slim strip under the title bar, as wide as the window and always
    // there (the canvas doesn't shift as tools come and go); its contents
    // fade in with the tool's options.
    docked: true
    dockEdge: Qt.BottomEdge
    // One more than the controls need: the hairline takes the last row.
    implicitHeight: Theme.buttonSize + padding * 2 + 1
    padding: 4
    visible: !suppressed

    // For screenshots (`--show popup:NAME`): opens one of the strip's popups.
    function showPopup(name: string) {
        const p = ({ dither: ditherPopup, symmetry: symmetryPopup, pen: penPopup, brushes: brushLibrary })[name]
        if (p)
            p.open()
    }

    component Separator: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 18
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        color: Theme.separator
    }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        font.features: { "tnum": 1 }
        verticalAlignment: Text.AlignVCenter
    }

    // A window narrower than the options scrolls them sideways (wheel or
    // drag) instead of cutting them off.
    Flickable {
        id: strip
        anchors.fill: parent
        anchors.bottomMargin: 1
        clip: true
        contentWidth: row.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width

    RowLayout {
        id: row
        // As wide as its contents and left-aligned: given the strip's whole
        // width, a layout would spread the items out.
        height: strip.height
        width: implicitWidth
        spacing: 2

        // History first: always in the same place, at the left of the options.
        IconButton {
            Layout.leftMargin: 2
            iconName: "undo-2"
            tip: "Undo" + Shortcuts.hints.undo
            enabled: root.editor.canUndo
            onClicked: root.editor.undo()
        }
        IconButton {
            iconName: "redo-2"
            tip: "Redo" + Shortcuts.hints.redo
            enabled: root.editor.canRedo
            onClicked: root.editor.redo()
        }
        Separator {}

        // Which tool these options belong to: a label, not a button (no
        // fill, no outline).
        Row {
            Layout.leftMargin: 6
            Layout.rightMargin: 4
            Layout.alignment: Qt.AlignVCenter
            spacing: 7
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 18
                implicitHeight: 18
                iconName: root.tool.icon
                icon.color: Theme.text
                hoverEnabled: false
                background: null
                enabled: false
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.tool.name
                color: Theme.text
                font.pixelSize: Theme.fontSize + 1
                font.weight: Font.DemiBold
            }
        }

        Separator {}

        NumberControl {
            visible: root.brushTool && !root.transforming && !(root.customBrush && root.editor.hasCustomBrush && !root.softTool)
            buttons: true
            value: root.editor.brushSize
            from: 1
            to: 64
            bigStep: 4
            suffix: " px"
            fieldWidth: 50
            tip: "Brush size ([ and ])"
            onMoved: v => root.editor.brushSize = v
        }
        // The custom brush has a scale instead of a size (the same Ctrl+Alt drag sets it).
        NumberControl {
            visible: root.brushTool && !root.transforming && root.customBrush && root.editor.hasCustomBrush && !root.softTool
            buttons: true
            typeable: false
            value: root.editor.customBrushScale
            from: -16
            to: 16
            shownText: root.editor.customBrushScaleText
            fieldWidth: 50
            tip: "Custom brush scale (" + root.editor.customBrushSize.width + " × " + root.editor.customBrushSize.height + ")"
            onMoved: v => root.editor.customBrushScale = v
        }
        IconButton {
            visible: root.brushTool && !root.transforming && root.customBrush && root.editor.hasCustomBrush && !root.softTool
                     && root.editor.customBrushEdited
            implicitWidth: 18
            iconName: "undo-2"
            tip: "Reset the custom brush's scale and flips"
            onClicked: root.editor.resetCustomBrush()
        }
        Separator { visible: root.brushTool && !root.transforming }
        SegmentGroup {
            visible: root.brushTool && !root.transforming
            SegmentButton {
                iconName: "square"
                tip: "Square brush"
                checked: root.editor.brushShape === Editor.SquareBrush
                onClicked: root.editor.brushShape = Editor.SquareBrush
            }
            SegmentButton {
                iconName: "circle"
                tip: "Round brush"
                checked: root.editor.brushShape === Editor.RoundBrush
                onClicked: root.editor.brushShape = Editor.RoundBrush
            }
            // The custom brush, shown as itself.
            SegmentButton {
                visible: root.brushTool && !root.softTool
                enabled: root.editor.hasCustomBrush
                tip: root.editor.hasCustomBrush
                     ? "Custom brush (" + root.editor.customBrushSize.width + " × "
                       + root.editor.customBrushSize.height + ")"
                     : "Custom brush: select pixels and press " + root.key("brushFromSelection")
                checked: root.customBrush
                onClicked: root.editor.brushShape = Editor.CustomBrush
                ImagePreview {
                    anchors.fill: parent
                    anchors.margins: 6
                    visible: root.editor.hasCustomBrush
                    image: root.editor.customBrushImage
                    checkerSize: 3
                }
                Text {
                    anchors.centerIn: parent
                    visible: !root.editor.hasCustomBrush
                    text: "?"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSize
                }
            }
        }
        IconButton {
            id: brushLibraryButton
            visible: root.brushTool && !root.softTool && !root.transforming
            implicitWidth: 18
            iconName: "chevron-down"
            tip: "Saved brushes"
            active: brushLibrary.visible
            onClicked: if (!brushLibrary.visible && Date.now() - brushLibrary.closedAt > 250) brushLibrary.open()

            BrushLibraryPopup {
                id: brushLibrary
                editor: root.editor
                canvas: root.canvas
                y: brushLibraryButton.height + 8
                x: Math.round(brushLibraryButton.width / 2 - width / 2)
            }
        }
        // One angle for every brush (the round one looks the same at any angle).
        NumberControl {
            visible: root.brushTool && !root.transforming
            Layout.leftMargin: 4
            value: root.editor.brushAngle
            from: -180
            to: 180
            step: 1
            bigStep: 15
            suffix: "\u00b0"
            fieldWidth: 50
            tip: "Brush angle: turns the square and custom brushes (Shift: 15\u00b0 steps)"
            onMoved: v => root.editor.brushAngle = v
        }
        IconButton {
            id: flipButton
            visible: root.brushTool && root.customBrush && root.editor.hasCustomBrush && !root.softTool && !root.transforming
            implicitWidth: 18
            iconName: "flip-horizontal-2"
            tip: "Flip the custom brush"
            active: flipMenu.visible
            onClicked: if (!flipMenu.visible && Date.now() - flipMenu.closedAt > 250) flipMenu.popup(flipButton, 0, flipButton.height + 8)

            AppMenu {
                id: flipMenu
                property double closedAt: 0
                onClosed: closedAt = Date.now()
                AppMenuItem { text: "Flip horizontally"; onTriggered: root.editor.flipCustomBrush(true) }
                AppMenuItem { text: "Flip vertically"; onTriggered: root.editor.flipCustomBrush(false) }
            }
        }
        Segmented {
            visible: root.brushTool && root.customBrush && !root.softTool && !root.transforming
                     && root.editor.tool !== Editor.Eraser && !(root.editor.shading && root.editor.tool === Editor.Pencil)
            Layout.leftMargin: 4
            model: ["Brush colors", "Color"]
            currentIndex: root.editor.brushColors ? 0 : 1
            onActivated: index => root.editor.brushColors = index === 0
        }
        OptionPill {
            visible: root.brushTool && root.customBrush && !root.softTool && root.editor.brushColors && !root.transforming
                     && root.editor.tool !== Editor.Eraser && !(root.editor.shading && root.editor.tool === Editor.Pencil)
            Layout.leftMargin: 4
            text: "Blend"
            tip: "Lay the brush's semi-transparent pixels over what is there instead of replacing it (RGBA sprites)"
            checked: root.editor.brushBlend
            onToggled: root.editor.brushBlend = !root.editor.brushBlend
        }
        OptionPill {
            visible: root.freehandTool && !root.transforming && !root.customBrush
            Layout.leftMargin: 4
            text: "Pixel perfect"
            tip: root.editor.brushSize === 1 && !root.customBrush ? "Removes the extra corner pixels of 1px strokes"
                                                                  : "Only for 1px brushes"
            checked: root.editor.pixelPerfect && root.editor.brushSize === 1 && !root.customBrush
            enabled: root.editor.brushSize === 1 && !root.customBrush
            opacity: enabled ? 1 : 0.5
            onToggled: root.editor.pixelPerfect = !root.editor.pixelPerfect
        }
        OptionPill {
            visible: (root.editor.tool === Editor.Pencil || root.editor.tool === Editor.Line
                      || (root.shapeTool && !root.editor.fillShapes)) && !root.tilePlacing && !root.transforming
            Layout.leftMargin: 4
            text: "Shading"
            tip: root.editor.shadeRange.x < 0 ? "Shade along the whole palette"
                                              : "Shade along palette colors " + (root.editor.shadeRange.x + 1)
                                                + "\u2013" + (root.editor.shadeRange.y + 1)
            checked: root.editor.shading
            onToggled: root.editor.shading = !root.editor.shading
        }
        OptionPill {
            visible: root.shapeTool && !root.transforming
            Layout.leftMargin: 4
            text: "Filled"
            tip: "Draw filled shapes"
            checked: root.editor.fillShapes
            onToggled: root.editor.fillShapes = !root.editor.fillShapes
        }
        // Fill: area and tolerance.
        Segmented {
            visible: root.fillTool && !root.transforming
            Layout.leftMargin: 4
            model: ["Contiguous", "Global"]
            currentIndex: root.editor.fillContiguous ? 0 : 1
            onActivated: index => root.editor.fillContiguous = index === 0
        }
        Label {
            visible: root.fillTool && !root.transforming
            Layout.leftMargin: 8
            text: "Tolerance"
        }
        NumberControl {
            visible: root.fillTool && !root.transforming
            value: root.editor.fillTolerance
            from: 0
            to: 255
            bigStep: 16
            fieldWidth: 44
            tip: "Fill tolerance (0: exact color)"
            onMoved: v => root.editor.fillTolerance = v
        }
        // Gradient: its shape and how it steps from one color to the other.
        AppComboBox {
            visible: root.gradientTool && !root.transforming
            Layout.leftMargin: 4
            implicitWidth: 110
            model: ["Linear", "Radial", "Reflected", "Diamond", "Angle"]
            currentIndex: root.editor.gradientShape
            onActivated: index => root.editor.gradientShape = index
        }
        OptionPill {
            visible: root.gradientTool && !root.transforming
            Layout.leftMargin: 4
            text: "Palette"
            tip: "Run through the palette's selected range (Shift-click swatches; the whole palette without one) instead of from the primary to the secondary color. Indexed sprites keep palette colors"
            checked: root.editor.gradientRamp
            onToggled: root.editor.gradientRamp = !root.editor.gradientRamp
        }
        AppComboBox {
            visible: root.gradientTool && !root.transforming
            Layout.leftMargin: 4
            implicitWidth: 130
            model: ["Smooth", "Dither 2 \u00d7 2", "Dither 4 \u00d7 4", "Dither 8 \u00d7 8", "Blue noise"]
            currentIndex: root.editor.gradientDither
            onActivated: index => root.editor.gradientDither = index
        }
        AppComboBox {
            visible: root.gradientTool && !root.transforming
            Layout.leftMargin: 4
            implicitWidth: 110
            model: ["No repeat", "Repeat", "Mirror"]
            currentIndex: root.editor.gradientRepeat
            onActivated: index => root.editor.gradientRepeat = index
        }
        OptionPill {
            visible: root.gradientTool && !root.transforming
            Layout.leftMargin: 4
            text: "Own stops"
            tip: "Colors of your own, anywhere along the gradient, instead of the primary and secondary colors or the palette"
            checked: root.editor.gradientCustom
            onToggled: root.editor.gradientCustom = !root.editor.gradientCustom
        }
        GradientStopsBar {
            visible: root.gradientTool && !root.transforming && root.editor.gradientCustom
            Layout.leftMargin: 4
            editor: root.editor
        }
        Label {
            visible: root.gradientTool && !root.transforming
            Layout.leftMargin: 6
            text: root.editor.gradientEditable ? "Drag its ends to change it; Enter keeps it, Esc takes it back"
                : root.editor.gradientCustom ? "Drag across the sprite"
                : root.editor.gradientRamp ? "Drag from the first color of the range to the last"
                                           : "Drag from the primary color to the secondary"
        }
        Segmented {
            visible: root.fillTool && !root.transforming
            Layout.leftMargin: 8
            model: ["This layer", "All layers"]
            currentIndex: root.editor.sampleMerged ? 1 : 0
            onActivated: index => root.editor.sampleMerged = index === 1
        }

        // Pixel mode on a tilemap layer: what painting does to the tiles.
        Segmented {
            visible: root.editor.tilemapLayer && !root.editor.editTiles && root.brushTool && !root.transforming
            Layout.leftMargin: 6
            model: ["Manual", "Auto", "Stack"]
            currentIndex: root.editor.tileEditMode
            onActivated: index => root.editor.tileEditMode = index
            HoverHandler { id: tileModeHover }
            AppToolTip {
                text: "Manual: edits only existing tiles, wherever they're used \u00b7 Auto: edits tiles in place, "
                      + "and an empty cell gets a matching tile or a new one \u00b7 Stack: each painted cell gets a new tile"
                active: tileModeHover.hovered
            }
        }

        // Tile mode: what the tool does with tiles.
        Label {
            visible: root.tilePlacing && !root.transforming
            Layout.leftMargin: 6
            text: root.editor.tool === Editor.Select ? "Tile mode: a selection is whole cells \u00b7 drag inside it to move them \u00b7 arrows move them a cell (Shift: ten; Alt: only the selection)"
                  : root.editor.tool === Editor.Move ? "Tile mode: drag the selected cells \u00b7 Alt: copy them \u00b7 arrows move them a cell (Alt: only the selection)"
                  : root.editor.tool === Editor.Eraser ? "Tile mode: clears tiles (the empty tile)"
                  : root.editor.tool === Editor.Eyedropper ? "Tile mode: picks the tile under the cursor"
                  : root.editor.tool === Editor.Fill ? "Tile mode: fills cells showing the same tile (inside the selection, if there is one)"
                  : root.editor.tool === Editor.Line || root.editor.tool === Editor.Rectangle || root.editor.tool === Editor.Ellipse
                    ? "Tile mode: draws with the picked tile \u00b7 right-click clears \u00b7 " + root.key("tileMode") + " for pixels"
                  : "Tile mode: places the picked tile \u00b7 right-click clears \u00b7 Shift-click tiles in the tileset for a block \u00b7 " + root.key("tileMode") + " for pixels"
        }
        // Freehand tiles keep diagonals one cell thin, like the pencil's 1 px strokes.
        OptionPill {
            visible: root.tilePlacing && (root.editor.tool === Editor.Pencil || root.editor.tool === Editor.Eraser)
                     && !root.transforming
            Layout.leftMargin: 4
            text: "Pixel perfect"
            tip: "Removes the extra corner cells where a freehand drag steps diagonally"
            checked: root.editor.pixelPerfect
            onToggled: root.editor.pixelPerfect = !root.editor.pixelPerfect
        }

        // Slice: pick one from the list, rename, recolor, delete.
        AppComboBox {
            visible: root.sliceTool && !root.transforming && root.editor.allSlices.length > 0
            Layout.leftMargin: 4
            implicitWidth: 130
            model: root.editor.allSlices.map(s => s.name)
            currentIndex: root.editor.currentSlice
            displayText: root.currentSliceData ? root.currentSliceData.name : "Pick a slice\u2026"
            onActivated: index => root.editor.currentSlice = index
        }
        AppTextField {
            id: sliceName
            visible: root.sliceTool && !root.transforming && root.currentSliceData !== null
            Layout.leftMargin: 4
            implicitWidth: 120
            text: root.currentSliceData ? root.currentSliceData.name : ""
            onEditingFinished: if (root.currentSliceData && text !== root.currentSliceData.name)
                                   root.editor.setSliceName(root.editor.currentSlice, text)
        }
        Rectangle {
            visible: root.sliceTool && !root.transforming && root.currentSliceData !== null
            Layout.leftMargin: 4
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            radius: 4
            color: root.currentSliceData ? root.currentSliceData.color : "transparent"
            border.color: Theme.swatchBorder
        }
        IconButton {
            visible: root.sliceTool && !root.transforming && root.currentSliceData !== null
            iconName: "pipette"
            tip: "Use the primary color for this slice"
            onClicked: root.editor.setSliceColor(root.editor.currentSlice, root.editor.primaryColor)
        }
        Separator { visible: root.sliceTool && !root.transforming && root.currentSliceData !== null }
        OptionPill {
            visible: root.sliceTool && !root.transforming && root.currentSliceData !== null
            text: "9-slice"
            tip: "A center that stretches while the corners stay (drag its lines on the canvas)"
            checked: root.currentSliceData ? root.currentSliceData.nineSlice : false
            onToggled: root.editor.setSliceNineSlice(root.editor.currentSlice, !checked)
        }
        OptionPill {
            visible: root.sliceTool && !root.transforming && root.currentSliceData !== null
            text: "Pivot"
            tip: "A point of the slice, e.g. where a character stands (drag it on the canvas)"
            checked: root.currentSliceData ? root.currentSliceData.hasPivot : false
            onToggled: root.editor.setSliceHasPivot(root.editor.currentSlice, !checked)
        }
        Separator { visible: root.sliceTool && !root.transforming }
        OptionPill {
            visible: root.sliceTool && !root.transforming
            text: "Key per frame"
            tip: "On: an edit on this frame gives the slice its own key here, so it can change over the animation"
            checked: root.editor.sliceKeys
            onToggled: root.editor.sliceKeys = !root.editor.sliceKeys
        }
        AppButton {
            visible: root.sliceTool && !root.transforming && root.currentSliceShown !== null
                     && root.currentSliceShown.keyFrame === root.editor.currentFrame && root.currentSliceShown.keyFrame > 0
            text: "Remove key"
            tip: "This frame's own key goes; the key before it applies again"
            onClicked: root.editor.removeSliceKey(root.editor.currentSlice)
        }
        Separator { visible: root.sliceTool && !root.transforming && root.currentSliceData !== null }
        IconButton {
            visible: root.sliceTool && !root.transforming && root.currentSliceData !== null
            iconName: "trash-2"
            tip: "Delete this slice (Delete)"
            onClicked: root.editor.removeSlice(root.editor.currentSlice)
        }
        Label {
            visible: root.sliceTool && !root.transforming && root.currentSliceData === null
            Layout.leftMargin: 6
            text: "Drag on the canvas to make a slice; click one to pick it"
        }

        // Text: the font, spacing, and the text being typed.
        AppComboBox {
            visible: root.textTool && !root.transforming
            Layout.leftMargin: 4
            implicitWidth: 90
            model: root.editor.fonts
            currentIndex: root.editor.textFont
            onActivated: index => root.editor.textFont = index
        }
        Label {
            visible: root.textTool && !root.transforming
            Layout.leftMargin: 6
            text: "Spacing"
        }
        NumberControl {
            visible: root.textTool && !root.transforming
            value: root.editor.textLetterSpacing
            from: -2
            to: 16
            fieldWidth: 40
            suffix: " px"
            tip: "Space between letters"
            onMoved: v => root.editor.textLetterSpacing = v
        }
        NumberControl {
            visible: root.textTool && !root.transforming
            value: root.editor.textLineSpacing
            from: -2
            to: 16
            fieldWidth: 40
            suffix: " px"
            tip: "Space between lines (Shift+Enter starts a line)"
            onMoved: v => root.editor.textLineSpacing = v
        }
        Segmented {
            visible: root.textTool && !root.transforming
            Layout.leftMargin: 4
            model: ["Left", "Center", "Right"]
            currentIndex: root.editor.textAlign
            onActivated: index => root.editor.textAlign = index
        }
        // What to do next; the text itself shows only on the canvas.
        Label {
            visible: root.textTool && !root.transforming
            Layout.leftMargin: 6
            text: root.editor.textActive ? "Type on the canvas \u00b7 Shift+Enter: new line \u00b7 Enter: place \u00b7 Esc: drop"
                                         : "Click the canvas to start typing"
        }
        // Takes the typing (keys, Backspace, Shift+Enter, paste) but isn't
        // seen: visible items only, so it's there with no size and no ink.
        AppTextArea {
            id: textField
            visible: root.textTool && !root.transforming
            opacity: 0
            Layout.preferredWidth: 0
            Layout.maximumHeight: 0
            enabled: root.editor.textActive
            onTextChanged: if (root.editor.textActive) root.editor.setText(text)
            onCursorPositionChanged: root.editor.textCursor = cursorPosition
            Keys.onReturnPressed: event => {
                event.accepted = !(event.modifiers & Qt.ShiftModifier)
                if (event.accepted)
                    root.editor.commitFloating()
            }
            Keys.onEnterPressed: event => {
                event.accepted = !(event.modifiers & Qt.ShiftModifier)
                if (event.accepted)
                    root.editor.commitFloating()
            }
            Keys.onEscapePressed: root.editor.cancelFloating()
            Connections {
                target: root.editor
                function onTextStarted() {
                    textField.text = root.editor.text
                    textField.forceActiveFocus()
                }
                function onTextChanged() {
                    if (!root.editor.textActive)
                        textField.text = ""
                }
            }
        }

        // Particle tools: which shape to drag out, which field to place.
        Segmented {
            visible: root.sourceTool
            Layout.leftMargin: 4
            model: ["Line", "Rectangle", "Oval"]
            currentIndex: root.editor.liveSourceKind - 1
            onActivated: index => root.editor.liveSourceKind = index + 1
        }
        Segmented {
            visible: root.fieldTool
            Layout.leftMargin: 4
            model: ["Attractor", "Vortex", "Wind", "Floor", "Collider"]
            currentIndex: root.editor.liveFieldKind
            onActivated: index => root.editor.liveFieldKind = index
        }

        // Lasso: drag an outline, or click its corners.
        Segmented {
            visible: root.editor.tool === Editor.Lasso && !root.transforming
            Layout.leftMargin: 4
            model: ["Freehand", "Polygon"]
            currentIndex: root.editor.lassoPolygon ? 1 : 0
            onActivated: index => root.editor.lassoPolygon = index === 1
        }

        // Magic wand: what counts as the same color.
        Segmented {
            visible: root.wandTool && !root.transforming
            Layout.leftMargin: 4
            model: ["Contiguous", "Global"]
            currentIndex: root.editor.wandContiguous ? 0 : 1
            onActivated: index => root.editor.wandContiguous = index === 0
        }
        Label {
            visible: root.wandTool && !root.transforming
            Layout.leftMargin: 8
            text: "Tolerance"
        }
        NumberControl {
            visible: root.wandTool && !root.transforming
            value: root.editor.wandTolerance
            from: 0
            to: 255
            bigStep: 16
            fieldWidth: 44
            tip: "Magic wand tolerance (0: exact color)"
            onMoved: v => root.editor.wandTolerance = v
        }
        Segmented {
            visible: root.wandTool && !root.transforming
            Layout.leftMargin: 8
            model: ["This layer", "All layers"]
            currentIndex: root.editor.sampleMerged ? 1 : 0
            onActivated: index => root.editor.sampleMerged = index === 1
        }
        Separator { visible: root.wandTool && !root.transforming }

        // What the selection tools are working on.
        Label {
            visible: root.selectionTool && !root.transforming
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            text: root.editor.floating ? "Pasted: Enter places it, Esc cancels"
                : root.editor.hasSelection
                  ? root.editor.selection.width + " \u00d7 " + root.editor.selection.height + " selected"
                : root.editor.tool === Editor.Move ? "No selection: moves the whole cel"
                : "No selection"
        }
        IconButton {
            visible: root.selectionTool && !root.transforming
            iconName: "scaling"
            tip: "Transform" + Shortcuts.hints.transform + ": scale and rotate the selection, or the layer's pixels"
            onClicked: root.editor.startTransform()
        }
        IconButton {
            visible: root.selectionTool && !root.transforming
            iconName: "brush"
            tip: "Brush from selection" + Shortcuts.hints.brushFromSelection + ": the selected pixels, or the whole layer, become the brush"
            onClicked: root.editor.brushFromSelection()
        }
        OptionPill {
            visible: root.selectionTool && !root.transforming
            indicator: false
            text: "Invert"
            tip: "Invert the selection" + Shortcuts.hints.invertSelection
            onToggled: root.editor.invertSelection()
        }
        Separator { visible: root.selectionTool && root.editor.hasSelection && !root.transforming }
        IconButton {
            visible: root.selectionTool && root.editor.hasSelection && !root.transforming
            iconName: "trash-2"
            tip: "Clear selected pixels (Delete)"
            onClicked: root.editor.deleteSelection()
        }

        // Floating pixels (paste / transform): flips, turns, size, angle.
        IconButton {
            visible: root.transforming
            iconName: "flip-horizontal-2"
            tip: "Flip horizontally" + Shortcuts.hints.flipHorizontal
            onClicked: root.editor.flipHorizontal()
        }
        IconButton {
            visible: root.transforming
            iconName: "flip-vertical-2"
            tip: "Flip vertically" + Shortcuts.hints.flipVertical
            onClicked: root.editor.flipVertical()
        }
        IconButton {
            visible: root.transforming
            iconName: "rotate-ccw"
            tip: "Rotate 90\u00b0 counterclockwise" + Shortcuts.hints.rotateCounterclockwise
            onClicked: root.editor.rotate90(false)
        }
        IconButton {
            visible: root.transforming
            iconName: "rotate-cw"
            tip: "Rotate 90\u00b0 clockwise" + Shortcuts.hints.rotateClockwise
            onClicked: root.editor.rotate90(true)
        }
        // Size and angle by number; they keep the pivot (picked on the
        // right) in place.
        NumberControl {
            visible: root.transforming
            Layout.leftMargin: 8
            value: root.editor.transformBox.width
            from: 1
            to: 4096
            suffix: " px"
            tip: "Width"
            onMoved: v => root.editor.resizeTransform(v, root.keepRatio ? root.ratioSize(v, true) : root.editor.transformBox.height)
        }
        Label {
            visible: root.transforming
            text: "\u00d7"
        }
        NumberControl {
            visible: root.transforming
            value: root.editor.transformBox.height
            from: 1
            to: 4096
            suffix: " px"
            tip: "Height"
            onMoved: v => root.editor.resizeTransform(root.keepRatio ? root.ratioSize(v, false) : root.editor.transformBox.width, v)
        }
        OptionPill {
            visible: root.transforming
            Layout.leftMargin: 4
            text: "Proportions"
            tip: "Typing one size sets the other to keep the picture's proportions"
            checked: root.keepRatio
            onToggled: root.keepRatio = !root.keepRatio
        }
        Label {
            visible: root.transforming
            Layout.leftMargin: 4
            text: {
                const s = root.editor.transformSourceSize
                return s.width > 0 ? Math.round(root.editor.transformBox.width / s.width * 100) + "%" : ""
            }
        }
        NumberControl {
            visible: root.transforming
            Layout.leftMargin: 4
            value: Math.round(root.editor.transformAngle)
            from: -180
            to: 180
            step: 1
            bigStep: 15
            suffix: "\u00b0"
            tip: "Angle (Shift: 15\u00b0 steps)"
            onMoved: v => root.editor.setTransformAngle(v)
        }
        // The pivot: one of nine points of the box.
        Item {
            visible: root.transforming
            Layout.leftMargin: 4
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            Grid {
                anchors.centerIn: parent
                columns: 3
                spacing: 2
                Repeater {
                    model: 9
                    delegate: Rectangle {
                        id: pivotDot
                        required property int index
                        readonly property bool current: index === root.editor.transformPivot
                        width: 6
                        height: 6
                        radius: 1.5
                        color: current ? Theme.accent : pivotArea.containsMouse ? Theme.textDim : Theme.textFaint
                        MouseArea {
                            id: pivotArea
                            anchors.fill: parent
                            anchors.margins: -1
                            hoverEnabled: true
                            onClicked: root.editor.transformPivot = pivotDot.index
                        }
                    }
                }
            }
            AppToolTip {
                text: "Pivot: what stays put when rotating or typing a size"
                active: pivotHover.hovered
            }
            HoverHandler { id: pivotHover }
        }
        IconButton {
            visible: root.transforming
            Layout.leftMargin: 6
            iconName: "x"
            tip: root.editor.floatingLifted ? "Cancel (puts the pixels back)" : "Cancel (Esc)"
            onClicked: root.editor.cancelFloating()
        }
        IconButton {
            visible: root.transforming
            iconName: "check"
            active: true
            tip: "Apply (Enter)"
            onClicked: root.editor.commitFloating()
        }

        // Dithering, for every tool that draws; its settings in a popup.
        Separator { visible: root.drawingTool }
        OptionPill {
            visible: root.drawingTool
            text: "Dither"
            tip: "Paint through a pattern anchored to the canvas"
            checked: root.editor.dither
            onToggled: root.editor.dither = !root.editor.dither
        }
        IconButton {
            id: ditherButton
            visible: root.drawingTool
            implicitWidth: 22
            iconName: "chevron-down"
            tip: "Dither pattern and options"
            active: ditherPopup.visible
            onClicked: if (!ditherPopup.visible && Date.now() - ditherPopup.closedAt > 250) ditherPopup.open()

            DitherPopup {
                id: ditherPopup
                editor: root.editor
                y: ditherButton.height + 8
                x: ditherButton.width / 2 - width / 2
            }
        }

        // Symmetry, for every tool that draws.
        Separator { visible: root.drawingTool }
        IconButton {
            id: symmetryButton
            readonly property bool anyAxis: root.editor.symmetryHorizontal || root.editor.symmetryVertical
                                            || root.editor.symmetryRising || root.editor.symmetryFalling
                                            || root.editor.symmetryRadial >= 2
            visible: root.drawingTool
            iconName: "flip-horizontal-2"
            tip: "Symmetry: mirror axes and where they cross"
            active: anyAxis || symmetryPopup.visible
            onClicked: if (!symmetryPopup.visible && Date.now() - symmetryPopup.closedAt > 250) symmetryPopup.open()

            SymmetryPopup {
                id: symmetryPopup
                editor: root.editor
                y: symmetryButton.height + 8
                x: symmetryButton.width / 2 - width / 2
            }
        }

        // Pen dynamics and the stabilizer, for the freehand tools; in a popup (next to symmetry: both are icons that open one).
        IconButton {
            id: penButton
            visible: root.penTool
            iconName: "pen-tool"
            tip: "Pen pressure, tilt and the stroke stabilizer"
            active: penPopup.visible || root.canvas.stabilizer > 0
            onClicked: if (!penPopup.visible && Date.now() - penPopup.closedAt > 250) penPopup.open()

            PenPopup {
                id: penPopup
                editor: root.editor
                canvas: root.canvas
                y: penButton.height + 8
                x: penButton.width / 2 - width / 2
            }
        }

        Separator {}

        // Modifiers and gestures of the current tool.
        IconButton {
            iconName: "info"
            tip: root.transforming
                 ? "<b>Transform</b><br>Drag inside: move<br>Square handles: scale \u00b7 Shift keeps proportions<br>"
                   + "Round handle: rotate \u00b7 Shift: 15\u00b0 steps<br>Enter applies"
                   + (root.editor.floatingLifted ? " \u00b7 Esc applies too (the X button cancels)" : " \u00b7 Esc cancels")
                 : root.editor.tool === Editor.Move && (root.editor.smearState.current ?? false) ? root.moveSmearHint
                 : root.hints[root.editor.tool] ?? ""
            visible: tip !== ""
        }
    }
    }
}
