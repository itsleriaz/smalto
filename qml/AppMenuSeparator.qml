import QtQuick
import QtQuick.Controls.Basic
import Smalto

MenuSeparator {
    topPadding: 4
    bottomPadding: 4
    leftPadding: 6
    rightPadding: 6
    implicitHeight: visible ? topPadding + bottomPadding + 1 : 0
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.separator
    }
}
