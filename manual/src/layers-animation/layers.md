# Layers and groups

Every sprite is a stack of layers; the topmost is drawn in front. Smalto shows them in two places: the **timeline**, whose left column lists the layers with their controls, and an optional compact **Layers** panel (<kbd>F8</kbd>) that is handy for work without frames.

## Working with layers

| To… | Do this |
|---|---|
| Add a layer | <kbd>Shift</kbd> + <kbd>N</kbd>, the **+** next to the layer controls, or **Layer → New Layer** |
| Add a group | The folder-plus button, or **Layer → New Group** |
| Pick a layer | Click it. <kbd>Ctrl</kbd>-click (<kbd>Cmd</kbd> on a Mac) on the canvas with a drawing tool picks the layer under the cursor. |
| Rename | Double-click its name (or right-click → **Rename**) |
| Reorder | Drag a layer up or down; drop it between layers, or onto a group to put it inside |
| Duplicate | Right-click → **Duplicate Layer** |
| Show / hide | The eye. A hidden layer isn't drawn on the canvas or exported. |
| Lock | The padlock. A locked layer can't be edited. |
| Delete | The trash button, or **Layer → Delete Layer** |

## Blend mode and opacity

Each layer has a **blend mode** and an **opacity** (0 to 100%), set in the timeline's layer controls, or in the Layers panel when it's shown. Smalto supports all 19 Aseprite blend modes (Normal, Multiply, Screen, Overlay and so on). A background layer is fixed: its mode and opacity can't change.

## Alpha lock

The **α** next to an image layer locks its transparency: painting stays on the layer's own pixels, so you can recolor shapes without spilling over their edges. It's also in **Layer → Alpha Lock** (<kbd>Shift</kbd> + <kbd>A</kbd>).

## Groups

A group holds layers and is drawn as one: its blend mode and opacity apply to the result. Select layers (<kbd>Shift</kbd> or <kbd>Ctrl</kbd> + click) and press <kbd>Ctrl</kbd> + <kbd>G</kbd> to group them; <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>G</kbd> ungroups. The chevron collapses or expands a group.

## Copying layers between documents

Right-click → **Copy Layer**, then **Paste Layer** in the same or another document.

## Clearing and deleting

**Clear Layer** empties the current layer's pixels on the current frame; **Delete Layer** removes the layer itself. Both are in the **Layer** menu, in red, and both can be undone with <kbd>Ctrl</kbd> + <kbd>Z</kbd>.

## Other kinds of layers

- **Tilemap layers** hold tiles instead of pixels: see [Tilemaps](../tilemaps-slices/tilemaps.md).
- **Particle layers** and **Smear groups** are live: see [Particle and smear layers](live-smear.md).
- **Import Image as Layer** (**File** menu) adds a picture as a new layer.
