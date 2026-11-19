pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// Import Sprite Sheet on the canvas: the tiles as outlines, the first one (the box the dialog's X, Y,
// Width and Height describe) with handles. Drag its sides and corners to size it (the opposite side
// stays), drag inside to move it; the dialog's fields follow, and the other tiles with them. Shift
// keeps the tile's shape while sizing. Takes the mouse over the canvas while the dialog is open, so
// the tools don't draw; the wheel still zooms.
Item {
    id: root

    required property Editor editor
    required property CanvasView canvas
    required property var dialog // SpriteSheetDialog

    readonly property bool active: dialog.visible
    readonly property real zoom: canvas.zoom
    readonly property point offset: canvas.offset

    visible: active

    // The first tile on screen.
    readonly property real boxX: offset.x + dialog.tileX * zoom
    readonly property real boxY: offset.y + dialog.tileY * zoom
    readonly property real boxW: dialog.tileW * zoom
    readonly property real boxH: dialog.tileH * zoom

    // Everything the outlines depend on, so they paint again when any of it changes.
    readonly property string signature: [zoom, offset.x, offset.y, width, height, dialog.tileX, dialog.tileY,
                                         dialog.tileW, dialog.tileH, dialog.padX, dialog.padY, dialog.columns,
                                         dialog.rows, dialog.usePadding].join()
    onSignatureChanged: outlines.requestPaint()

    // The box as it was when a drag began, and where the pointer was in canvas pixels.
    property rect startBox
    property point pressDoc
    function docPoint(area, mouse) {
        const p = area.mapToItem(root, mouse.x, mouse.y)
        return Qt.point((p.x - offset.x) / zoom, (p.y - offset.y) / zoom)
    }
    function begin(area, mouse) {
        startBox = Qt.rect(dialog.tileX, dialog.tileY, dialog.tileW, dialog.tileH)
        pressDoc = docPoint(area, mouse)
    }
    function delta(area, mouse) {
        const p = docPoint(area, mouse)
        return Qt.point(Math.round(p.x - pressDoc.x), Math.round(p.y - pressDoc.y))
    }
    function move(area, mouse) {
        const d = delta(area, mouse)
        dialog.setBox(startBox.x + d.x, startBox.y + d.y, startBox.width, startBox.height)
    }
    // hx / hy: -1 the left / top side moves, 1 the right / bottom, 0 neither.
    function resize(area, mouse, hx, hy) {
        const d = delta(area, mouse)
        const s = startBox
        let l = s.x, t = s.y, r = s.x + s.width, b = s.y + s.height
        if (hx < 0) l = Math.max(0, Math.min(l + d.x, r - 1))
        if (hx > 0) r = Math.max(l + 1, r + d.x)
        if (hy < 0) t = Math.max(0, Math.min(t + d.y, b - 1))
        if (hy > 0) b = Math.max(t + 1, b + d.y)
        let w = r - l, h = b - t
        if (mouse.modifiers & Qt.ShiftModifier) {
            const k = hx !== 0 && hy !== 0 ? Math.max(w / s.width, h / s.height) : hx !== 0 ? w / s.width : h / s.height
            w = Math.max(1, Math.round(s.width * k))
            h = Math.max(1, Math.round(s.height * k))
            if (hx < 0) l = s.x + s.width - w
            if (hy < 0) t = s.y + s.height - h
        }
        dialog.setBox(l, t, w, h)
    }

    // The tiles. Past the first, only while they are big enough on screen to tell apart.
    Canvas {
        id: outlines
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const d = root.dialog, z = root.zoom
            if (!d.valid && d.columns < 1)
                return
            const stepX = (d.tileW + (d.usePadding ? d.padX : 0)) * z
            const stepY = (d.tileH + (d.usePadding ? d.padY : 0)) * z
            const detailed = stepX >= 4 && stepY >= 4 && d.count <= 4096
            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.alpha(Theme.accent, 0.75)
            if (detailed) {
                for (let c = 0; c < d.columns; ++c)
                    for (let r = 0; r < d.rows; ++r) {
                        if (c === 0 && r === 0)
                            continue
                        ctx.strokeRect(Math.round(root.boxX + c * stepX) + 0.5, Math.round(root.boxY + r * stepY) + 0.5,
                                       Math.round(root.boxW) - 1, Math.round(root.boxH) - 1)
                    }
            } else {
                // Just the area the tiles cover.
                ctx.strokeRect(Math.round(root.boxX) + 0.5, Math.round(root.boxY) + 0.5,
                               Math.round((d.columns - 1) * stepX + root.boxW) - 1,
                               Math.round((d.rows - 1) * stepY + root.boxH) - 1)
            }
        }
    }

    // Keeps clicks from the tools.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onWheel: wheel => wheel.accepted = false
    }

    Item {
        id: frame
        x: root.boxX
        y: root.boxY
        width: root.boxW
        height: root.boxH

        Rectangle {
            anchors.fill: parent
            color: Qt.alpha(Theme.accent, 0.12)
            border.color: Theme.accent
            border.width: 2
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor
            onPressed: mouse => root.begin(this, mouse)
            onPositionChanged: mouse => { if (pressed) root.move(this, mouse) }
            onWheel: wheel => wheel.accepted = false
        }

        // The numbers, while it is dragged.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 10
            text: root.dialog.tileX + ", " + root.dialog.tileY + "   " + root.dialog.tileW + " × " + root.dialog.tileH
            color: Theme.text
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
            font.pixelSize: Theme.fontSizeSmall
            font.features: { "tnum": 1 }
        }

        // Sides first (a strip along each), then the corners and middles over them.
        Repeater {
            model: [
                { hx: -1, hy: 0, cursor: Qt.SizeHorCursor }, { hx: 1, hy: 0, cursor: Qt.SizeHorCursor },
                { hx: 0, hy: -1, cursor: Qt.SizeVerCursor }, { hx: 0, hy: 1, cursor: Qt.SizeVerCursor },
            ]
            delegate: Item {
                id: side
                required property var modelData
                readonly property bool vertical: modelData.hx !== 0
                readonly property real inset: Math.min(8, (vertical ? frame.height : frame.width) / 4)
                x: vertical ? (modelData.hx + 1) / 2 * frame.width - 5 : inset
                y: vertical ? inset : (modelData.hy + 1) / 2 * frame.height - 5
                width: vertical ? 10 : frame.width - 2 * inset
                height: vertical ? frame.height - 2 * inset : 10
                MouseArea {
                    anchors.fill: parent
                    cursorShape: side.modelData.cursor
                    onPressed: mouse => root.begin(this, mouse)
                    onPositionChanged: mouse => { if (pressed) root.resize(this, mouse, side.modelData.hx, side.modelData.hy) }
                }
            }
        }

        Repeater {
            model: [
                { hx: -1, hy: -1, cursor: Qt.SizeFDiagCursor }, { hx: 0, hy: -1, cursor: Qt.SizeVerCursor },
                { hx: 1, hy: -1, cursor: Qt.SizeBDiagCursor }, { hx: 1, hy: 0, cursor: Qt.SizeHorCursor },
                { hx: 1, hy: 1, cursor: Qt.SizeFDiagCursor }, { hx: 0, hy: 1, cursor: Qt.SizeVerCursor },
                { hx: -1, hy: 1, cursor: Qt.SizeBDiagCursor }, { hx: -1, hy: 0, cursor: Qt.SizeHorCursor },
            ]
            delegate: Rectangle {
                id: handle
                required property var modelData
                width: 9
                height: 9
                x: (modelData.hx + 1) / 2 * frame.width - width / 2
                y: (modelData.hy + 1) / 2 * frame.height - height / 2
                color: handleArea.containsMouse || handleArea.pressed ? Theme.accent : Theme.text
                border.color: Theme.accent
                MouseArea {
                    id: handleArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: handle.modelData.cursor
                    onPressed: mouse => root.begin(this, mouse)
                    onPositionChanged: mouse => { if (pressed) root.resize(this, mouse, handle.modelData.hx, handle.modelData.hy) }
                }
            }
        }
    }
}
