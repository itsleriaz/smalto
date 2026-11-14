pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// Tools in groups (draw, shapes, select/move, view) separated by thin
// lines. One column when it fits in `availableHeight`; otherwise as many
// columns as it takes, tools wrapping within their group.
Panel {
    id: root

    required property Editor editor
    // Height the strip may use; set by the window.
    property real availableHeight: 10000
    // The columns it starts with (1-4); a short window still adds more. The
    // handle on its right edge asks the window to change this.
    property int preferredColumns: 1
    signal columnsRequested(int columns)

    // A live layer swaps the strip for its own tools: the emitter (Move), the source, the fields, and
    // the two that don't change pixels.
    readonly property var liveGroups: [
        [
            { tool: Editor.Move, icon: "crosshair", name: "Emitter", key: "move" },
            { tool: Editor.SourceTool, icon: "shapes", name: "Source", key: "sourceTool" },
            { tool: Editor.FieldTool, icon: "magnet", name: "Field", key: "fieldTool" },
            { tool: Editor.Eyedropper, icon: "pipette", name: "Eyedropper", key: "eyedropper" },
        ],
        [
            { tool: Editor.Hand, icon: "hand", name: "Hand", key: "hand", extra: "hold Space" },
        ],
    ]
    // A Smear group: posing it, moving all of it, and the two that don't change pixels.
    readonly property var smearGroups: [
        [
            { tool: Editor.PoseTool, icon: "scaling", name: "Pose", key: "" },
            { tool: Editor.Move, icon: "move", name: "Move", key: "move" },
            { tool: Editor.Eyedropper, icon: "pipette", name: "Eyedropper", key: "eyedropper" },
        ],
        [
            { tool: Editor.Hand, icon: "hand", name: "Hand", key: "hand", extra: "hold Space" },
        ],
    ]
    readonly property var drawGroups: [
        [
            { tool: Editor.Pencil, icon: "pencil", name: "Pencil", key: "pencil" },
            { tool: Editor.Eraser, icon: "eraser", name: "Eraser", key: "eraser" },
            { tool: Editor.Fill, icon: "paint-bucket", name: "Fill", key: "fill" },
            { tool: Editor.Gradient, icon: "gradient", name: "Gradient", key: "gradient" },
            { tool: Editor.Eyedropper, icon: "pipette", name: "Eyedropper", key: "eyedropper" },
        ],
        [
            { tool: Editor.Blur, icon: "droplet", name: "Blur", key: "blurTool" },
            { tool: Editor.Jumble, icon: "shuffle", name: "Jumble", key: "jumbleTool" },
        ],
        [
            { tool: Editor.Line, icon: "slash", name: "Line", key: "line" },
            { tool: Editor.Rectangle, icon: "square", name: "Rectangle", key: "rectangle" },
            { tool: Editor.Ellipse, icon: "circle", name: "Ellipse", key: "ellipse" },
            { tool: Editor.Text, icon: "type", name: "Text", key: "text" },
        ],
        [
            { tool: Editor.Select, icon: "square-dashed", name: "Select", key: "select" },
            { tool: Editor.Lasso, icon: "lasso", name: "Lasso", key: "lasso" },
            { tool: Editor.MagicWand, icon: "wand-sparkles", name: "Magic Wand", key: "magicWand" },
            { tool: Editor.Move, icon: "move", name: "Move", key: "move" },
            { tool: Editor.SliceTool, icon: "slice", name: "Slice", key: "slice" },
        ],
        [
            { tool: Editor.Hand, icon: "hand", name: "Hand", key: "hand", extra: "hold Space" },
        ],
    ]

    readonly property var groups: editor.liveLayer ? liveGroups : (editor.smearState.current ?? false) ? smearGroups : drawGroups

    readonly property int gap: 2
    readonly property int separatorHeight: 13

    function heightFor(columns) {
        let h = padding * 2
        for (let i = 0; i < groups.length; ++i) {
            const rows = Math.ceil(groups[i].length / columns)
            h += rows * Theme.buttonSize + (rows - 1) * gap
            if (i > 0)
                h += separatorHeight + 2 * gap
        }
        return h
    }
    readonly property int columns: {
        for (let c = Math.max(1, Math.min(4, preferredColumns)); c < 4; ++c)
            if (heightFor(c) <= availableHeight)
                return c
        return 4
    }

    implicitWidth: columns * Theme.buttonSize + (columns - 1) * gap + padding * 2
    implicitHeight: heightFor(columns)
    padding: 6

    Behavior on implicitWidth { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

    Column {
        spacing: root.gap

        Repeater {
            model: root.groups

            delegate: Column {
                id: group
                required property var modelData
                required property int index
                spacing: root.gap

                Item {
                    visible: group.index > 0
                    width: root.columns * Theme.buttonSize + (root.columns - 1) * root.gap
                    height: root.separatorHeight
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width - 12
                        height: 1
                        color: Theme.separator
                    }
                }

                Grid {
                    columns: root.columns
                    spacing: root.gap

                    Repeater {
                        model: group.modelData
                        delegate: IconButton {
                            id: toolButton
                            required property var modelData
                            iconName: modelData.icon
                            // "Pencil (B)"; the hand adds "or hold Space".
                            tip: {
                                const k = Shortcuts.labels[modelData.key] ?? ""
                                const more = modelData.extra ?? ""
                                if (!toolButton.enabled)
                                    return modelData.name + " (not on a live layer)"
                                return k === "" && more === "" ? modelData.name
                                     : modelData.name + " (" + (k !== "" && more !== "" ? k + ", or " + more : k + more) + ")"
                            }
                            active: root.editor.tool === modelData.tool
                            // A live layer has its own strip, so everything shown works.
                            enabled: true
                            onClicked: root.editor.tool = modelData.tool
                            // The one chosen tool is solid: the same "on" as an option pill.
                            icon.color: active ? Theme.textOnAccent : hovered ? Theme.text : Theme.textDim
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: toolButton.active ? Theme.accent
                                     : toolButton.down ? Theme.pressed
                                     : toolButton.hovered ? Theme.hover : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }
                        }
                    }
                }
            }
        }
    }

    // Drag the right edge to choose how many columns the strip starts with.
    Item {
        id: grip
        parent: root
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 10
        z: 2

        readonly property bool shown: hover.hovered || drag.active
        Rectangle {
            anchors.centerIn: parent
            width: 3
            height: 26
            radius: 1.5
            color: grip.shown ? Theme.accent : Theme.textFaint
            opacity: grip.shown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        HoverHandler {
            id: hover
            cursorShape: Qt.SizeHorCursor
        }
        DragHandler {
            id: drag
            target: null
            property int startColumns
            onActiveChanged: if (active) startColumns = root.columns
            onCentroidChanged: if (active) {
                const dx = centroid.scenePosition.x - centroid.scenePressPosition.x
                const next = startColumns + Math.round(dx / (Theme.buttonSize + root.gap))
                root.columnsRequested(Math.max(1, Math.min(4, next)))
            }
        }
        AppToolTip {
            text: qsTr("Drag to change how many columns the tools start in")
            active: hover.hovered && !drag.active
        }
    }
}
