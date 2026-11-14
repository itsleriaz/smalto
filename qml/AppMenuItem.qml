import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Menu entry: text, optional right-aligned hint (e.g. a shortcut).
MenuItem {
    id: item

    property string hint
    // A divider line instead of an entry (for menus made from a model).
    property bool rule: false
    // A destructive entry (delete, clear): red text and a red highlight.
    property bool danger: false

    implicitWidth: Math.max(200, row.implicitWidth + leftPadding + rightPadding)
    implicitHeight: !visible ? 0 : rule ? 9 : 28
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.fontSize

    contentItem: RowLayout {
        id: row
        spacing: 16
        Rectangle {
            visible: item.rule
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.separator
        }
        Text {
            visible: !item.rule
            Layout.fillWidth: true
            text: item.text
            font: item.font
            color: !item.enabled ? Theme.textFaint : item.danger ? Theme.danger : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            visible: item.hint !== "" && !item.rule
            text: item.hint
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textFaint
            verticalAlignment: Text.AlignVCenter
        }
    }
    indicator: null
    arrow: null
    background: Rectangle {
        radius: Theme.radiusSmall - 2
        color: !(item.highlighted && item.enabled && !item.rule) ? "transparent"
               : item.danger ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.16) : Theme.accentSoft
    }
}
