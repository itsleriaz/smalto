# Glossary

**9-slice**
: A slice whose center stretches while its corners stay the same size; used for resizable panels and buttons in games. See [Slices](../tilemaps-slices/slices.md).

**Alpha**
: How opaque a pixel is, from fully transparent to fully opaque.

**Alpha lock**
: A layer setting that keeps painting on the layer's existing pixels only. See [Layers](../layers-animation/layers.md#alpha-lock).

**Bake**
: To turn a live layer (particles or a Smear group) into an ordinary image layer holding what it showed on every frame.

**Canvas**
: The area where you draw; the sprite's size.

**Cel**
: The pixels of one layer on one frame: one dot of the timeline grid.

**Color mode**
: How a sprite stores color: RGBA, grayscale or indexed. See [Color modes](../color/color-modes.md).

**Dither**
: Painting or converting with a pattern of two colors so they look like a blend, or so a limited palette hides banding.

**Frame**
: One picture of an animation: one column of the timeline.

**Group**
: A layer that holds other layers and is drawn as one.

**Indexed color**
: A mode where every pixel is a number pointing into the palette, so changing a palette color changes every pixel using it.

**Linked cels**
: Cels that share their pixels, so editing one changes all of them.

**Live layer**
: A layer that makes pixels from settings (a particle layer or a Smear group) rather than storing drawn pixels.

**Onion skin**
: Showing the frames around the current one faintly, so you can line up motion.

**Palette**
: The list of colors a sprite (or you) can pick from.

**Pivot**
: A point that stays still when something rotates or scales; also a slice's anchor point.

**Pixel perfect**
: A drawing option that removes the extra corner pixels of a one-pixel freehand stroke.

**Primary and secondary color**
: The two colors you paint with: left mouse button and right mouse button. <kbd>X</kbd> swaps them.

**Slice**
: A named rectangle on the sprite, for sprite sheet regions, 9-slices, hitboxes and anchors.

**Smear**
: A Smear group animates art with keys and adds a motion trail.

**Sprite**
: A document: its layers, frames, palette and settings. Smalto's file for it is `.aseprite`.

**Sprite sheet**
: One image holding every frame, with a JSON file saying where each is.

**Tag**
: A named range of frames, like *walk* or *idle*, with a play direction and repeat.

**Tile, tileset, tilemap**
: A tile is a small reusable picture; a tileset is the set of them; a tilemap layer arranges them on a grid.

**Tolerance**
: How different a color may be from the target and still count (fill, magic wand, replace color).
