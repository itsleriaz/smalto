# Text, gradients and fills

## Fill

The **Fill** tool (<kbd>G</kbd>) paints an area with the primary color (right click: the secondary).

- **Contiguous** fills the connected area; **Global** fills every matching pixel of the cel.
- **Tolerance** is how different a color may be and still count (0 is exact; indexed sprites match exact indices).
- **This layer / All layers:** with **All layers**, Smalto finds the area in what every visible layer shows, and fills the current layer.
- The fill stays inside the selection, and **Dither** (see [Symmetry, dithering and shading](symmetry-dither-shading.md)) works with it.

## Gradients

The **Gradient** tool (<kbd>Shift</kbd> + <kbd>G</kbd>): drag across the selection (or the whole cel). The primary color is at the start and the secondary at the end. Make the secondary color transparent for a fade.

Options:

- **Shape:** Linear, Radial, Reflected, Diamond or Angle.
- **Palette:** run through the palette's selected range instead of from the primary to the secondary color (Shift-click swatches to pick a range; with none, the whole palette). Indexed sprites keep palette colors.
- **Dither:** Smooth, a 2 × 2, 4 × 4 or 8 × 8 pattern, or Blue noise. Dithered gradients use two colors only.
- **Repeat:** No repeat, Repeat or Mirror.
- **Own stops:** colors of your own anywhere along the gradient. A color bar appears: click the bar to add a stop (in the color the gradient has there), drag a marker to move it, click it to pick its color, right-click it to remove it.

After you drag, the gradient stays editable: drag its ends to change it, <kbd>Enter</kbd> keeps it and <kbd>Esc</kbd> takes it back. The gradient stays inside the selection and on an alpha-locked layer's own pixels.

## Text

The **Text** tool (<kbd>T</kbd>): click the canvas, then type. The text floats in the primary color.

- <kbd>Shift</kbd> + <kbd>Enter</kbd> starts a new line, <kbd>Enter</kbd> places the text, <kbd>Esc</kbd> drops it.
- Drag the text to move it before placing it. A click elsewhere starts a new text; a click on the text you just placed opens it again.
- Options: the **font** (pixel fonts), the **spacing** between letters and between lines (in pixels), and left, center or right **alignment**.
