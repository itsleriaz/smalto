import QtQuick

// A one-of-many choice made of icon buttons (SegmentButton children): one
// shared track, the chosen one raised. Same look as `Segmented`, for
// choices that are pictures.
Rectangle {
    id: root

    default property alias content: row.data

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 30
    radius: Theme.radiusSmall
    color: Theme.controlTrack

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
    }
}
