pragma ComponentBehavior: Bound

import QtQuick
import Qt.labs.platform as Native
import Smalto

// The macOS menu bar, made from `entries` (AppMenus.menus). Qt moves the
// entries with a role (Preferences, About, Quit) into the app menu. An entry
// with a `submenu` becomes a menu inside the menu. See NativeItem for keys.
Native.MenuBar {
    id: bar

    required property var entries
    // AppMenus.states: what the entries show now, applied without remaking them.
    property var states: ({})
    // The destructive entries' texts (AppMenus.dangerTexts): drawn red.
    property var dangerTexts: []
    signal chosen(string action, var arg)

    // Qt only moves the entries with a role (Preferences, About, Quit) to the
    // app menu when the bar is attached to its window, so the bar is attached
    // again, a moment after the menus were made or remade (they are remade
    // whenever something they show changes). Without it those entries only
    // appeared after switching to another app and back.
    property var host: null
    onEntriesChanged: { reattach.restart(); colorTimer.restart() }
    onStatesChanged: colorTimer.restart()
    onDangerTextsChanged: colorTimer.restart()
    Component.onCompleted: { reattach.restart(); colorTimer.restart() }
    // After the menus exist (and again after a text changed): Qt's items
    // reset their titles when remade.
    Timer {
        id: colorTimer
        interval: 120
        onTriggered: MenuStyle.markDestructive(bar.dangerTexts)
    }
    Timer {
        id: reattach
        interval: 40
        onTriggered: {
            if (!bar.host)
                return
            bar.window = null
            bar.window = bar.host
        }
    }

    // An entry (a slot of the Instantiator below) goes into its menu as an item or a menu.
    function place(menu: var, index: int, slot: var) {
        if (slot.sub)
            menu.insertMenu(index, slot.sub)
        else
            menu.insertItem(index, slot.item)
    }
    function unplace(menu: var, slot: var) {
        if (slot.sub)
            menu.removeMenu(slot.sub)
        else
            menu.removeItem(slot.item)
    }

    Instantiator {
        model: bar.entries
        delegate: Native.Menu {
            id: menu
            required property var modelData
            title: modelData.title

            Instantiator {
                model: menu.modelData.items
                delegate: QtObject {
                    id: slot
                    required property var modelData
                    // A plain entry, or a menu inside this one.
                    readonly property var item: NativeItem {
                        modelData: slot.modelData
                        states: bar.states
                        onChosen: (action, arg) => bar.chosen(action, arg)
                    }
                    readonly property var sub: slot.modelData.submenu ? subComponent.createObject(slot) : null

                    readonly property Component subComponent: Component {
                        Native.Menu {
                            id: submenu
                            title: slot.modelData.text
                            Instantiator {
                                model: slot.modelData.submenu
                                delegate: NativeItem {
                                    states: bar.states
                                    onChosen: (action, arg) => bar.chosen(action, arg)
                                }
                                onObjectAdded: (index, object) => submenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => submenu.removeItem(object)
                            }
                        }
                    }
                }
                onObjectAdded: (index, object) => bar.place(menu, index, object)
                onObjectRemoved: (index, object) => bar.unplace(menu, object)
            }
        }
        onObjectAdded: (index, object) => bar.insertMenu(index, object)
        onObjectRemoved: (index, object) => bar.removeMenu(object)
    }
}
