import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Themed tooltip with shared warm-up timing (see Tips). Bind `active` to
// the hover state of whatever it describes.
ToolTip {
    id: tip

    property bool active: false

    delay: 0
    timeout: -1
    padding: 8
    topPadding: 5
    bottomPadding: 5
    margins: 6
    font.pixelSize: Theme.fontSizeSmall

    onActiveChanged: {
        if (active && text !== "") {
            showTimer.interval = Tips.warm ? 0 : Tips.delay
            showTimer.restart()
        } else {
            showTimer.stop()
            close()
        }
    }
    onOpened: Tips.shown()
    onClosed: Tips.hidden()

    Timer {
        id: showTimer
        onTriggered: if (tip.active) tip.open()
    }

    contentItem: Text {
        text: tip.text
        font: tip.font
        color: Theme.text
        textFormat: Text.StyledText
        wrapMode: Text.Wrap
        lineHeight: 1.15
    }
    background: Rectangle {
        color: Theme.tooltip
        radius: Theme.radiusSmall
        border.color: Theme.panelBorder
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
