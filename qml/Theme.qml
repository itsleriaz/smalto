pragma Singleton
import QtQuick

// Design tokens. Components never hard-code colors or sizes. A theme is a
// palette family (Basic, Everforest, ...) with a dark and a light variant;
// `mode` picks the variant (Main.qml remembers it), System following the OS
// appearance live. Each theme brings its own accent colors. Everything else
// (hover, outlines, separators, ...) is derived from the variant's text and
// surface colors, so a theme only lists the colors that define it.
QtObject {
    // 0 System, 1 Dark, 2 Light.
    property int mode: 0
    readonly property bool dark: mode === 1 || (mode === 0 && Application.styleHints.colorScheme !== Qt.ColorScheme.Light)

    // The themes. Per variant: `bg` the window, `canvas` the area behind the
    // sprite, `panel` the floating panels, `raised` the chosen item of a
    // group, `tooltip`, the three text strengths, `danger` (destructive
    // buttons) and `error` (invalid input). `accents` are each
    // { name, dark, light }; the first is the theme's own. Text on an accent
    // fill is white (always, for Basic), or the window color on a pale accent.
    readonly property var themes: [
        {
            name: "Basic",
            dark: { bg: "#1b1b20", canvas: "#202026", panel: "#25252b", raised: "#3a3a44", tooltip: "#2c2c33",
                    text: "#e2e2e8", dim: "#a3a3ad", faint: "#707078", danger: "#ff8787", error: "#ff6b6b" },
            light: { bg: "#e6e6eb", canvas: "#dadae0", panel: "#fbfbfd", raised: "#ffffff", tooltip: "#fbfbfd",
                     text: "#1f1f25", dim: "#5c5c66", faint: "#91919b", danger: "#d63c3c", error: "#d93636" },
            fixedOnAccent: true,
            accents: [
                { name: "Blue", dark: "#6e8bff", light: "#4f6fe6" },
                { name: "Violet", dark: "#a07cff", light: "#7a55e0" },
                { name: "Pink", dark: "#f06ba8", light: "#d13f7d" },
                { name: "Red", dark: "#f2706b", light: "#d14545" },
                { name: "Orange", dark: "#f59a4a", light: "#cc6a14" },
                { name: "Green", dark: "#4fc38a", light: "#20905a" },
                { name: "Teal", dark: "#3fbfc7", light: "#138d96" },
            ]
        },
        {
            name: "Everforest",
            dark: { bg: "#232a2e", canvas: "#2d353b", panel: "#343f44", raised: "#4a555b", tooltip: "#3d484d",
                    text: "#d3c6aa", dim: "#a2aca1", faint: "#838a7d", danger: "#e38986", error: "#e67e80" },
            light: { bg: "#efebd4", canvas: "#e6e2cc", panel: "#fdf6e3", raised: "#fffbef", tooltip: "#fdf6e3",
                     text: "#5c6a72", dim: "#63727a", faint: "#859289", danger: "#af5f61", error: "#e66868" },
            accents: [
                { name: "Green", dark: "#a7c080", light: "#8da101" },
                { name: "Aqua", dark: "#83c092", light: "#35a77c" },
                { name: "Blue", dark: "#7fbbb3", light: "#3a94c5" },
                { name: "Yellow", dark: "#dbbc7f", light: "#dfa000" },
                { name: "Orange", dark: "#e69875", light: "#f57d26" },
                { name: "Red", dark: "#e67e80", light: "#f85552" },
                { name: "Purple", dark: "#d699b6", light: "#df69ba" },
            ]
        },
        {
            name: "Tokyo Night",
            dark: { bg: "#16161e", canvas: "#1a1b26", panel: "#24283b", raised: "#363b58", tooltip: "#292e42",
                    text: "#c0caf5", dim: "#9aa5ce", faint: "#68719b", danger: "#f7768e", error: "#f7768e" },
            light: { bg: "#d5d6db", canvas: "#e1e2e7", panel: "#ebecf0", raised: "#ffffff", tooltip: "#f3f3f6",
                     text: "#343b58", dim: "#58679e", faint: "#7f86a9", danger: "#cf2d62", error: "#c64343" },
            accents: [
                { name: "Blue", dark: "#7aa2f7", light: "#2e7de9" },
                { name: "Purple", dark: "#bb9af7", light: "#9854f1" },
                { name: "Cyan", dark: "#7dcfff", light: "#007197" },
                { name: "Teal", dark: "#73daca", light: "#118c74" },
                { name: "Green", dark: "#9ece6a", light: "#587539" },
                { name: "Orange", dark: "#ff9e64", light: "#b15c00" },
                { name: "Red", dark: "#f7768e", light: "#f52a65" },
            ]
        },
        {
            name: "Catppuccin",
            dark: { bg: "#181825", canvas: "#1e1e2e", panel: "#313244", raised: "#45475a", tooltip: "#313244",
                    text: "#cdd6f4", dim: "#a6adc8", faint: "#777c93", danger: "#f38ba8", error: "#f38ba8" },
            light: { bg: "#dce0e8", canvas: "#e6e9ef", panel: "#f4f5f9", raised: "#ffffff", tooltip: "#eff1f5",
                     text: "#4c4f69", dim: "#6b6e84", faint: "#8a8da0", danger: "#d20f39", error: "#d20f39" },
            accents: [
                { name: "Mauve", dark: "#cba6f7", light: "#8839ef" },
                { name: "Lavender", dark: "#b4befe", light: "#7287fd" },
                { name: "Blue", dark: "#89b4fa", light: "#1e66f5" },
                { name: "Teal", dark: "#94e2d5", light: "#179299" },
                { name: "Green", dark: "#a6e3a1", light: "#40a02b" },
                { name: "Peach", dark: "#fab387", light: "#fe640b" },
                { name: "Pink", dark: "#f5c2e7", light: "#ea76cb" },
                { name: "Red", dark: "#f38ba8", light: "#d20f39" },
            ]
        },
        {
            name: "Gruvbox",
            dark: { bg: "#1d2021", canvas: "#282828", panel: "#3c3836", raised: "#504945", tooltip: "#3c3836",
                    text: "#ebdbb2", dim: "#bdae93", faint: "#8e8171", danger: "#f6765a", error: "#fb4934" },
            light: { bg: "#ebdbb2", canvas: "#f2e5bc", panel: "#fbf1c7", raised: "#fdf6d8", tooltip: "#fbf1c7",
                     text: "#3c3836", dim: "#665c54", faint: "#928374", danger: "#9d0006", error: "#cc241d" },
            accents: [
                { name: "Orange", dark: "#fe8019", light: "#af3a03" },
                { name: "Yellow", dark: "#fabd2f", light: "#b57614" },
                { name: "Green", dark: "#b8bb26", light: "#79740e" },
                { name: "Aqua", dark: "#8ec07c", light: "#427b58" },
                { name: "Blue", dark: "#83a598", light: "#076678" },
                { name: "Purple", dark: "#d3869b", light: "#8f3f71" },
                { name: "Red", dark: "#fb4934", light: "#9d0006" },
            ]
        },
    ]
    property int themeIndex: 0
    readonly property var theme: themes[Math.max(0, Math.min(themes.length - 1, themeIndex))]
    readonly property var colors: dark ? theme.dark : theme.light

    readonly property color background: colors.bg
    readonly property color canvasBackground: colors.canvas
    readonly property color canvasBorder: Qt.rgba(text.r, text.g, text.b, dark ? 0.14 : 0.18)

    readonly property color panelSolid: colors.panel
    readonly property color panel: Qt.rgba(panelSolid.r, panelSolid.g, panelSolid.b, 0.94)
    readonly property color panelBorder: Qt.rgba(text.r, text.g, text.b, dark ? 0.06 : 0.08)
    readonly property color shadow: dark ? Qt.rgba(0, 0, 0, 0.38) : Qt.rgba(0, 0, 0, 0.16)
    readonly property color hover: Qt.rgba(text.r, text.g, text.b, dark ? 0.06 : 0.05)
    readonly property color pressed: Qt.rgba(text.r, text.g, text.b, dark ? 0.11 : 0.10)
    readonly property color separator: Qt.rgba(text.r, text.g, text.b, dark ? 0.07 : 0.09)
    readonly property color tooltip: colors.tooltip
    // Controls in the options strips and dialogs: a pill / field sits on
    // `controlFill` with a `controlBorder` outline; a group of exclusive
    // choices sits in a `controlTrack` with the chosen one `controlRaised`.
    readonly property color controlFill: dark ? Qt.rgba(text.r, text.g, text.b, 0.04)
                                              : Qt.rgba(controlRaised.r, controlRaised.g, controlRaised.b, 0.9)
    readonly property color controlBorder: Qt.rgba(text.r, text.g, text.b, 0.16)
    readonly property color controlTrack: Qt.rgba(text.r, text.g, text.b, dark ? 0.07 : 0.065)
    readonly property color controlRaised: colors.raised

    // The checkerboard behind transparent pixels when it follows the theme
    // (Preferences > Canvas): two steps of the panel color toward the text.
    readonly property color checkerA: Qt.tint(panelSolid, Qt.rgba(text.r, text.g, text.b, dark ? 0.05 : 0.03))
    readonly property color checkerB: Qt.tint(panelSolid, Qt.rgba(text.r, text.g, text.b, dark ? 0.14 : 0.12))

    // The chosen theme's accents (Preferences > General), each with a dark and
    // a light value (the light ones are deeper so text on them stays readable).
    readonly property var accents: theme.accents
    property int accentIndex: 0
    // Each theme remembers its own accent: one index per theme.
    property var accentChoices: themes.map(() => 0)
    onThemeIndexChanged: accentIndex = accentChoices[themeIndex] ?? 0
    onAccentIndexChanged: {
        if (accentChoices[themeIndex] === accentIndex)
            return
        const choices = accentChoices.slice()
        choices[themeIndex] = accentIndex
        accentChoices = choices
    }
    readonly property var accentPreset: accents[Math.max(0, Math.min(accents.length - 1, accentIndex))]
    readonly property color accent: dark ? accentPreset.dark : accentPreset.light
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.22 : 0.16)
    readonly property color accentTint: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.12 : 0.09)
    // Text and icons on accent fills.
    readonly property color textOnAccent: theme.fixedOnAccent === true || (0.299 * accent.r + 0.587 * accent.g + 0.114 * accent.b) < 0.6
                                          ? "white" : colors.bg

    readonly property color text: colors.text
    readonly property color textDim: colors.dim
    readonly property color textFaint: colors.faint

    readonly property color danger: colors.danger   // destructive buttons
    readonly property color error: colors.error     // invalid input
    readonly property color scrollHandle: Qt.rgba(text.r, text.g, text.b, dark ? 0.18 : 0.22)
    readonly property color swatchBorder: Qt.rgba(text.r, text.g, text.b, dark ? 0.08 : 0.12)
    readonly property color handleBorder: dark ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(0, 0, 0, 0.25)

    readonly property int radius: 14
    readonly property int radiusSmall: 8
    readonly property int spacing: 8
    readonly property int margin: 8 // the panels' distance from the window edge
    readonly property int buttonSize: 34
    readonly property int iconSize: 18

    readonly property int fontSize: 12
    readonly property int fontSizeSmall: 11

    readonly property int durationFast: 120
    readonly property int durationNormal: 200
}
