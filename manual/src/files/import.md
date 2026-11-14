# Importing images

## Opening an image

**File → Open…** also opens **PNG**, **JPEG**, **BMP**, **TGA** and **GIF** files. The image becomes a new RGBA document. A **GIF** opens with all its frames.

> Not supported yet: WebP. JPEG's orientation tag is ignored.

### Numbered images

If you open one image of a numbered series (`walk1.png`, `walk2.png`…), Smalto asks:

> Open 8 numbered images as an animation?

- **Open as Animation** (the accent button) makes one sprite whose frames are the images.
- **Just This File** opens only the one you chose.

## Import Image as Layer

**File → Import Image as Layer…** puts a picture on a new layer of the current sprite. Importing a GIF this way takes its first frame only. The picture is converted to the sprite's color mode.

## Import Tileset Image

For sprite sheets of tiles, see [Importing a tileset image](../tilemaps-slices/tilemaps.md#importing-a-tileset-image).

## Paste

Anything you copy in another app can be pasted: **Edit → Paste** floats it on the canvas, **Paste as New Layer** puts it on its own layer, and **Paste as New Sprite** makes a new document from it.
