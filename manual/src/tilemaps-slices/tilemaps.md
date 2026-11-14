# Tilemaps

A **tilemap layer** builds a picture out of reusable **tiles** from a **tileset**, the way many games do: draw each tile once, then place it as often as you like. Change a tile and every place it's used changes.

## Creating a tilemap layer

- **Layer → New Tilemap Layer…** starts a new, empty tilemap layer.
- **Layer → Convert to Tilemap…** (or right-click a layer) cuts an ordinary layer into tiles on the grid; identical tiles are kept once.

In the dialog, pick a **Tileset** (**New tileset**, or an existing one that other layers already use: layers sharing a tileset share its tiles, so editing a tile changes it on every one), give it a **Name**, and set the **Tile size**. **Base index** is the number the first tile shows as. Tile size starts at your grid's size. **Convert to Normal Layer** turns a tilemap layer back into pixels.

## Pixel mode and tile mode

A tilemap layer has two ways of working, switched with <kbd>Alt</kbd> + <kbd>T</kbd>:

- **Pixel mode:** you draw pixels as usual, and Smalto keeps the tiles up to date. The options strip has **Manual** (edits only existing tiles, wherever they're used), **Auto** (edits tiles in place; an empty cell gets a matching tile or a new one) and **Stack** (each painted cell gets a new tile).
- **Tile mode:** the tools work on whole tiles. The **Pencil** places the picked tile (right-click clears), the **Eraser** clears cells, the **Eyedropper** picks the tile under the cursor, **Fill** fills cells showing the same tile, and **Line**, **Rectangle** and **Ellipse** draw with the picked tile. **Select** snaps to whole cells, and **Move** drags the selected cells (hold <kbd>Alt</kbd> to copy them).

## The tileset panel

In tile mode a **Tileset** panel shows the tiles. Click a tile to pick it, or **Shift**-click to pick a block (a *stamp*) to place at once. The panel's buttons:

- **Flip horizontally / vertically / turn:** place tiles flipped or turned (with both flips, any quarter turn).
- **Size and columns:** the grid's cell size and number of columns (<kbd>Ctrl</kbd> + scroll over it changes the size).
- **New tile**, **duplicate**, **edit** (opens the tile in its own tab, repeating around itself so you can check that it tiles; double-clicking a tile does too), **move earlier / later**, **delete** (where it's used, the cells become empty).
- **Tidy:** **Remove unused tiles** and **Merge duplicate tiles**. Unused tiles are dimmed, and the panel says how many times the picked tile is used.

You can drag tiles to reorder them.

## Importing a tileset image

**File → Import Tileset Image…** cuts a picture into tiles. Set the **Tile size** (the button next to it guesses it from the picture), and the **Margin** around the tiles and the **Spacing** between them. Options:

- **Skip empty tiles:** fully transparent tiles are left out.
- **Merge identical tiles:** equal tiles are made once.
- **Place the tiles as in the picture:** the new layer starts with the tiles laid out like the picture.

## Exporting a tileset

With a tilemap layer current, **File → Export Tileset…** writes the tiles as a PNG, with options for **Columns**, **Margin** and **Spacing**, and optionally:

- **Tiled** files: a tileset (`.tsx` or JSON `.tsj`) and a map (`.tmx` or JSON `.tmj`) of every tilemap layer of that tileset on the current frame.
- A **Godot 4** `TileSet` resource (`.tres`, for Godot 4.3 or newer): an atlas source with every tile, pointing at the PNG. Put the files in your project and set the folder in the dialog.
- A **CSV** of tile numbers per tilemap layer, numbered as Tiled does (0 is empty, the first tile is 1).

Layers with another tile size stay out of a map; the dialog says so.
