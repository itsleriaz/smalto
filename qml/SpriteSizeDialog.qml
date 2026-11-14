pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Sprite size: scales every cel to a new width and height (in pixels or
// percent), nearest-neighbor. A floating window: the new size is outlined over
// the canvas and shown as a thumbnail while you type, nothing changes until
// Resize. The aspect lock is kept while the app runs.
FloatingDialog {
    id: root

    required property Editor editor

    // The thumbnail of the scaled frame.
    property string previewSource: ""
    onSpriteWidthChanged: Qt.callLater(refreshPreview)
    onSpriteHeightChanged: Qt.callLater(refreshPreview)
    function refreshPreview() {
        previewSource = visible && valid ? editor.spriteSizePreview(spriteWidth, spriteHeight, 240, method) : ""
    }
    // Another edit ends the dialog's picture of the sprite.
    Connections {
        target: root.editor
        function onCanvasSizeChanged() { if (root.visible) root.close() }
    }

    property int originalWidth: 1
    property int originalHeight: 1
    property int spriteWidth: 1
    property int spriteHeight: 1
    property bool keepRatio: true
    readonly property var methods: ["nearest", "smooth", "bilinear"]
    readonly property string method: methods[methodChoice.currentIndex]
    onMethodChanged: Qt.callLater(refreshPreview)
    readonly property bool valid: spriteWidth >= 1 && spriteWidth <= 4096 && spriteHeight >= 1 && spriteHeight <= 4096

    function clampSize(v) { return Math.max(1, Math.min(4096, Math.round(v))) }

    property int widthPct: 100
    property int heightPct: 100

    // Sets the size in pixels; the percentages follow.
    function setSize(w, h) {
        spriteWidth = clampSize(w)
        spriteHeight = clampSize(h)
        widthPct = Math.round(spriteWidth * 100 / originalWidth)
        heightPct = Math.round(spriteHeight * 100 / originalHeight)
    }

    // Sets the scale; the pixels follow.
    function setScale(pw, ph) {
        widthPct = pw
        heightPct = ph
        spriteWidth = clampSize(originalWidth * pw / 100)
        spriteHeight = clampSize(originalHeight * ph / 100)
    }

    function apply() {
        if (!valid)
            return
        editor.resizeSprite(spriteWidth, spriteHeight, method)
        close()
    }

    onOpened: {
        originalWidth = editor.canvasWidth
        originalHeight = editor.canvasHeight
        setSize(originalWidth, originalHeight)
        refreshPreview()
    }

    dragHeight: title.height + 8

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    component Unit: Text {
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
    }

    contentItem: ColumnLayout {
        spacing: 16

        Text {
            id: title
            text: qsTr("Sprite Size")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        GridLayout {
            columns: 3
            columnSpacing: 14
            rowSpacing: 12

            Label { text: qsTr("Size") }
            RowLayout {
                spacing: 6
                NumberControl {
                    fieldWidth: 72
                    tabStop: true
                    onAccepted: root.apply()
                    from: 1
                    to: 4096
                    bigStep: 10
                    value: root.spriteWidth
                    tip: qsTr("Width")
                    onMoved: v => root.setSize(v, root.keepRatio ? v * root.originalHeight / root.originalWidth
                                                                 : root.spriteHeight)
                }
                Text {
                    text: "×"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize
                }
                NumberControl {
                    fieldWidth: 72
                    tabStop: true
                    onAccepted: root.apply()
                    from: 1
                    to: 4096
                    bigStep: 10
                    value: root.spriteHeight
                    tip: qsTr("Height")
                    onMoved: v => root.setSize(root.keepRatio ? v * root.originalWidth / root.originalHeight
                                                              : root.spriteWidth, v)
                }
                Unit { text: qsTr("px") }
            }
            // Spans both rows: the lock ties pixels and percent alike.
            IconButton {
                Layout.rowSpan: 2
                iconName: root.keepRatio ? "lock" : "lock-open"
                active: root.keepRatio
                tip: qsTr("Keep aspect ratio")
                onClicked: {
                    root.keepRatio = !root.keepRatio
                    if (root.keepRatio)
                        root.setSize(root.spriteWidth, root.spriteWidth * root.originalHeight / root.originalWidth)
                }
            }

            Label { text: qsTr("Scale") }
            RowLayout {
                spacing: 6
                NumberControl {
                    fieldWidth: 72
                    tabStop: true
                    onAccepted: root.apply()
                    from: 1
                    to: 100000
                    bigStep: 10
                    value: root.widthPct
                    tip: qsTr("Width scale")
                    onMoved: v => root.setScale(v, root.keepRatio ? v : root.heightPct)
                }
                Text {
                    text: "×"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize
                }
                NumberControl {
                    fieldWidth: 72
                    tabStop: true
                    onAccepted: root.apply()
                    from: 1
                    to: 100000
                    bigStep: 10
                    value: root.heightPct
                    tip: qsTr("Height scale")
                    onMoved: v => root.setScale(root.keepRatio ? v : root.widthPct, v)
                }
                Unit { text: "%" }
            }

            Label { text: qsTr("Method") }
            Segmented {
                id: methodChoice
                Layout.columnSpan: 2
                model: [qsTr("Nearest"), qsTr("Smooth"), qsTr("Bilinear")]
            }

            Item { implicitWidth: 1; implicitHeight: 1 }
            Unit {
                Layout.columnSpan: 2
                Layout.maximumWidth: 300
                wrapMode: Text.WordWrap
                text: qsTr("From %1 × %2. ").arg(root.originalWidth).arg(root.originalHeight)
                      + (root.method === "nearest" ? qsTr("Hard pixels, no new colors.")
                         : root.method === "smooth" ? qsTr("Rounds off stair steps with no new colors (pixel art).")
                         : qsTr("Blends neighbors, which adds colors (indexed sprites take Smooth)."))
            }
        }

        // The frame at the new size (the whole sprite is scaled on Resize).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            radius: Theme.radiusSmall
            color: Theme.hover
            Image {
                anchors.fill: parent
                anchors.margins: 6
                visible: root.previewSource !== ""
                source: root.previewSource
                fillMode: Image.PreserveAspectFit
                smooth: false
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Resize")
                primary: true
                enabled: root.valid
                onClicked: root.apply()
            }
        }
    }
}
