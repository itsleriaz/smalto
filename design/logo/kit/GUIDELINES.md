# Smalto — Logo guidelines

## 1. The logo
**Idea:** *smalto* is Italian for the glass tiles of a mosaic, and pixel art is a kind of mosaic. The mark is a pixel S
set as fourteen tiles. The fourteenth tile, in coral, is still being placed: the next pixel, the next frame.

| Version | File | Use it for |
|---|---|---|
| Horizontal (primary) | `smalto-horizontal.svg` | README header, website, about box |
| Stacked | `smalto-stacked.svg` | Splash screens, square-ish spaces |
| Symbol | `smalto-symbol.svg` | Anywhere the name is already nearby, 33 px and up |
| Symbol, small cut | `smalto-symbol-small.svg` | 32 px and below: favicons, toolbars, tabs |
| Wordmark | `smalto-wordmark.svg` | Rare: when the symbol is shown separately on the same view |
| App icon | `smalto-app-icon.svg` (= `resources/icon/icon.svg`) | macOS / Windows app icon |
| Reversed | `*-reversed.svg` | On wine, near-black and dark photos |
| One colour | `png/*-black`, `*-white`, `*-mono-900c3f` | Single-colour print, stamps, engraving |

The wordmark is drawn on the same pixel grid as the symbol. Never retype "smalto" in a font as a stand-in.

## 2. Construction
- Symbol: 5 × 5 grid, unit **44**, gap (grout) **6**, tile corner radius **3**, on a 256 canvas.
- The coral tile sits at the top-right corner and is nudged **6 up and 6 right**. The whole grid is shifted by half
  that nudge, so the mark is optically centred.
- Wordmark: 7-row pixel grid with a 5-row x-height. In the horizontal lockup, one wordmark cell is half a tile pitch
  (22). The gap between the symbol and the wordmark is one tile pitch (44), and the wordmark's baseline sits on the
  bottom of the S.
- Reversed artwork uses tiles 1 unit smaller, because light shapes on dark grounds look heavier.
- `design/logo/build_kit.py` regenerates every SVG from these numbers.

## 3. Clear space
Keep at least **one tile pitch** (the width of a tile plus its gap) empty on every side. It scales with the logo.

## 4. Minimum size
| Version | Screen | Print |
|---|---|---|
| Horizontal | 120 px wide | 30 mm wide |
| Symbol (master) | 33 px | 8 mm |
| Symbol, small cut | 16 px | 4 mm |

## 5. Colour
| Name | HEX | RGB | CMYK (approx.) | Role |
|---|---|---|---|---|
| Wine | `#900C3F` | 144 12 63 | 0 92 56 44 | Primary: logo, app-icon field |
| Coral | `#E84A5F` | 232 74 95 | 0 68 59 9 | The moving tile, on light grounds |
| Coral light | `#FF847C` | 255 132 124 | 0 48 51 0 | The moving tile, on wine or dark grounds |
| Cream | `#FDF1E4` | 253 241 228 | 0 5 10 1 | Reversed logo, paper |
| Ink | `#1E1418` | 30 20 24 | 0 33 20 88 | Text next to the logo |

Pantone matches have not been chosen; pick them from a physical swatch book before printing.

**Approved pairs:** wine (+ coral) on white or cream · cream (+ coral light) on wine · cream (+ coral light) on
near-black · black on white · white on black.
Contrast: wine on white is 9.1:1, cream on wine 8.2:1. **Wine on near-black is only 2.1:1: always use the reversed
version on dark UI.** The coral tile has lower contrast (about 3.8:1 in both pairs). That's acceptable because it is
one tile in a mark, not text. In one-colour versions it takes the same colour as the rest; the nudge alone carries the
idea.

## 6. Don'ts
- Don't put the coral tile back in line with the rest of the grid, or give the nudge to another tile.
- Don't close the gaps on the master artwork (the small cut is the only gapless version).
- Don't use the master below 33 px; use the small cut.
- Don't add colours to the other tiles (no rainbow mosaic), gradients, shadows, gloss or outlines.
- Don't stretch, rotate or rearrange the lockups, or change the space between symbol and wordmark.
- Don't use wine artwork on dark backgrounds.

## 7. Files
- `kit/*.svg`: masters. `kit/png/`: PNG and one-colour exports. `kit/web/`: favicon.ico/svg, PNG icons,
  `site.webmanifest`, `head-snippet.html`.
- `kit/presentation.html`: rationale and mockups.
- App icon: `resources/icon/icon.svg`, rebuilt into `.icns`/`.ico` with `scripts/make-icons.sh`.

Trademark clearance has not been checked. Run a search before registering or using the mark commercially.
