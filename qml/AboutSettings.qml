pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Smalto

// Preferences > About: the app, its version and license, and credits for
// what it bundles. Links open in the browser.
Flickable {
    id: root

    // Name, what it is and its license, and a link (may be "").
    readonly property var credits: [
        { name: "Qt 6", what: qsTr("Application framework, LGPL-3.0, linked dynamically"), link: "https://www.qt.io" },
        { name: "Lucide", what: qsTr("Icons, ISC license"), link: "https://lucide.dev" },
        { name: "misc-fixed fonts", what: qsTr("Text tool fonts by Markus Kuhn and the X11 contributors, public domain"),
          link: "https://www.cl.cam.ac.uk/~mgk25/ucs-fonts.html" },
        { name: "zlib 1.3.1", what: qsTr("Compression for .aseprite files, zlib license"), link: "https://zlib.net" },
        { name: "stb_image", what: qsTr("Image import, by Sean Barrett, public domain / MIT"),
          link: "https://github.com/nothings/stb" },
        { name: qsTr("Built-in palettes"),
          what: qsTr("PICO-8 (Lexaloffle Games), Sweetie 16 (GrafxKid), DawnBringer 16 and 32 (DawnBringer), "
                     + "Endesga 32 and 64 (ENDESGA), Game Boy (the BGB emulator)"),
          link: "https://lospec.com/palette-list" },
        { name: "Lospec", what: qsTr("The palette browser loads palettes from lospec.com"), link: "https://lospec.com" },
    ]

    contentWidth: width
    contentHeight: column.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar {}

    ColumnLayout {
        id: column
        width: root.width - 12
        spacing: 14

        ColumnLayout {
            spacing: 2
            Text {
                text: "Smalto"
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Version %1").arg(Qt.application.version)
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("A pixel art editor and animator.")
            color: Theme.text
            font.pixelSize: Theme.fontSize
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Free software under the GNU General Public License, version 3 (see the "
                       + "LICENSE file; https://www.gnu.org/licenses/gpl-3.0.html), with no warranty. "
                       + "Aseprite files are read and written from Aseprite's public file format "
                       + "specification; Smalto isn't affiliated with Aseprite.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.separator
        }

        Text {
            text: qsTr("Made with")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Repeater {
            model: root.credits
            delegate: ColumnLayout {
                id: credit
                required property var modelData
                Layout.fillWidth: true
                spacing: 1
                // The name opens the project's page (underlined on hover).
                Text {
                    text: credit.modelData.name
                    color: nameArea.containsMouse ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontSize
                    font.weight: Font.DemiBold
                    font.underline: nameArea.containsMouse
                    MouseArea {
                        id: nameArea
                        anchors.fill: parent
                        enabled: credit.modelData.link !== ""
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally(credit.modelData.link)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: credit.modelData.what
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
