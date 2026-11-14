pragma ComponentBehavior: Bound

import QtQuick
import QtQml
import Smalto

// Arranges the side panels in two columns, left and right: each column is a
// stack of slots from the top, every slot as wide as its column. A slot holds
// one panel, or several as tabs (a strip of names above it shows the one
// that is open). A slot keeps the height its open panel wants unless the
// column is short, when the ones that give way first (`shrinkRank`) shrink
// down to their `minHeight`. The arrangement is saved as JSON (`layoutJson`):
// per column its slots, each with its tabs and the open one. A grip on each
// panel (or a tab) drags it elsewhere: a line shows where it lands as a slot
// of its own, a title it is dropped on takes it as another tab; the gaps
// between stacked slots drag the upper one's height.
//
// The panels are items made by the window and given to `panels` by id; they
// take their `x`, `y`, `width` and `height` from `xOf`, `yOf`, `widthOf`,
// `heightOf`, show only when `isActive(id)` and take their drag state from
// `dragId`, `dragDX`, `dragDY`.
Item {
    id: root

    // id -> the panel item; each has `wantHeight`, `minHeight`, `shrinkRank`.
    required property var panels
    // id -> whether the window shows that panel at all (a preference).
    required property var wanted
    property string layoutJson: ""
    signal layoutJsonEdited(string json)
    // A gap between two slots was dragged: the open panel above is now this tall.
    signal heightRequested(string id, real height)
    signal leftWidthRequested(real width)
    signal rightWidthRequested(real width)

    // Where the columns are (window coordinates).
    property real columnTop: 0
    property real columnBottom: 600
    property real leftX: 10
    property real rightX: 1000 // the right column's right edge
    property real leftWidth: 230
    property real rightWidth: 248
    // Between stacked slots (none when docked).
    property real gap: Theme.spacing
    property bool docked: false
    // Docked columns have no empty room: what is left over goes to the last slot.
    property bool fill: false
    readonly property real tabHeight: 26

    readonly property var ids: ["layers", "colors", "preview", "live"]
    // The "live" slot holds the current live layer's settings: a particle layer's or a Smear group's.
    property string liveName: qsTr("Particles")
    readonly property var names: ({ layers: qsTr("Layers"), colors: qsTr("Colors"), preview: qsTr("Preview"), live: liveName })
    readonly property var layout: sanitized(layoutJson)
    readonly property var arrangement: compute()
    readonly property var placement: arrangement.panels // the open panels, by id
    readonly property var slotList: arrangement.slots   // every slot with a panel to show
    readonly property bool hasLeft: slotList.some(sl => sl.side === "left")
    readonly property bool hasRight: slotList.some(sl => sl.side === "right")

    // A drag in progress: which panel, how far it has moved, and where it
    // would land: after `dropIndex` of the column's other slots, or as a tab
    // of the slot whose first tab is `dropMerge`.
    property string dragId: ""
    property real dragDX: 0
    property real dragDY: 0
    property string dropSide: ""
    property int dropIndex: 0
    property string dropMerge: ""

    // --- the arrangement ------------------------------------------------

    // JSON -> { left: [slot], right: [slot] }, a slot being { tabs, active }.
    // A column may list plain ids (the older format) or slots; unknown ids
    // go, repeats go, and a panel that is missing returns to its usual column.
    function sanitized(json: string): var {
        let parsed = {}
        try { parsed = JSON.parse(json) } catch (e) { parsed = {} }
        const out = { left: [], right: [] }
        const seen = {}
        for (const side of ["left", "right"])
            for (const entry of (Array.isArray(parsed[side]) ? parsed[side] : [])) {
                const raw = typeof entry === "string" ? [entry] : Array.isArray(entry) ? entry
                          : (entry && Array.isArray(entry.tabs) ? entry.tabs : [])
                const tabs = raw.filter(id => ids.indexOf(id) >= 0 && !seen[id])
                if (tabs.length === 0)
                    continue
                for (const id of tabs)
                    seen[id] = true
                const active = entry && typeof entry.active === "string" && tabs.indexOf(entry.active) >= 0 ? entry.active : tabs[0]
                out[side].push({ tabs: tabs, active: active })
            }
        for (const id of ids)
            if (!seen[id]) {
                // The particle settings start as a tab of the preview.
                const host = id === "live" ? out.right.concat(out.left).find(sl => sl.tabs.indexOf("preview") >= 0) : undefined
                if (host)
                    host.tabs.push(id)
                else
                    out[id === "layers" ? "left" : "right"].push({ tabs: [id], active: id })
            }
        return out
    }
    function cloneLayout(): var {
        const copy = {}
        for (const side of ["left", "right"])
            copy[side] = layout[side].map(sl => ({ tabs: sl.tabs.slice(), active: sl.active }))
        return copy
    }

    function compute(): var {
        const panelsOut = {}
        const slotsOut = []
        const avail = columnBottom - columnTop
        for (const side of ["left", "right"]) {
            const shown = []
            layout[side].forEach((sl, index) => {
                const tabs = sl.tabs.filter(id => wanted[id] && panels[id])
                if (tabs.length === 0)
                    return
                shown.push({ index: index, tabs: tabs, active: tabs.indexOf(sl.active) >= 0 ? sl.active : tabs[0],
                             strip: tabs.length > 1 ? tabHeight : 0 })
            })
            const width = side === "left" ? leftWidth : rightWidth
            const x = side === "left" ? leftX : rightX - width
            const want = shown.map(s => panels[s.active].wantHeight + s.strip)
            const least = shown.map(s => panels[s.active].minHeight + s.strip)
            let over = want.reduce((a, b) => a + b, 0) + Math.max(0, shown.length - 1) * gap - avail
            const order = shown.map((s, i) => i).sort((a, b) => panels[shown[a].active].shrinkRank - panels[shown[b].active].shrinkRank)
            for (const i of order) {
                if (over <= 0)
                    break
                const take = Math.min(Math.max(0, want[i] - least[i]), over)
                want[i] -= take
                over -= take
            }
            // Still too tall (a very short window): the last slots give up their
            // minimum too, down to a header's worth, so nothing runs under the timeline.
            for (let i = shown.length - 1; i >= 0 && over > 0; --i) {
                const take = Math.min(Math.max(0, want[i] - shown[i].strip - 40), over)
                want[i] -= take
                over -= take
            }
            if (fill && shown.length > 0) {
                const used = want.reduce((a, b) => a + b, 0) + (shown.length - 1) * gap
                if (used < avail)
                    want[shown.length - 1] += avail - used
            }
            let y = columnTop
            shown.forEach((s, k) => {
                panelsOut[s.active] = { side: side, x: x, y: y + s.strip, w: width, h: want[k] - s.strip,
                                        first: k === 0, last: k === shown.length - 1, slot: s.index }
                slotsOut.push({ side: side, index: s.index, k: k, tabs: s.tabs, active: s.active, strip: s.strip,
                                x: x, y: y, w: width, h: want[k], last: k === shown.length - 1 })
                y += want[k] + gap
            })
        }
        return { panels: panelsOut, slots: slotsOut }
    }

    // Opens the tab that holds `id` (when it is a tab of a slot).
    function reveal(id: string) {
        for (const side of ["left", "right"]) {
            const index = layout[side].findIndex(sl => sl.tabs.indexOf(id) >= 0)
            if (index >= 0) {
                if (layout[side][index].active !== id)
                    activate(side, index, id)
                return
            }
        }
    }
    function isActive(id: string): bool { return placement[id] !== undefined }
    function firstOf(id: string): bool { return placement[id] !== undefined && placement[id].first }
    function xOf(id: string): real { return (placement[id] ? placement[id].x : 0) + (dragId === id ? dragDX : 0) }
    function yOf(id: string): real { return (placement[id] ? placement[id].y : 0) + (dragId === id ? dragDY : 0) }
    function widthOf(id: string): real { return placement[id] ? placement[id].w : 0 }
    function heightOf(id: string): real { return placement[id] ? placement[id].h : 0 }

    function commit(next: var) {
        const json = JSON.stringify(next)
        if (json !== JSON.stringify(layout))
            layoutJsonEdited(json)
    }
    // Opens a tab.
    function activate(side: string, slotIndex: int, id: string) {
        const next = cloneLayout()
        next[side][slotIndex].active = id
        commit(next)
    }

    // --- moving a panel -------------------------------------------------

    // The slots of the column that the panel `id` isn't alone in.
    function otherSlots(side: string, id: string): var {
        return slotList.filter(sl => sl.side === side && !(sl.tabs.length === 1 && sl.tabs[0] === id))
    }
    function dragTo(id: string, dx: real, dy: real, sceneX: real, sceneY: real) {
        dragId = id
        dragDX = dx
        dragDY = dy
        const p = root.mapFromItem(null, sceneX, sceneY)
        dropSide = p.x < (leftX + leftWidth + (rightX - rightWidth)) / 2 ? "left" : "right"
        dropMerge = ""
        let count = 0
        for (const sl of otherSlots(dropSide, id)) {
            // Over a title (the tab strip and the panel's header): a tab of it.
            const from = sl.y + (sl.k === 0 ? 8 : 0)
            if (p.y >= from && p.y < sl.y + sl.strip + 34 && sl.tabs.indexOf(id) < 0) {
                dropMerge = sl.tabs[0]
                break
            }
            if (sl.y + sl.h / 2 < p.y)
                ++count
        }
        dropIndex = count
    }
    function endDrag(id: string) {
        const side = dropSide
        const merge = dropMerge
        const others = otherSlots(side, id)
        const before = dropIndex < others.length ? others[dropIndex].tabs[0] : ""
        dragId = ""
        dragDX = dragDY = 0
        dropSide = ""
        dropMerge = ""
        if (side === "")
            return
        const next = cloneLayout()
        for (const s of ["left", "right"]) { // leave the old slot
            for (const sl of next[s]) {
                const at = sl.tabs.indexOf(id)
                if (at < 0)
                    continue
                sl.tabs.splice(at, 1)
                if (sl.active === id)
                    sl.active = sl.tabs[0]
            }
            next[s] = next[s].filter(sl => sl.tabs.length > 0)
        }
        if (merge !== "") {
            const target = next[side].find(sl => sl.tabs[0] === merge)
            if (target) {
                target.tabs.push(id)
                target.active = id
            }
        } else {
            let at = before === "" ? next[side].length : next[side].findIndex(sl => sl.tabs[0] === before)
            if (at < 0)
                at = next[side].length
            next[side].splice(at, 0, { tabs: [id], active: id })
        }
        commit(next)
    }

    Instantiator {
        model: root.ids
        delegate: Connections {
            required property string modelData
            target: root.panels[modelData]
            function onMoveStarted() { root.dragId = modelData }
            function onMoveBy(dx: real, dy: real, sceneX: real, sceneY: real) { root.dragTo(modelData, dx, dy, sceneX, sceneY) }
            function onMoveEnded() { if (root.dragId === modelData) root.endDrag(modelData) }
        }
    }

    // --- tab strips -----------------------------------------------------

    Repeater {
        model: root.slotList.filter(sl => sl.strip > 0)
        delegate: Rectangle {
            id: strip
            required property var modelData
            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: root.tabHeight
            radius: root.docked ? 0 : Theme.radiusSmall
            color: Theme.panel
            z: 5

            Row {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 2
                Repeater {
                    model: strip.modelData.tabs
                    delegate: Rectangle {
                        id: tab
                        required property string modelData
                        readonly property bool open: modelData === strip.modelData.active
                        width: (strip.width - 6 - (strip.modelData.tabs.length - 1) * 2) / strip.modelData.tabs.length
                        height: strip.height - 6
                        radius: Theme.radiusSmall - 2
                        color: open ? Theme.accentSoft : tabArea.containsMouse ? Theme.hover : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: root.names[tab.modelData]
                            color: tab.open ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: tab.open ? Font.DemiBold : Font.Normal
                        }
                        MouseArea {
                            id: tabArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.activate(strip.modelData.side, strip.modelData.index, tab.modelData)
                        }
                        // Dragging a tab takes its panel (opened first) elsewhere.
                        DragHandler {
                            target: null
                            onActiveChanged: {
                                if (active) {
                                    root.activate(strip.modelData.side, strip.modelData.index, tab.modelData)
                                    root.dragId = tab.modelData
                                } else if (root.dragId === tab.modelData) {
                                    root.endDrag(tab.modelData)
                                }
                            }
                            onCentroidChanged: if (active)
                                root.dragTo(tab.modelData, centroid.scenePosition.x - centroid.scenePressPosition.x,
                                            centroid.scenePosition.y - centroid.scenePressPosition.y,
                                            centroid.scenePosition.x, centroid.scenePosition.y)
                        }
                    }
                }
            }
        }
    }

    // --- drop feedback --------------------------------------------------

    // While a panel is dragged, an empty side shows where it could go.
    Repeater {
        model: ["left", "right"]
        delegate: Rectangle {
            required property string modelData
            readonly property bool empty: modelData === "left" ? !root.hasLeft : !root.hasRight
            visible: root.dragId !== "" && empty
            x: modelData === "left" ? root.leftX : root.rightX - root.rightWidth
            y: root.columnTop
            width: modelData === "left" ? root.leftWidth : root.rightWidth
            height: root.columnBottom - root.columnTop
            radius: Theme.radius
            color: root.dropSide === modelData ? Theme.accentSoft : Theme.accentTint
            border.color: Theme.accent
            border.width: root.dropSide === modelData ? 2 : 1
            z: 35
        }
    }

    // Where the dragged panel would land: a line between slots,
    Rectangle {
        readonly property var column: root.dropSide === "" ? [] : root.otherSlots(root.dropSide, root.dragId)
        visible: root.dragId !== "" && root.dropSide !== "" && root.dropMerge === ""
        x: root.dropSide === "left" ? root.leftX : root.rightX - root.rightWidth
        width: root.dropSide === "left" ? root.leftWidth : root.rightWidth
        y: {
            if (column.length === 0)
                return root.columnTop
            if (root.dropIndex === 0)
                return column[0].y - root.gap / 2 - 1
            const above = column[Math.min(root.dropIndex, column.length) - 1]
            return above.y + above.h + root.gap / 2 - 1
        }
        height: 3
        radius: 1.5
        color: Theme.accent
        z: 40
    }
    // or a title that takes it as a tab.
    Rectangle {
        readonly property var target: root.dropMerge === "" ? undefined : root.slotList.find(sl => sl.side === root.dropSide && sl.tabs[0] === root.dropMerge)
        visible: root.dragId !== "" && target !== undefined
        x: target ? target.x : 0
        y: target ? target.y : 0
        width: target ? target.w : 0
        height: target ? target.strip + 34 : 0
        radius: Theme.radiusSmall
        color: Theme.accentSoft
        border.color: Theme.accent
        border.width: 2
        z: 40
    }

    // --- sizes ----------------------------------------------------------

    // The gap under a slot that has another below it. The model is a count,
    // not `slotList`: that array is rebuilt on every height change, which
    // would recreate the handles (and end their drag) at the first step.
    Repeater {
        model: root.slotList.length
        delegate: EdgeHandle {
            id: gapHandle
            required property int index
            readonly property var slot: root.slotList[index] ?? null
            visible: slot !== null && !slot.last && root.dragId === ""
            x: slot ? slot.x : 0
            y: slot ? slot.y + slot.h + root.gap / 2 - 5 : 0
            width: slot ? slot.w : 0
            horizontal: false
            from: slot ? root.panels[slot.active].minHeight + slot.strip : 0
            to: 900
            value: slot ? slot.h : 0
            tip: qsTr("Drag to change the height of this panel")
            onResized: v => { if (gapHandle.slot) root.heightRequested(gapHandle.slot.active, v - gapHandle.slot.strip) }
        }
    }
    // The columns' inner edges.
    EdgeHandle {
        visible: root.hasLeft
        x: root.leftX + root.leftWidth - 5
        y: root.columnTop
        height: root.columnBottom - root.columnTop
        direction: 1
        from: 180
        to: 520
        value: root.leftWidth
        tip: qsTr("Drag to change the width of the left panels")
        onResized: v => root.leftWidthRequested(v)
    }
    EdgeHandle {
        visible: root.hasRight
        x: root.rightX - root.rightWidth - 5
        y: root.columnTop
        height: root.columnBottom - root.columnTop
        direction: -1
        from: 180
        to: 520
        value: root.rightWidth
        tip: qsTr("Drag to change the width of the right panels")
        onResized: v => root.rightWidthRequested(v)
    }
}
