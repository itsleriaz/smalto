import QtQuick
import QtQuick.Layouts
import Smalto

// A number you can set every way: scroll (Shift: bigger steps), press and
// drag sideways to scrub, or click to type (Enter / clicking away applies,
// Esc cancels). Optional - / + buttons. Emits `moved` with each new value
// (already clamped); the owner writes it back to `value`. With `divisor` > 1
// the value counts 1/divisor units and shows as value / divisor (divisor 2: halves,
// "7.5"); typing takes decimals. With `factor` > 0, dragging and scrolling
// multiply the value by 1 + factor per step instead of adding `step` (zoom).
RowLayout {
    id: root

    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1
    property int bigStep: step * 10
    property string suffix: ""
    property string zeroText: ""   // shown instead of 0, e.g. "∞"
    property string shownText: ""  // shown instead of the value (and its suffix), e.g. "×2"
    property bool typeable: true   // false: clicking doesn't open the text field (drag, scroll and buttons still work)
    property bool buttons: false
    property int fieldWidth: 56
    property string tip: ""
    property int divisor: 1
    property real factor: 0
    property bool tabStop: false   // Tab reaches it and opens the text field; Tab again moves on
    readonly property bool editing: field.visible

    signal moved(int value)
    signal accepted()   // Enter in the text field (after the value was applied)

    activeFocusOnTab: tabStop
    onActiveFocusChanged: if (activeFocus && tabStop && !field.visible) startEditing()

    function set(v) {
        v = Math.max(from, Math.min(to, Math.round(v)))
        if (v !== value)
            moved(v)
    }
    function shown(v) { return divisor > 1 ? String(v / divisor) : String(v) }
    function startEditing() {
        field.cancelled = false
        field.text = shown(value)
        field.visible = true
        field.forceActiveFocus()
        field.selectAll()
    }

    spacing: 2

    IconButton {
        visible: root.buttons
        iconName: "minus"
        enabled: root.value > root.from
        onClicked: root.set(root.value - root.step)
    }

    Item {
        Layout.preferredWidth: Math.max(root.fieldWidth, label.implicitWidth + 12)
        Layout.preferredHeight: 26

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall - 2
            // An outlined field: it is a value you can type or drag.
            color: area.pressed ? Theme.pressed : area.containsMouse ? Theme.hover : Theme.controlFill
            border.color: Theme.controlBorder
            border.width: 1
            visible: !field.visible
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        Text {
            id: label
            visible: !field.visible
            anchors.centerIn: parent
            text: root.shownText !== "" ? root.shownText
                  : root.value === 0 && root.zeroText !== "" ? root.zeroText : root.shown(root.value) + root.suffix
            color: Theme.text
            font.pixelSize: Theme.fontSize
            font.features: { "tnum": 1 }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            visible: !field.visible
            hoverEnabled: true
            // A scrub is sideways only: a scrolling panel around it must not take the drag over.
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real pressX
            property int pressValue
            property bool scrubbed: false
            onPressed: mouse => {
                pressX = mouse.x
                pressValue = root.value
                scrubbed = false
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const dx = mouse.x - pressX
                if (!scrubbed && Math.abs(dx) < 3)
                    return
                scrubbed = true
                // One step per 6 px (Shift: big steps).
                const big = (mouse.modifiers & Qt.ShiftModifier) !== 0
                if (root.factor > 0)
                    root.set(pressValue * Math.pow(1 + root.factor, Math.round(dx / 6) * (big ? 5 : 1)))
                else
                    root.set(pressValue + Math.round(dx / 6) * (big ? root.bigStep : root.step))
            }
            onReleased: if (!scrubbed && root.typeable) root.startEditing()
            onWheel: wheel => {
                const big = (wheel.modifiers & Qt.ShiftModifier) !== 0
                const step = big ? root.bigStep : root.step
                const ticks = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y / 120 : wheel.angleDelta.x / 120
                if (ticks !== 0 && root.factor > 0)
                    root.set(root.value * Math.pow(1 + root.factor, (ticks > 0 ? 1 : -1) * (big ? 5 : 1)))
                else if (ticks !== 0)
                    root.set(root.value + (ticks > 0 ? step : -step))
            }
            AppToolTip {
                text: root.typeable ? (root.tip !== "" ? root.tip + " · click to type, drag or scroll to change"
                                                       : "Click to type, drag or scroll to change")
                                    : (root.tip !== "" ? root.tip + " · drag or scroll to change" : "Drag or scroll to change")
                active: area.containsMouse && !area.pressed
            }
        }

        AppTextField {
            id: field
            visible: false
            anchors.fill: parent
            compact: true
            horizontalAlignment: Text.AlignHCenter
            font.features: { "tnum": 1 }
            validator: root.divisor > 1 ? decimals : whole
            readonly property IntValidator whole: IntValidator { bottom: root.from; top: root.to }
            readonly property RegularExpressionValidator decimals: RegularExpressionValidator {
                regularExpression: /-?\d*([.,]\d*)?/
            }
            property bool cancelled: false
            onAccepted: {
                finish()
                root.accepted()
            }
            Keys.onBacktabPressed: event => {
                finish()
                const prev = root.tabStop ? root.nextItemInFocusChain(false) : null
                if (prev)
                    prev.forceActiveFocus(Qt.BacktabFocusReason)
                event.accepted = prev !== null
            }
            onActiveFocusChanged: if (!activeFocus && visible) finish()
            Keys.onEscapePressed: {
                cancelled = true
                visible = false
            }
            function finish() {
                if (!visible)
                    return
                visible = false
                if (!cancelled && text !== "")
                    root.set(root.divisor > 1 ? parseFloat(text.replace(",", ".")) * root.divisor : parseInt(text))
                cancelled = false
            }
        }
    }

    IconButton {
        visible: root.buttons
        iconName: "plus"
        enabled: root.value < root.to
        onClicked: root.set(root.value + root.step)
    }
}
