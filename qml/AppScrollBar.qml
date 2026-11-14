import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Thin, rounded scroll bar that brightens on hover.
ScrollBar {
    id: bar
    policy: ScrollBar.AsNeeded
    padding: 2
    minimumSize: 0.08

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: bar.pressed ? Theme.textDim : bar.hovered ? Theme.textFaint : Theme.scrollHandle
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active || bar.size < 1 ? 1 : 0
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    background: null
}
