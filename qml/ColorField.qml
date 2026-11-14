import QtQuick
import QtQuick.Layouts
import Smalto

// A color as a swatch and a hex field (#rrggbb, or #rrggbbaa with `alpha`).
// `edited(color)` when a valid color is typed; the owner writes it to `value`.
RowLayout {
    id: root

    property color value: "black"
    property bool alpha: false
    signal edited(color value)

    spacing: 6

    function hex(c: color): string {
        const part = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return "#" + part(c.r) + part(c.g) + part(c.b) + (alpha ? part(c.a) : "")
    }

    Rectangle {
        Layout.preferredWidth: 22
        Layout.preferredHeight: 22
        radius: 5
        color: root.value
        border.color: Theme.swatchBorder
        border.width: 1
    }
    AppTextField {
        id: field
        Layout.preferredWidth: root.alpha ? 96 : 84
        text: root.hex(root.value)
        selectAllOnFocus: true
        maximumLength: root.alpha ? 9 : 7
        font.features: { "tnum": 1 }
        validator: RegularExpressionValidator {
            regularExpression: root.alpha ? /^#?[0-9a-fA-F]{8}$/ : /^#?[0-9a-fA-F]{6}$/
        }
        onEditingFinished: if (acceptableInput) {
            const t = text.startsWith("#") ? text : "#" + text
            // #rrggbbaa is the order of this field; Qt reads #aarrggbb.
            const c = root.alpha ? Qt.color("#" + t.slice(7, 9) + t.slice(1, 7)) : Qt.color(t)
            root.edited(c)
            text = Qt.binding(() => root.hex(root.value))
        }
    }
}
