# Palettes

## The color panel

The **Color** panel has the **primary** and **secondary** colors (the two overlapping swatches; <kbd>X</kbd> swaps them), the color picker and a **hue** bar, and fields for the primary color: a **hex** code (`#rgb`, `#rrggbb` or `#rrggbbaa`) and **R, G, B, A** numbers. The picker keeps the alpha.

## The palette

Below the picker, the **Palette** section shows your swatches:

- **Click** a swatch to make it the primary color; **right-click** to make it the secondary.
- **Drag** swatches to reorder them. In an indexed sprite, moving a color moves its pixels with it.
- **Shift-click** several swatches to pick a **range**, used by Shading and by gradients that follow the palette.

The buttons in the section header:

| Button | What it does |
|---|---|
| Sliders | **Palette tools** (below) |
| Palette | **Presets, load and save** (below) |
| **+** | Add the primary color to the palette |
| Replace | Set the selected swatch to the primary color |
| Trash | Remove the selected swatch (red on hover) |

The chevron collapses the palette.

## Choosing a palette

The palette button opens a popup with two tabs:

- **Presets:** built-in palettes (from [Lospec](https://lospec.com/palette-list), each credits its author) and your own saved ones. Click one to use it. **Save as Preset…** keeps the current palette under a name; saved presets can be renamed or removed.
- **Lospec:** browse and search lospec.com's palette list (needs an internet connection). You can also paste a palette's name or link.

The folder and save buttons load a palette from a file (`.gpl`, `.hex`, `.pal`, `.act`, `.aseprite`, `.png`) and save the palette as `.gpl`, `.hex`, `.pal` or `.act`.

In an indexed sprite, the popup asks what should happen to the pixels, and previews the sprite in the new palette as you point at one:

- **Keep indices:** each pixel keeps its number, so it takes that number's new color.
- **Match colors:** each pixel goes to the new color nearest to what it is now.

## Palette tools

The sliders button opens **Palette tools**. They act on the picked range, or on the whole palette:

- **Ramp:** **Make ramp** fills the colors between the first and last picked ones with an even ramp; **Insert** adds new colors (you choose how many), a ramp from the first picked color to the last, before the last one. **Hue: Straight** or **Hue shift** decides whether the ramp turns the hue.
- **Sort** by hue, saturation or lightness, or **Reverse**.
- **Clean up:** remove **Unused** colors that no pixel uses, or merge **Duplicates**.
- **Reduce:** keep only the number of colors you pick; the closest ones merge first. **On canvas** shows the result on the canvas while you choose.

Every palette change can be undone.
