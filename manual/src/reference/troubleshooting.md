# Troubleshooting

## Smalto quit and I lost work

Smalto copies unsaved sprites aside a few seconds after you stop editing and offers them back the next time you start it. See [When Smalto quits unexpectedly](../files/open-save.md#when-smalto-quits-unexpectedly). If it didn't offer anything, the copy interval may be off (**Preferences → Files → Crash recovery**), or nothing newer than the last copy exists. Restored documents come back without undo history.

## A tool does nothing or draws somewhere odd

- Check the **layer**: a hidden or locked layer can't be drawn on (the eye and padlock in the timeline). A group can't be drawn on either; pick a layer inside it.
- **Alpha lock** (the α next to the layer) keeps painting on existing pixels only.
- Is there a **selection**? Tools stay inside it. <kbd>Ctrl</kbd> + <kbd>D</kbd> deselects.
- On a **tilemap layer** in tile mode, the pencil places whole tiles; press <kbd>Alt</kbd> + <kbd>T</kbd> for pixel mode.
- With **Symmetry** or **Dither** on (their buttons are lit), strokes are mirrored or patterned.
- A paste or transform that is still **floating** needs <kbd>Enter</kbd> (or <kbd>Esc</kbd>).

## A shortcut doesn't work

Single-key shortcuts don't work while a text field has focus. Click the canvas first. Also check **Preferences → Shortcuts**: you may have rebound the key.

## The window opened off screen, or the layout is a mess

**Preferences → Panels → Reset panel layout** puts the panels back. <kbd>Shift</kbd> + <kbd>Tab</kbd> brings back panels you hid with it.

## The animation looks wrong in another program

- **Smear groups** and **particle layers** are only animated by Smalto; use **Layer → Bake Layer** before saving a file for other tools.
- **Alpha lock** is dropped when Aseprite saves the file.
- See [Working with Aseprite files](../files/aseprite.md).

## Export looks different from the canvas

Hidden layers aren't exported. A GIF has no partial transparency: alpha becomes on or off, and with more than 255 colors in all, each frame gets its own palette.

## Lospec palettes won't load

The Lospec browser needs an internet connection and uses an endpoint Lospec doesn't officially document; if it changes, browsing shows an error, while opening a palette by name or link usually still works. The built-in presets work offline.

## Drawing feels slow

A very large sprite with many layers and frames takes more work to redraw; hiding layers you don't need helps. Release builds are much faster than debug builds.

## "Smalto can't be opened" on macOS, or SmartScreen on Windows

The first releases aren't signed. See [Install](../getting-started/install.md).

## Where are my settings and files?

Smalto keeps settings and its recovery copies in your user data folder. Your sprites are wherever you saved them. To start fresh, quit Smalto and delete its settings.

## Reporting a bug

Please include your system and Smalto version (the **About Smalto** entry: in the Smalto menu on a Mac, in the File menu on Windows and Linux), what you did, and the file if you can share it.
