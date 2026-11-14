import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Themed multi-line text field, styled like AppTextField. It grows with its
// lines (up to `maxLines`, then it scrolls).
TextArea {
    id: area

    property int maxLines: 4

    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontSize
    padding: 6
    leftPadding: 8
    rightPadding: 8
    wrapMode: TextEdit.NoWrap
    implicitHeight: Math.min(contentHeight, maxLines * fontMetrics.height) + topPadding + bottomPadding

    FontMetrics {
        id: fontMetrics
        font: area.font
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 30
        radius: Theme.radiusSmall
        color: Theme.hover
        border.width: 1
        border.color: area.activeFocus ? Theme.accent : "transparent"
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }
}
