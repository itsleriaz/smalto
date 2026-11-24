# Frames, tags and onion skin

## The timeline

The timeline is a grid: layers are rows, frames are columns, and each dot is a **cel**, the pixels of one layer on one frame. Tags are the colored bars above the frame numbers. <kbd>Tab</kbd> shows and hides it.

Along its top you'll find, from the left:

1. **Previous frame**, **play / pause** and **next frame** (<kbd>←</kbd>, <kbd>Enter</kbd>, <kbd>→</kbd>).
2. **New frame**, **duplicate frames**, **delete frames**, and the **frame duration** in milliseconds.
3. **Onion skin** with its settings.
4. The frame count ("Frame 1 / 4").

## Frames

| To… | Do this |
|---|---|
| Add a frame | <kbd>Alt</kbd> + <kbd>N</kbd> or the **+** button |
| Duplicate | <kbd>Alt</kbd> + <kbd>D</kbd> or the copy button |
| Select frames | Click or drag over the frame numbers; <kbd>Shift</kbd> extends |
| Move frames | Drag the edge of the selected frame numbers (or <kbd>Cmd</kbd>-drag anywhere in them) |
| Delete | The trash button (a sprite always keeps one frame) |
| Reverse a range | Right-click the frame numbers → **Reverse Frames** |
| Frame duration | The ms field sets it for the current frame, or for the selected frames. Right-click → **Frame Duration…** for a dialog. |

Press <kbd>Enter</kbd> to play the animation; the **Preview** panel plays it at its own zoom: scroll over it to zoom about the pointer, drag to pan, double-click to fit it again. The Preview panel's own play button plays it there without moving the canvas; when both play, they stay in step (whichever started second joins the other on its frame).

## Cels

Click and drag in the grid to select a block of cels; <kbd>Shift</kbd> extends it. A block of cels is outlined; whole frames light up in the frame numbers instead, so the two are easy to tell apart. To move a selection, drag its edge (the pointer becomes a move cursor there) or <kbd>Cmd</kbd>-drag anywhere inside it; a plain drag inside starts a new selection. Hold <kbd>Alt</kbd> / <kbd>Option</kbd> when you let go to copy instead.

**Delete**, **Cut**, **Copy**, **Paste**, **Select All**, **Esc** and the arrow keys act on what you clicked last: the timeline or the canvas. The timeline's selection is in the accent color while it's the one they act on, and gray once you click the canvas. With the canvas clicked last and no pixels selected, Delete does nothing. Right-click for **Link Cels** (cels that share their pixels, so editing one changes all), **Unlink Cels**, **Cut**, **Copy** and **Paste**, and **Clear Cels** (in red).

## Tags

A tag names a range of frames, so one file can hold several animations (*walk*, *idle*…).

1. Select frames and right-click → **New Tag from Frames**.
2. Double-click the tag (or right-click → **Edit Tag…**) to set its **Name**, **Color**, **Direction** (Forward, Reverse, Ping-pong, Ping-pong reverse) and **Repeat** (a number of plays, or ∞ for a loop).
3. Drag a tag's ends to resize it and its middle to move it.

Tags are saved in the `.aseprite` file and can be exported one per row of a sprite sheet. **Delete Tag** is in the tag's right-click menu.

## Onion skin

**Onion skin** (<kbd>F3</kbd>) draws the frames around the current one faintly under or over your drawing, so you can line up motion. The chevron next to it opens the settings:

- **Before** and **After:** how many frames on each side.
- **Opacity** of the ghost frames, and a **Tint** (red for before, blue for after, or off).
- **Drawn:** behind or in front of the current frame.
- **Loop within the tag:** while a tag is playing, the frames before its first and after its last are the ones at its other end.
