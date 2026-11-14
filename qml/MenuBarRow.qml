pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// The in-window menu bar (Windows and Linux; macOS uses its own menu bar):
// one title per menu of `menus` (AppMenus.menus). Once a menu is open,
// hovering another title switches to it, as in a system menu bar.
Row {
    id: root

    property var menus: []
    signal chosen(string action, var arg)

    // The menu that is open, if any.
    property var openMenu: null

    spacing: 0

    Repeater {
        model: root.menus.filter(m => !m.nativeOnly)
        delegate: MenuButton {
            id: button
            required property var modelData
            text: modelData.title
            chevron: false
            implicitHeight: 24
            active: menu.visible
            onClicked: menu.open()
            onHoveredChanged: if (hovered && root.openMenu !== null && root.openMenu !== menu) {
                root.openMenu.close()
                menu.open()
            }

            ModelMenu {
                id: menu
                y: button.height + 4
                items: button.modelData.items
                onChosen: (action, arg) => root.chosen(action, arg)
                onAboutToShow: root.openMenu = menu
                onClosed: if (root.openMenu === menu) root.openMenu = null
            }
        }
    }
}
