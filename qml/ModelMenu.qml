pragma ComponentBehavior: Bound

import QtQuick
import Smalto

// An in-window menu made from `items` (see AppMenus): each entry's text, its
// key hint (a tick for a switch that is on), enabled state and a divider for a
// separator. An entry with `submenu` opens a menu beside it (one level).
// `chosen(action, arg)` says which was picked.
AppMenu {
    id: root

    property var items: []
    signal chosen(string action, var arg)

    function hintFor(d: var): string {
        return d.hint ?? (d.checked ? "✓" : (d.key ? (Shortcuts.labels[d.key] ?? "") : ""))
    }

    // One entry; a submenu entry also carries its nested menu in `sub`.
    component Entry: QtObject {
        id: slot
        required property var modelData
        readonly property AppMenuItem item: AppMenuItem {
            rule: slot.modelData.separator === true
            text: slot.modelData.text ?? ""
            enabled: !rule && slot.modelData.enabled !== false
            hint: root.hintFor(slot.modelData)
            onTriggered: root.chosen(slot.modelData.action, slot.modelData.arg)
        }
        readonly property var sub: slot.modelData.submenu ? subComponent.createObject(slot) : null
        readonly property Component subComponent: Component {
            AppMenu {
                id: submenu
                title: slot.modelData.text
                Instantiator {
                    model: slot.modelData.submenu
                    delegate: AppMenuItem {
                        required property var modelData
                        rule: modelData.separator === true
                        text: modelData.text ?? ""
                        enabled: !rule && modelData.enabled !== false
                        hint: root.hintFor(modelData)
                        onTriggered: root.chosen(modelData.action, modelData.arg)
                    }
                    onObjectAdded: (index, object) => submenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => submenu.removeItem(object)
                }
            }
        }
    }

    Instantiator {
        model: root.items
        delegate: Entry {}
        onObjectAdded: (index, object) => {
            const e = object as Entry
            if (e.sub)
                root.insertMenu(index, e.sub)
            else
                root.insertItem(index, e.item)
        }
        onObjectRemoved: (index, object) => {
            const e = object as Entry
            if (e.sub)
                root.removeMenu(e.sub)
            else
                root.removeItem(e.item)
        }
    }
}
