pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Smalto

// The colors a gradient passes through (Editor.gradientCustomStops), as a bar showing it with a
// marker at each stop: drag a marker to move it, click it to pick its color, right-click it to
// remove it, click the bar to add a stop there (in the color the gradient has at that spot).
Item {
    id: root

    required property Editor editor

    readonly property var stops: editor.gradientCustomStops
    readonly property int count: stops.length
    readonly property real pad: 7
    readonly property real barWidth: width - 2 * pad
    property int picked: -1

    implicitWidth: 190
    implicitHeight: 28

    // The color of stop `i`, the nearest one when it is out of range (while the list changes).
    function stopColor(i) {
        const s = root.stops
        return s.length === 0 ? "transparent" : s[Math.max(0, Math.min(s.length - 1, i))].color
    }
    function colorAt(position) {
        const s = root.stops
        if (s.length === 0)
            return "black"
        if (position <= s[0].position)
            return s[0].color
        for (let i = 1; i < s.length; ++i) {
            if (position <= s[i].position) {
                const span = s[i].position - s[i - 1].position
                const t = span > 0 ? (position - s[i - 1].position) / span : 0
                const a = s[i - 1].color, b = s[i].color
                return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
            }
        }
        return s[s.length - 1].color
    }
    // The list with one stop changed (position and / or color), or removed, or added.
    function write(list) {
        const out = []
        for (const e of list) {
            const o = {}
            o.position = e.position
            o.color = e.color
            out.push(o)
        }
        root.editor.gradientCustomStops = out
    }
    function setStop(index, position, color) {
        const list = root.stops.map(e => ({ position: e.position, color: e.color }))
        if (position !== undefined)
            list[index].position = position
        if (color !== undefined)
            list[index].color = color
        write(list)
    }
    function removeStop(index) {
        if (root.stops.length <= 2)
            return
        const list = root.stops.map(e => ({ position: e.position, color: e.color }))
        list.splice(index, 1)
        picker.close()
        write(list)
    }
    function addStop(position) {
        const list = root.stops.map(e => ({ position: e.position, color: e.color }))
        const entry = {}
        entry.position = position
        entry.color = colorAt(position)
        list.push(entry)
        list.sort((a, b) => a.position - b.position)
        write(list)
    }

    // The gradient itself: a flat run before the first stop, a segment between each pair, a flat
    // run after the last.
    Rectangle {
        x: root.pad
        y: 5
        width: root.barWidth
        height: 18
        radius: 3
        color: "transparent"
        border.color: Theme.controlBorder
        clip: true
        Repeater {
            model: root.count + 1
            delegate: Rectangle {
                id: segment
                required property int index
                readonly property real from: index === 0 || index > root.count ? 0 : root.stops[index - 1].position
                readonly property real to: index >= root.count ? 1 : root.stops[index].position
                x: from * root.barWidth
                width: Math.max(0, (to - from) * root.barWidth)
                height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: root.stopColor(segment.index - 1) }
                    GradientStop { position: 1; color: root.stopColor(segment.index) }
                }
            }
        }
    }
    MouseArea {
        x: root.pad
        width: root.barWidth
        height: parent.height
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => root.addStop(Math.max(0, Math.min(1, mouse.x / root.barWidth)))
    }
    Repeater {
        model: root.count
        delegate: Rectangle {
            id: marker
            required property int index
            readonly property var stop: index < root.count ? root.stops[index] : { position: 0, color: "transparent" }
            x: root.pad + stop.position * root.barWidth - width / 2
            y: 1
            width: 12
            height: 26
            radius: 3
            color: stop.color
            border.color: root.picked === index && picker.visible ? Theme.accent : "white"
            border.width: 2
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: 4
                color: "transparent"
                border.color: "black"
                border.width: 1
                z: -1
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -2
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                property bool moved: false
                onPressed: moved = false
                onPositionChanged: mouse => {
                    if (!(pressedButtons & Qt.LeftButton))
                        return
                    const x = mapToItem(root, mouse.x, 0).x
                    // Between its neighbors, so it keeps its place in the list.
                    const lo = marker.index > 0 ? root.stops[marker.index - 1].position + 0.001 : 0
                    const hi = marker.index < root.count - 1 ? root.stops[marker.index + 1].position - 0.001 : 1
                    const p = Math.max(lo, Math.min(hi, (x - root.pad) / root.barWidth))
                    if (Math.abs(p - marker.stop.position) > 0.002) {
                        moved = true
                        root.setStop(marker.index, p, undefined)
                    }
                }
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        root.removeStop(marker.index)
                    } else if (!moved) {
                        root.picked = marker.index
                        picker.open()
                    }
                }
            }
        }
    }

    Popup {
        id: picker
        y: root.height + 8
        x: root.pad + (root.picked >= 0 && root.picked < root.count ? root.stops[root.picked].position : 0) * root.barWidth - width / 2
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Panel { solid: true }
        contentItem: ColorPicker {
            current: root.picked >= 0 && root.picked < root.count ? root.stops[root.picked].color : "black"
            squareHeight: 120
            onEdited: c => root.setStop(root.picked, undefined, c)
        }
    }
}
