# Indexed color and color modes

A sprite has one **color mode**, shown in the status bar (RGBA, Grayscale or Indexed). Click it, or choose **Sprite → Color Mode…**, to change it.

## The three modes

- **RGBA:** every pixel has its own red, green, blue and alpha. The usual choice, and the mode of images you import.
- **Grayscale:** every pixel is a brightness and an alpha.
- **Indexed:** every pixel is a *number* that points into the sprite's palette. Change a palette color and every pixel using it changes. Indexed sprites are the classic way to make pixel art with a limited palette, to recolor with palette swaps, and to export small indexed PNGs. One palette entry is the **transparent** one; it can't be removed.

## Converting

The **Color Mode** dialog has a live preview of the current frame; **Convert** changes every frame.

| Option | What it does |
|---|---|
| **Mode** | RGBA, Grayscale or Indexed |
| **Palette** (for indexed) | **Sprite's colors** (a palette made from the colors the sprite already uses), **Current** (the palette you have), or **Choose…** (pick a preset or load one) |
| **Colors** | At most this many colors besides transparent (fewer if the sprite has fewer). If there are more, Smalto reduces them with *median cut*. |
| **Keep see-through shades** | Partly transparent pixels become palette colors with alpha, instead of opaque ones |
| **Dither** | None, Ordered 2×2 / 4×4 / 8×8, Blue noise, Floyd–Steinberg, Atkinson, Sierra, Stucki or Riemersma. Dither spreads the error of the nearest-color choice so gradients don't band. |
| **Strength** | How much the dither spreads: 100% full, 0% none |
| **Serpentine** | Error diffusion runs back along every other row, which hides its streaks |
| **OKLab** | Find the nearest palette color the way colors *look* (OKLab), not by distance in RGB |

A note under the options says what will happen, for example "Each pixel takes the nearest of the palette's 16 colors."

Things to know:

- Converting to **indexed** drops partial transparency unless you turn on **Keep see-through shades**: semi-transparent pixels become opaque, and fully transparent ones become the transparent index.
- The error-diffusion dithers work on each image separately, so frames of an animation dither differently. The ordered dithers and blue noise are the same on every frame, so they're a better choice for animation.
- Converting to **grayscale** turns colors into their brightness and keeps alpha.
- Converting from indexed to RGBA gives pixels their palette colors.
- Every conversion can be undone.

## Changing the palette of an indexed sprite

Loading or choosing a palette in an indexed sprite asks whether pixels keep their numbers or are matched to the nearest new color: see [Palettes](palettes.md#choosing-a-palette).

## Indexed export

Export can write an **indexed PNG** (**Colors → Indexed if ≤ 256**): see [Exporting](../files/export.md).
