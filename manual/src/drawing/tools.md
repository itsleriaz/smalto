# Tools

The tools are in the rail on the left of the window, in groups. Pick one by clicking it or pressing its key (hover a tool to see its name and key). The options strip under the tabs shows that tool's options, and the **(i)** button at its right end shows the tool's gestures at a glance.

All keys below are defaults; see [All shortcuts](../reference/shortcuts.md).

## Drawing

| Tool | Key | What it does |
|---|---|---|
| **Pencil** | <kbd>B</kbd> | Draws with the primary color. The right mouse button draws with the secondary color. <kbd>Shift</kbd>-click draws a straight line from the last point. |
| **Eraser** | <kbd>E</kbd> | Erases to transparent (or the transparent index in an indexed sprite). Same brush options as the pencil. |
| **Fill** | <kbd>G</kbd> | Fills an area. See [Fill](text-gradient-fill.md#fill). |
| **Gradient** | <kbd>Shift</kbd> + <kbd>G</kbd> | Drags a gradient from the primary color to the secondary. See [Gradients](text-gradient-fill.md#gradients). |
| **Eyedropper** | <kbd>I</kbd> | Picks a color from the canvas. Left click sets the primary color, right click the secondary. |

**Picking colors without switching tools:** hold <kbd>Alt</kbd> (<kbd>Option</kbd> on a Mac) and click with the pencil, eraser or fill to pick the color under the cursor (right click: the secondary). <kbd>Ctrl</kbd>-click (<kbd>Cmd</kbd> on a Mac) selects the layer under the cursor.

## Soft brushes

| Tool | Key | What it does |
|---|---|---|
| **Blur** | <kbd>R</kbd> | Softens the pixels it passes over. Each pixel is blurred at most once per stroke. |
| **Jumble** | <kbd>J</kbd> | Shuffles the pixels it passes over with their neighbors, for texture and noise. A new pattern every stroke. |

Both stay inside the current selection.

## Shapes and text

| Tool | Key | What it does |
|---|---|---|
| **Line** | <kbd>L</kbd> | Drag to draw. <kbd>Shift</kbd> snaps to 45° steps. |
| **Rectangle** | <kbd>U</kbd> | <kbd>Shift</kbd> makes a square. The **Filled** option fills it. |
| **Ellipse** | <kbd>O</kbd> | <kbd>Shift</kbd> makes a circle. The **Filled** option fills it. |
| **Text** | <kbd>T</kbd> | See [Text](text-gradient-fill.md#text). |

For all three shapes, hold <kbd>Alt</kbd> (<kbd>Option</kbd>) to draw from the center, and use the right mouse button for the secondary color. While you drag, the size (width × height in pixels) shows next to the pointer.

## Selecting and moving

| Tool | Key | What it does |
|---|---|---|
| **Select** | <kbd>M</kbd> | Drag a rectangle. |
| **Lasso** | <kbd>Q</kbd> | Draw an outline freehand, or click its corners (polygon). |
| **Magic Wand** | <kbd>W</kbd> | Click to select pixels of the same color. |
| **Move** | <kbd>V</kbd> | Moves the selected pixels, or the whole cel when nothing is selected. |
| **Slice** | <kbd>C</kbd> | Marks named areas of the sprite. See [Slices](../tilemaps-slices/slices.md). |

See [Selecting and transforming](selection-transform.md) for details.

## Hand

The **Hand** (<kbd>H</kbd>) pans the view. You can also hold <kbd>Space</kbd> with any tool and drag, drag with the middle mouse button, or scroll with two fingers. Zoom with the wheel, a pinch or <kbd>+</kbd> / <kbd>-</kbd>; <kbd>0</kbd> fits the sprite to the window.

## Tools for particle and smear layers

On a particle layer the rail swaps to its own tools (**Emitter**, **Source**, **Field**); on a smear group it shows **Pose** and **Move**. See [Particle and smear layers](../layers-animation/live-smear.md).

## On a tilemap layer

In tile mode, the pencil, eraser and eyedropper work on whole tiles instead of pixels. See [Tilemaps](../tilemaps-slices/tilemaps.md).
