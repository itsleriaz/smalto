pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Smalto

// Undo and redo, then the preferences button (in the macOS menu bar instead,
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
            iconName: "undo-2"
            tip: "Undo" + Shortcuts.hints.undo
            enabled: root.editor.canUndo
            onClicked: root.editor.undo()
        }
        IconButton {
            iconName: "redo-2"
            tip: "Redo" + Shortcuts.hints.redo
            enabled: root.editor.canRedo
            onClicked: root.editor.redo()
        }

        IconButton {
            visible: !root.nativeMenus
            iconName: "settings"
            tip: "Preferences" + Shortcuts.hints.preferences
            onClicked: root.chosen("preferences", undefined)
        }
    }
}
