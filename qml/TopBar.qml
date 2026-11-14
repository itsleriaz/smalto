pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Smalto

// The preferences button (in the macOS menu bar instead,
// `nativeMenus`). The in-window menus are in the strip above (MenuBarRow).
Panel {
    id: root

    required property Editor editor
    property bool nativeMenus: false
    signal chosen(string action, var arg)

    bare: true // sits in the title bar
    implicitWidth: row.implicitWidth + padding * 2
    implicitHeight: Theme.buttonSize + padding * 2
    padding: 0

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 2

        IconButton {
            visible: !root.nativeMenus
            iconName: "settings"
            tip: "Preferences" + Shortcuts.hints.preferences
            onClicked: root.chosen("preferences", undefined)
        }
    }
}
