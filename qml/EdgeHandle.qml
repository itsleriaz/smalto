import QtQuick
import Smalto

// A strip on a panel's edge that resizes it by dragging: a thin grip shows
// on hover. Place it over the edge (`x`, `y` and the length along it are the
// owner's); `horizontal` is a bar you drag sideways (a left or right edge),
// else up and down. `direction` is +1 when dragging toward larger screen
// coordinates makes the panel bigger (a right or bottom edge), -1 otherwise.
// It emits `resized` with the new value, kept between `from` and `to`; the
// owner writes it back to `value`.
Item {
    id: root

    property bool horizontal: true
    property int direction: 1
    property real value
    property real from: 0
    property real to: 10000
    property string tip

    signal resized(real value)

    readonly property bool active: hover.hovered || drag.active

    implicitWidth: horizontal ? 10 : 40
    implicitHeight: horizontal ? 40 : 10
    z: 20

    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? 3 : 26
        height: root.horizontal ? 26 : 3
        radius: 1.5
        color: root.active ? Theme.accent : Theme.textFaint
        opacity: root.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
    HoverHandler {
        id: hover
        cursorShape: root.horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
    DragHandler {
        id: drag
        target: null
        property real start
        onActiveChanged: if (active) start = root.value
        onCentroidChanged: if (active) {
            const travel = root.horizontal ? centroid.scenePosition.x - centroid.scenePressPosition.x
                                           : centroid.scenePosition.y - centroid.scenePressPosition.y
            root.resized(Math.max(root.from, Math.min(root.to, Math.round(start + root.direction * travel))))
        }
    }
    AppToolTip {
        text: root.tip
        active: hover.hovered && !drag.active && root.tip !== ""
    }
}
