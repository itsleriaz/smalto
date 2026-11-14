pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Preferences > Shortcuts: every rebindable action with its keys. "+" records
// a new key (Esc cancels); a key used elsewhere moves here, and the line at
// the bottom says from where. Hovering a key shows × to remove it.
ColumnLayout {
    id: root

    // The action waiting for a key ("" when none).
    property string recording: ""
    property string message: ""
    // The row button under the pointer, for the one tooltip they share.
    property Item hoveredButton: null

    // Header rows ({header}) before each group's actions, filtered by name
    // or key.
    readonly property var rows: {
        const query = search.text.trim().toLowerCase()
        const out = []
        let group = ""
        for (const a of Shortcuts.actions) {
            if (query !== "" && !a.name.toLowerCase().includes(query)
                    && !a.shown.some(k => k.toLowerCase().includes(query)))
                continue
            if (a.group !== group) {
                group = a.group
                out.push({ header: group })
            }
            out.push(a)
        }
        return out
    }

    function startRecording(id: string) {
        recording = id
        message = ""
    }
    function record(id: string, key: string) {
        recording = ""
        if (Shortcuts.isReserved(key)) {
            message = qsTr("%1 is fixed (Esc, Enter, Delete, Backspace, arrows and Space keep their own jobs).").arg(Shortcuts.display(key))
            return
        }
        const taken = Shortcuts.assign(id, key)
        message = taken !== "" ? qsTr("%1 moved here from %2.").arg(Shortcuts.display(key)).arg(taken) : ""
    }

    onVisibleChanged: {
        recording = ""
        message = ""
    }

    spacing: 10

    component RowButton: Button {
        id: button
        property string iconName
        property string tip
        implicitWidth: 24
        implicitHeight: 24
        padding: 0
        flat: true
        display: AbstractButton.IconOnly
        focusPolicy: Qt.NoFocus
        icon.source: "qrc:/icons/" + iconName + ".svg"
        icon.width: 14
        icon.height: 14
        icon.color: hovered ? Theme.text : Theme.textDim
        background: Rectangle {
            radius: Theme.radiusSmall - 2
            color: button.down ? Theme.pressed : button.hovered ? Theme.hover : "transparent"
        }
        onHoveredChanged: root.hoveredButton = hovered ? button : (root.hoveredButton === button ? null : root.hoveredButton)
    }

    RowLayout {
        spacing: 8
        Text {
            text: qsTr("Shortcuts")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        AppTextField {
            id: search
            placeholderText: qsTr("Search")
            Layout.preferredWidth: 150
        }
        AppButton {
            text: qsTr("Reset all")
            enabled: Shortcuts.customized
            onClicked: {
                Shortcuts.resetAll()
                root.recording = ""
                root.message = ""
            }
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.rows
        ScrollBar.vertical: AppScrollBar {}

        // A press on empty space stops recording.
        MouseArea {
            parent: list.contentItem
            width: list.width
            height: Math.max(list.contentHeight, list.height)
            z: -1
            onPressed: root.recording = ""
        }

        delegate: Item {
            id: row
            required property var modelData
            readonly property bool header: modelData.header !== undefined
            readonly property bool recordingThis: !header && root.recording === modelData.id

            width: ListView.view.width - 10
            height: header ? 28 : 30

            Text {
                visible: row.header
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                text: row.header ? row.modelData.header : ""
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            Rectangle {
                visible: !row.header
                anchors.fill: parent
                radius: Theme.radiusSmall - 2
                color: rowHover.hovered || row.recordingThis ? Theme.hover : "transparent"
            }
            HoverHandler { id: rowHover; enabled: !row.header }

            RowLayout {
                visible: !row.header
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 4

                Text {
                    text: row.header ? "" : row.modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Repeater {
                    model: row.header ? [] : row.modelData.shown
                    delegate: Rectangle {
                        id: chip
                        required property string modelData
                        required property int index
                        Layout.preferredWidth: chipText.implicitWidth + 16 + (chipHover.hovered ? 14 : 0)
                        Layout.preferredHeight: 22
                        radius: 5
                        color: Theme.hover
                        border.color: Theme.separator
                        HoverHandler { id: chipHover }
                        Text {
                            id: chipText
                            anchors.verticalCenter: parent.verticalCenter
                            x: 8
                            text: chip.modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSmall
                            font.features: { "tnum": 1 }
                        }
                        Text {
                            visible: chipHover.hovered
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            text: "×"
                            color: removeArea.containsMouse ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            id: removeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.message = ""
                                Shortcuts.unassign(row.modelData.id, row.modelData.keys[chip.index])
                            }
                        }
                    }
                }

                // Waiting for a key: takes every key press, including ones
                // the window or the dialog would otherwise act on.
                Rectangle {
                    visible: row.recordingThis
                    Layout.preferredWidth: waitText.implicitWidth + 16
                    Layout.preferredHeight: 22
                    radius: 5
                    color: Theme.accentSoft
                    border.color: Theme.accent
                    focus: visible
                    onVisibleChanged: if (visible) forceActiveFocus()
                    onActiveFocusChanged: if (!activeFocus && row.recordingThis) root.recording = ""
                    Keys.onShortcutOverride: event => event.accepted = true
                    Keys.onPressed: event => {
                        event.accepted = true
                        if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                            root.recording = ""
                            return
                        }
                        const key = Shortcuts.fromKeyEvent(event.key, event.modifiers)
                        if (key !== "")
                            root.record(row.modelData.id, key)
                    }
                    Text {
                        id: waitText
                        anchors.centerIn: parent
                        text: qsTr("Press a key…")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }

                RowButton {
                    iconName: "plus"
                    tip: qsTr("Add a key")
                    visible: !row.recordingThis
                    opacity: rowHover.hovered || (!row.header && row.modelData.shown.length === 0) ? 1 : 0
                    onClicked: root.startRecording(row.modelData.id)
                }
                RowButton {
                    iconName: "rotate-ccw"
                    tip: qsTr("Back to the default")
                    visible: !row.header && row.modelData.custom
                    onClicked: {
                        root.message = ""
                        Shortcuts.reset(row.modelData.id)
                    }
                }
            }
        }

        AppToolTip {
            parent: root.hoveredButton ?? list
            text: root.hoveredButton ? (root.hoveredButton as RowButton).tip : ""
            active: root.hoveredButton !== null
        }
    }

    Text {
        text: root.message !== "" ? root.message
            : qsTr("Esc, Enter, Delete, Backspace, the arrows and Space keep their own jobs.")
        color: root.message !== "" ? Theme.text : Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
