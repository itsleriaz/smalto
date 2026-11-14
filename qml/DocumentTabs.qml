pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Smalto

// One tab per open document, along the top of the window: click to switch,
// the x (or a middle click) to close, + for a new sprite. A dot marks unsaved
// changes. Scrolls sideways when the tabs don't fit.
Panel {
    id: root

    required property Editor editor

    // The tab under the pointer, for the one tooltip they share.
    property Item hoveredTab: null
    property int hoveredIndex: -1
    property bool closeHovered: false

    signal closeRequested(int index)
    signal closeOthersRequested(int index)
    signal closeToTheRightRequested(int index)
    signal newRequested()

    // While a tab is dragged: the gap it would land in (0..count), else -1.
    property int dropGap: -1
    // The gap at `x` in the list's content: before or after the tab there.
    function gapAt(x: real): int {
        const i = list.indexAt(x, list.height / 2)
        if (i < 0)
            return x <= 0 ? 0 : list.count
        const item = list.itemAtIndex(i)
        return x < item.x + item.width / 2 ? i : i + 1
    }

    bare: true // sits in the title bar
    implicitHeight: 30 + padding * 2
    // The width the tabs want: each takes `idealTabWidth`, and when the row
    // gives them less they shrink (down to `minTabWidth`) before it scrolls.
    // Not measured from the tabs, so shrinking can't feed back into it.
    readonly property real idealTabWidth: 170
    readonly property real minTabWidth: 90
    readonly property real tabWidth: {
        const n = list.count
        if (n === 0)
            return idealTabWidth
        const fit = (list.width - (n - 1) * list.spacing) / n
        return Math.max(minTabWidth, Math.min(idealTabWidth, fit))
    }
    implicitWidth: list.count * idealTabWidth + Math.max(0, list.count - 1) * list.spacing
                   + addButton.width + row.spacing + padding * 2
    padding: 0

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 4

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.editor.documents
            currentIndex: root.editor.currentDocument
            highlightFollowsCurrentItem: false
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            // Where a dragged tab will land (on the scrolling content).
            Rectangle {
                z: 2
                visible: root.dropGap >= 0
                readonly property Item after: root.dropGap > 0 ? list.itemAtIndex(root.dropGap - 1) : null
                x: (after ? after.x + after.width + list.spacing / 2 : 0) - width / 2
                width: 2
                height: list.height
                radius: 1
                color: Theme.accent
            }

            // Middle button, for the whole row: a click closes the tab under
            // it, a drag scrolls the row (and isn't a click). Declared in the
            // ListView, so they live on its scrolling content: the positions
            // already include contentX, and the drag goes by scene position.
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: point => {
                    const i = list.indexAt(point.position.x, point.position.y)
                    if (i >= 0)
                        root.closeRequested(i)
                }
            }
            DragHandler {
                target: null
                acceptedButtons: Qt.MiddleButton
                cursorShape: Qt.ClosedHandCursor // while dragging
                property real startX
                onActiveChanged: if (active) startX = list.contentX
                onCentroidChanged: if (active) {
                    const dx = centroid.scenePosition.x - centroid.scenePressPosition.x
                    list.contentX = Math.max(0, Math.min(list.contentWidth - list.width, startX - dx))
                }
            }

            delegate: Rectangle {
                id: tab
                required property var modelData
                required property int index
                readonly property bool current: index === root.editor.currentDocument
                readonly property bool showClose: current || area.containsMouse

                width: root.tabWidth
                height: ListView.view.height
                radius: Theme.radiusSmall
                color: current ? Theme.accentSoft : area.containsMouse ? Theme.hover : "transparent"

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    // The middle button is the row's (above). Left: click to
                    // switch, drag to reorder (it moves when let go); right:
                    // the tab menu.
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property real pressX
                    property bool dragging: false
                    property bool wasDrag: false
                    onPressed: mouse => {
                        pressX = mouse.x
                        dragging = false
                        wasDrag = false
                        if (mouse.button === Qt.RightButton) {
                            tabMenu.index = tab.index
                            tabMenu.path = tab.modelData.path
                            tabMenu.popup()
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || !(pressedButtons & Qt.LeftButton))
                            return
                        if (!dragging && Math.abs(mouse.x - pressX) > 6)
                            dragging = true
                        if (dragging)
                            root.dropGap = root.gapAt(mapToItem(list.contentItem, mouse.x, mouse.y).x)
                    }
                    onReleased: {
                        if (dragging && root.dropGap >= 0) {
                            const gap = root.dropGap
                            root.editor.moveDocument(tab.index, gap > tab.index ? gap - 1 : gap)
                        }
                        wasDrag = dragging
                        dragging = false
                        root.dropGap = -1
                    }
                    onCanceled: {
                        dragging = false
                        root.dropGap = -1
                    }
                    onContainsMouseChanged: {
                        if (containsMouse) {
                            root.hoveredTab = tab
                            root.hoveredIndex = tab.index
                        } else if (root.hoveredTab === tab) {
                            root.hoveredTab = null
                            root.hoveredIndex = -1
                        }
                    }
                    onClicked: mouse => {
                        if (mouse.button === Qt.LeftButton && !wasDrag)
                            root.editor.currentDocument = tab.index
                    }
                }

                // The current tab's accent underline.
                Rectangle {
                    visible: tab.current
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: parent.width - 20
                    height: 2
                    radius: 1
                    color: Theme.accent
                }


                RowLayout {
                    id: content
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 4
                    spacing: 6

                    Text {
                        text: tab.modelData.name
                        color: tab.current ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fontSize
                        font.weight: tab.current ? Font.DemiBold : Font.Normal
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    // Unsaved changes; the close button takes its place on hover.
                    Item {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        Rectangle {
                            anchors.centerIn: parent
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.accent
                            visible: tab.modelData.modified && !tab.showClose
                        }
                        IconButton {
                            id: closeButton
                            anchors.fill: parent
                            visible: tab.showClose
                            iconName: "x"
                            tip: "Close" + Shortcuts.hints.closeTab
                            onHoveredChanged: root.closeHovered = hovered
                            onClicked: root.closeRequested(tab.index)
                        }
                    }
                }
            }
        }

        AppMenu {
            id: tabMenu
            property int index: -1
            property string path
            AppMenuItem { text: qsTr("Close"); hint: Shortcuts.labels.closeTab; onTriggered: root.closeRequested(tabMenu.index) }
            AppMenuItem {
                text: qsTr("Close Others")
                enabled: root.editor.documents.length > 1
                onTriggered: root.closeOthersRequested(tabMenu.index)
            }
            AppMenuItem {
                text: qsTr("Close Tabs to the Right")
                enabled: tabMenu.index >= 0 && tabMenu.index < root.editor.documents.length - 1
                onTriggered: root.closeToTheRightRequested(tabMenu.index)
            }
            AppMenuSeparator {}
            AppMenuItem {
                text: Qt.platform.os === "osx" ? qsTr("Reveal in Finder")
                      : Qt.platform.os === "windows" ? qsTr("Show in Explorer") : qsTr("Open Folder")
                enabled: tabMenu.path !== ""
                onTriggered: root.editor.revealFile(tabMenu.path)
            }
        }

        AppToolTip {
            parent: root.hoveredTab ?? root
            readonly property var doc: root.editor.documents[root.hoveredIndex]
            text: doc ? (doc.path || doc.name) : ""
            active: root.hoveredTab !== null && !root.closeHovered
        }

        IconButton {
            id: addButton
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            iconName: "plus"
            tip: "New sprite" + Shortcuts.hints.newSprite
            onClicked: root.newRequested()
        }
    }
}
