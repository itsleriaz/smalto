import QtQuick
import Qt.labs.platform as Native
import Smalto

// One entry of the macOS menu bar (see NativeMenuBar and AppMenus). A key
// equivalent is bound only when it has Cmd or is an F key: a plain key
// (B, +, 0) would be taken from a text field you are typing in, so the key is
// written after the name instead, and the window's own shortcut stays in charge.
Native.MenuItem {
    id: entry

    required property var modelData
    // The entries' current texts, enabled states and ticks, by id (AppMenus.states).
    property var states: ({})
    signal chosen(string action, var arg)

    readonly property var state: states[modelData.id] ?? ({ text: modelData.text ?? "", enabled: true, checked: false })

    readonly property string sequence: modelData.key ? (Shortcuts.keys[modelData.key] ?? [])[0] ?? "" : (modelData.seq ?? "")
    readonly property bool bound: /Ctrl\+|Meta\+/.test(sequence) || /^F\d+$/.test(sequence)
    readonly property string plainKey: modelData.key && !bound ? (Shortcuts.labels[modelData.key] ?? "") : ""

    text: (modelData.submenu ? modelData.text : state.text ?? "") + (plainKey !== "" ? "    " + plainKey : "")
    separator: modelData.separator === true
    enabled: state.enabled
    checkable: modelData.checkable === true
    checked: state.checked
    shortcut: bound ? sequence : ""
    role: modelData.role === "preferences" ? Native.MenuItem.PreferencesRole
        : modelData.role === "about" ? Native.MenuItem.AboutRole
        : modelData.role === "quit" ? Native.MenuItem.QuitRole
        : Native.MenuItem.TextHeuristicRole
    onTriggered: chosen(modelData.action, modelData.arg)
}
