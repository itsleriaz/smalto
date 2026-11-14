pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Smalto

// Themed drop-down. `labelFor(index)` can override the text of an entry.
ComboBox {
    id: box

    property var labelFor: null

    function labelOf(index) {
        return labelFor ? labelFor(index) : textAt(index)
    }

    implicitWidth: 140
    implicitHeight: 28
    font.pixelSize: Theme.fontSize

    contentItem: Text {
        leftPadding: 10
        rightPadding: box.indicator.width + 6
        // displayText, not textAt(): it updates once the model is there.
        text: box.labelFor ? box.labelFor(box.currentIndex) : box.displayText
        color: box.enabled ? Theme.text : Theme.textFaint
        font: box.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    // The icon is white; ColorImage tints it like IconButton's icons.
    indicator: ColorImage {
        x: box.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        source: "qrc:/icons/chevron-down.svg"
        sourceSize: Qt.size(28, 28)
        color: box.enabled ? Theme.textDim : Theme.textFaint
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: box.hovered || box.popup.visible ? Theme.pressed : Theme.hover
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    delegate: ItemDelegate {
        id: item
        required property int index
        width: ListView.view.width
        height: 28
        highlighted: box.highlightedIndex === index
        contentItem: Text {
            text: box.labelOf(item.index)
            color: item.index === box.currentIndex ? Theme.text : Theme.textDim
            font: box.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSmall - 2
            color: item.highlighted ? Theme.accentSoft : "transparent"
        }
    }
    popup: Popup {
        y: box.height + 4
        width: Math.max(box.width, 160)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
        }
        background: Panel {
            radius: Theme.radiusSmall + 2
            padding: 0
        }
    }
}
