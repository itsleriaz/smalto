# Slices

A **slice** is a named rectangle on the sprite. Slices mark things other tools need to know about: regions of a sprite sheet, **9-slice** panels, hitboxes, anchor points. They are saved in the `.aseprite` file and in the sprite sheet's JSON.

## Making and editing slices

Pick the **Slice** tool (<kbd>C</kbd>):

- **Drag** on empty canvas to make a slice.
- **Click** a slice to pick it, **drag inside** it to move it, **drag a handle** to resize it.
- <kbd>Delete</kbd> removes the picked slice.

The options strip then shows, for the picked slice: a **list** of all slices, its **name** (type to rename), its **color** (the pipette button sets it to the primary color), and a trash button. Slice colors are only for telling slices apart.

## 9-slice and pivot

- **9-slice:** a center that stretches while the corners stay. Turn it on and drag its lines on the canvas. Game engines use this for panels and buttons that must resize.
- **Pivot:** a point of the slice, such as where a character stands. Turn it on and drag it on the canvas.

## Slices over time

Turn on **Key per frame** to let a slice change during the animation: an edit on a frame then gives the slice its own *key* there. **Remove key** appears on a frame that has one; the earlier key applies again.

## Showing slices

The grid popup in the status bar has a **Slices** setting to show or hide slices on the canvas.

## Exporting

Slices are written to the `meta.slices` section of a sprite sheet's JSON, as Aseprite does, and kept in saved `.aseprite` files.
