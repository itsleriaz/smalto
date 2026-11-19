# Importing images

## Opening an image

**File → Open…** also opens **PNG**, **JPEG**, **BMP**, **TGA** and **GIF** files. The image becomes a new RGBA document. A **GIF** opens with all its frames.

> Not supported yet: WebP. JPEG's orientation tag is ignored.

### Numbered images

If you open one image of a numbered series (`walk1.png`, `walk2.png`…), Smalto asks:

> Open 8 numbered images as an animation?

- **Open as Animation** (the accent button) makes one sprite whose frames are the images.
- **Just This File** opens only the one you chose.

## Import Sprite Sheet

**File → Import Sprite Sheet…** cuts a picture into tiles and makes them the frames of a new sprite. It cuts the picture in the current tab; **Select File…** opens another picture (in a tab of its own) to cut instead. The sheet itself is never changed.

| Option | What it does |
|--------|--------------|
| **Type** | **Horizontal Strip** (one row), **Vertical Strip** (one column), **By Rows** or **By Columns**: the order the tiles become frames in. |
| **X**, **Y**, **Width**, **Height** | The first tile's box. The other tiles follow it. |
| **Columns**, **Rows** | How many tiles. They fill in from the box and the picture's size; type to override. |
| **Padding** | Space between neighbouring tiles, across (**Pad X**) and down (**Pad Y**). |
| **Include partial tiles at bottom/right edges** | A tile cut off by the picture's edge counts as a tile. What is missing is transparent. |
| **Skip empty tiles** | Fully transparent tiles are left out. |

The tiles are outlined on the canvas, and the first one has handles. Drag a side or corner to size it (**Shift** keeps its shape), drag inside it to move it. The fields change with it, and so do the other tiles. Nothing is made until **Import**.

## Import Folder as Animation

**File → Import Folder as Animation…** makes one sprite from a whole folder of images. Smalto looks in the folder and its subfolders:

- Images that share a folder and a name but for the number at its end (`walk_1.png`, `walk_2.png`, `walk_10.png`) are one animation, in numeric order.
- Any other image is an animation of one frame.
- An animation with no name of its own (`run/1.png`, `run/2.png`) is named for its folder.

Each animation becomes a **tag** over its frames. The dialog lists what it found: untick an animation to leave it out, or rename its tag. A GIF in the folder adds all its frames. The canvas fits the largest image; smaller ones sit in its top left.

## Import Image as Layer

**File → Import Image as Layer…** puts a picture on a new layer of the current sprite. Importing a GIF this way takes its first frame only. The picture is converted to the sprite's color mode.

## Import Tileset Image

For sprite sheets of tiles, see [Importing a tileset image](../tilemaps-slices/tilemaps.md#importing-a-tileset-image).

## Paste

Anything you copy in another app can be pasted: **Edit → Paste** floats it on the canvas, **Paste as New Layer** puts it on its own layer, and **Paste as New Sprite** makes a new document from it.
