# Effects

The **Effects** menu holds the tools that change the pixels of a cel. They open a floating panel that doesn't block the canvas: the picture previews the result live, and **Apply** commits it (**Cancel** or <kbd>Esc</kbd> closes it, **Reset** returns to the defaults). Every effect can be undone.

**Apply to** at the bottom of the panel chooses how much it changes: **This cel**, **Selected cels**, **This tag (layer)**, **This layer, all frames** or **Whole sprite**. The preview shows the current frame. With a selection, effects stay inside it.

| Effect | Key | What it does |
|---|---|---|
| **Outline…** | <kbd>Shift</kbd> + <kbd>O</kbd> | Draws an outline in a **Color** of your choice, **Outside** or **Inside** the shapes, with **Round** or **Square** corners. |
| **Replace Color…** | <kbd>Shift</kbd> + <kbd>R</kbd> | Swaps one color for another. **Tolerance** is how different a color may be and still be replaced. Pick the colors from the swatches, the canvas or the color panel. |
| **Hue / Saturation…** | <kbd>Ctrl</kbd> + <kbd>U</kbd> | Shifts **Hue**, **Saturation** and **Lightness**. |
| **Blur…** | | A **Gaussian** or **Box** blur with a **Radius**; **Edges** can **Keep shape** or **Spread**. |
| **Pixel Cleanup…** | | Fixes common clutter. Choose what to fix: **Orphan pixels** (a lone opaque pixel with no opaque neighbor is removed), **Pinholes** (an empty pixel with opaque pixels on all four sides, which takes their color), **Stray colors** (a pixel whose four neighbors all share another color takes it) and **Line doubles** (the extra pixel of an L-shaped step on a one-color 1-pixel line). |
| **Color Curves…** | | Adjusts brightness and contrast with a curve per **Channel** (RGB, R, G or B). Drag points on the curve; **Reset curves** straightens it. |
| **Posterize…** | | Limits each color channel to a number of **Levels**. |
| **Reduce Colors…** | | Cuts the sprite to a number of colors chosen from all its frames, with the same **Dither**, **Strength**, **Serpentine** and **OKLab** options as the [Color Mode](color-modes.md) dialog. Not available in indexed sprites. |
| **Drop Shadow…** | | A shadow with a **Color**, **Opacity** and an **Offset X** / **Offset Y**. |
| **Convolution Matrix…** | | A custom filter: a 3×3 or 5×5 grid of numbers. Start from a **Preset** (Sharpen, Sharpen more, Edge detect, Emboss, Soften, Identity); **Divide by** (auto: the sum of the kernel) and **Offset** (added to each channel afterwards) refine it. |
| **Quick Effects** | | Ready-made particle effects (and your own saved ones) added to the sprite in one step: see [Particle and smear layers](../layers-animation/live-smear.md). |

> **Note.** The shortcut keys above are the defaults from the shortcut table; the other effects have no key (Blur, Pixel Cleanup, Color Curves, Posterize, Drop Shadow and Convolution Matrix) and are unbound until you set them in **Preferences → Shortcuts**.
