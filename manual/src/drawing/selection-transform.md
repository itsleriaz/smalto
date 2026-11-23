# Selecting and transforming

## Making a selection

- **Select** (<kbd>M</kbd>): drag a rectangle. <kbd>Shift</kbd> while dragging makes it a square, <kbd>Alt</kbd> (<kbd>Option</kbd>) draws it from the center; its size shows next to the pointer. Double-click with the grid shown to select one grid cell.
- **Lasso** (<kbd>Q</kbd>): in **Freehand** mode, drag around what to select (hold <kbd>Alt</kbd> for a straight segment); the outline closes itself. In **Polygon** mode, click the corners, then click the first corner, double-click or press <kbd>Enter</kbd> to finish. <kbd>Backspace</kbd> removes the last corner, <kbd>Esc</kbd> cancels, and <kbd>Shift</kbd> on later corners snaps to 45°.
- **Magic Wand** (<kbd>W</kbd>): click to select the pixels of that color. **Contiguous** selects the connected area, **Global** every matching pixel. **Tolerance** is how different a color may be and still count. **All layers** looks at what every visible layer shows instead of the current layer.

**Combining selections:** hold <kbd>Shift</kbd> when you start a selection to **add**, <kbd>Alt</kbd> (<kbd>Option</kbd>) to **subtract**, or both to **intersect**.

Click once outside a selection to deselect. Other selection commands:

| Command | Key |
|---|---|
| Select all | <kbd>Ctrl</kbd> + <kbd>A</kbd> |
| Deselect | <kbd>Ctrl</kbd> + <kbd>D</kbd> (or <kbd>Esc</kbd>) |
| Invert selection | <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>I</kbd> |
| Clear the selected pixels | <kbd>Delete</kbd> |

Painting tools stay inside the selection.

## Moving

Drag inside a selection to move its pixels, or use the **Move** tool (<kbd>V</kbd>), which moves the whole cel when nothing is selected. The arrow keys nudge the selected pixels by one pixel (<kbd>Shift</kbd>: 10 pixels); <kbd>Alt</kbd> (<kbd>Option</kbd>) + arrows moves only the selection outline.

## Copy, cut and paste

<kbd>Ctrl</kbd> + <kbd>C</kbd>, <kbd>X</kbd> and <kbd>V</kbd> copy, cut and paste pixels, or frames and cels if you clicked the timeline last. **Copy Merged** (<kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd>) copies what all visible layers show. **Paste as New Layer** and **Paste as New Sprite** put the clipboard in a new layer or document.

With the pointer over the canvas, a paste lands centered under it; from the menu, or with the pointer elsewhere, it goes back where it was copied from (an image from another app is centered). **Paste in Place** (<kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>V</kbd>) always puts it back where it was copied from. **Paste as New Layer** follows the pointer the same way.

A paste floats: drag it to move it, press <kbd>Enter</kbd> (or click elsewhere) to place it, <kbd>Esc</kbd> to cancel.

## Transform

**Transform** (<kbd>Ctrl</kbd> + <kbd>T</kbd>) scales and rotates the selection, or the layer's pixels when nothing is selected, and any floating paste.

- Drag the **square handles** to scale (<kbd>Shift</kbd> keeps the proportions) and the **round handle** to rotate (<kbd>Shift</kbd>: 15° steps). Drag inside to move.
- The options strip shows the **width** and **height** (turn on **Proportions** to keep the ratio), the percentage, the **angle**, and a nine-point **pivot** that stays put when rotating or typing a size.
- Flip with <kbd>Shift</kbd> + <kbd>H</kbd> and <kbd>Shift</kbd> + <kbd>V</kbd>, and turn 90° with <kbd>Ctrl</kbd> + <kbd>]</kbd> and <kbd>Ctrl</kbd> + <kbd>[</kbd>.
- <kbd>Enter</kbd> (or the check button) applies; <kbd>Esc</kbd> or the **×** cancels.

## Brush from selection

The brush button in the selection tools' options (or <kbd>Ctrl</kbd> + <kbd>B</kbd>) turns the selected pixels into a [custom brush](brushes.md#custom-brushes).
