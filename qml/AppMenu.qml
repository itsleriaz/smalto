import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Themed context / drop-down menu. Use AppMenuItem and AppMenuSeparator.
Menu {
    id: menu
    padding: 4
    margins: 8
    background: Panel {
        implicitWidth: 200
        radius: Theme.radiusSmall + 2
        padding: 0
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
