pragma Singleton
import QtQuick

// Shared tooltip timing: the first tooltip waits `delay`; while one is
// showing, or for `coolDown` after it hides, the next shows at once.
QtObject {
    id: tips
    readonly property int delay: 600
    readonly property int coolDown: 700
    property bool warm: false

    function shown() {
        warm = true
        coolTimer.stop()
    }
    function hidden() {
        coolTimer.restart()
    }

    property Timer coolTimer: Timer {
        interval: tips.coolDown
        onTriggered: tips.warm = false
    }
}
