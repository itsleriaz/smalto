import QtQuick
import Smalto

// The heading of a panel or a panel section: small, spaced capitals.
Text {
    color: Theme.textDim
    font.pixelSize: Theme.fontSizeSmall - 1
    font.weight: Font.DemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 0.8
    elide: Text.ElideRight
}
