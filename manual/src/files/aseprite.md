# Working with Aseprite files

Smalto's native format is **`.aseprite`**, the file format of [Aseprite](https://www.aseprite.org). You can open a file in Smalto, save it, and open it again in Aseprite (and the other way round) without converting anything.

> Smalto reads and writes the format from its **public specification** only. It contains no Aseprite code and isn't affiliated with Aseprite.

## What is kept

Smalto reads and writes:

- Color modes: RGBA, grayscale and indexed, with the palette.
- Layers and groups, with their blend modes (all 19), opacity, visibility and lock.
- Frames with their durations, cels (including linked cels) and cel positions.
- Tags with direction and repeat.
- Slices, with 9-slice centers and pivots.
- Tilemap layers and tilesets.
- Layer, cel and sprite user data.

Fully transparent cels are skipped and cels are trimmed to their opaque pixels when saving, as Aseprite does.

## What is kept without being understood

Anything in a file that Smalto doesn't model is kept and written back unchanged: unknown chunks (the color profile, later-frame palettes, and so on), user data, and layer flags it doesn't use. Opening and saving a file in Smalto doesn't strip things it can't edit.

## Smalto's own additions

- **Alpha lock** is stored in a layer flag that Aseprite doesn't use. Aseprite opens such a file fine but drops the lock when it saves.
- **Particle layers** and **Smear groups** are saved as ordinary layers (the pixels you see), plus extra data Smalto reads back to keep them editable. Other programs show the art as drawn, without the motion of a Smear group; use **Bake Layer** first to share the file.

## Limits

- Linked cels share their pixels, so linked cels with different z-index or opacity collapse to one value.
- Chunks that Smalto keeps without understanding are pinned to a frame number and don't move if you insert or remove frames.
- The HSL blend modes (Hue, Saturation, Color, Luminosity) can differ from Aseprite by up to 2 per color channel.
- Export always writes full-canvas frames, and overwrites existing numbered files and JSON without asking (only the main file is confirmed).

## Other programs and engines

For tools that don't read `.aseprite`, [export](export.md) a sprite sheet with its JSON (frames, tags and slices) or PNG frames. For tilemaps, see [Exporting a tileset](../tilemaps-slices/tilemaps.md#exporting-a-tileset): Tiled and Godot files can be written directly.
