import QtQuick
import QtQuick.Controls.Basic
import Smalto

// Themed single-line text field. Red outline while the input isn't valid.
TextField {
    id: field

    property bool compact: false // smaller, for inline editing (e.g. layer names)
    // Numbers and codes: the whole text is selected when the field gets focus
    // (a click or Tab), so typing replaces it. After the click's own cursor move,
    // hence callLater.
    property bool selectAllOnFocus: false
    // The first click after the field got focus (also from code, as in a dialog that
    // focuses it when it opens) selects everything too; later clicks place the cursor.
    property bool firstClick: false
    onActiveFocusChanged: {
        if (!selectAllOnFocus)
            return
        firstClick = activeFocus
        if (activeFocus)
            Qt.callLater(selectAll)
    }
    TapHandler {
        enabled: field.selectAllOnFocus
        acceptedButtons: Qt.LeftButton
        onTapped: if (field.firstClick) {
            field.selectAll()
            field.firstClick = false
        }
    }

    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontSize
    padding: compact ? 2 : 6
    leftPadding: compact ? 4 : 8
    rightPadding: compact ? 4 : 8

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: field.compact ? 22 : 30
        radius: field.compact ? 4 : Theme.radiusSmall
        color: field.compact ? Theme.background : Theme.hover
        border.width: 1
        border.color: !field.acceptableInput ? Theme.error
                    : field.activeFocus || field.compact ? Theme.accent : "transparent"
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }
}
