import QtQuick

// One choice of a SegmentGroup. Children draw over the icon (a custom
// preview, say).
Rectangle {
    id: root

    property string iconName
    property bool checked: false
    property string tip
    default property alias content: slot.data
    signal clicked()

    implicitWidth: 30
    implicitHeight: 26
    radius: Theme.radiusSmall - 2
    color: checked ? Theme.controlRaised : area.containsMouse ? Theme.hover : "transparent"
    border.color: checked ? Theme.controlBorder : "transparent"
    border.width: 1
    opacity: enabled ? 1 : 0.5
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    IconButton {
        anchors.fill: parent
        visible: root.iconName !== ""
        iconName: root.iconName
        icon.color: root.checked ? Theme.accent : Theme.textDim
        hoverEnabled: false
        background: null
        enabled: false
    }
    Item {
        id: slot
        anchors.fill: parent
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.clicked()
    }
    AppToolTip {
        text: root.tip
        active: area.containsMouse && root.tip !== ""
    }
}
