#pragma once

#include "core/Commands.h"

namespace core {

// Where an indexed sprite's palette comes from when converting to indexed.
enum class IndexedPalette : uint8_t {
    Current,    // keep the palette; pixels take the nearest entry
    FromSprite, // the sprite's own colors (see paletteFromSprite)
    Given,      // a palette handed in (index 0 should be the transparent entry)
};

// How pixels are spread over the palette's colors going to indexed.
enum class IndexedDither : uint8_t {
    None,      // every pixel takes the nearest color
    Floyd,     // error diffusion (Floyd-Steinberg): smooth, noisy
    Ordered,   // a 4 x 4 Bayer pattern: regular, stable between frames
    Atkinson,  // error diffusion that drops a quarter of the error: crisp, high contrast
    Bayer2,    // a 2 x 2 Bayer pattern: coarse
    Bayer8,    // an 8 x 8 Bayer pattern: fine
    BlueNoise, // a 64 x 64 blue noise tile: even and grainy, stable between frames
    Sierra,    // error diffusion over three rows (Sierra): smoother and less patterned than Floyd-Steinberg
    Stucki,    // error diffusion over three rows (Stucki): sharper, with long reach
    Riemersma, // the error follows a Hilbert curve (the last 16 errors, fading): no rows, little pattern
};

// What else shapes the dither. strength: 0..100 (100 is the full effect: patterns shift a color
// by up to +-24, diffusion passes on all of its error); serpentine: error diffusion runs back along
// every other row (not Riemersma); oklab: the nearest palette color is found in OKLab, which is
// closer to how colors look than distance in RGB.
struct DitherOptions {
    int strength = 100;
    bool serpentine = false;
    bool oklab = false;
};

// The blue noise tile's rank (0..4095, each exactly once) at a pixel; the tile
// repeats every 64 pixels. Made once, by void and cluster.
int blueNoiseRank(int x, int y);

// `image` (any format) with its pixels spread over `palette` by `dither`, as
// Indexed8 indices (see colorModeCommand): fully transparent pixels take
// `transparentIndex`, the rest the nearest opaque entry.
Image ditherToPalette(const Document& doc, const Image& image, bool background, const Palette& palette,
                      int transparentIndex, IndexedDither dither, bool keepAlpha = false,
                      const DitherOptions& options = {});

// The pattern's threshold at a pixel, 0..1 (Bayer2, Ordered = Bayer 4 x 4, Bayer8, BlueNoise;
// 0.5 for the other kinds): a shade t shows its "on" color where t exceeds this.
float ditherThreshold(IndexedDither pattern, int x, int y);

// Visible colors of every cel (alpha dropped, so as opaque colors) in
// reading order, layers bottom to top, each once. Stops after `limit`.
std::vector<Rgba> spriteColors(const Document& doc, size_t limit);

// A palette for the sprite as it looks: index 0 transparent, then its
// colors. Up to maxColors - 1 colors are kept exactly (in spriteColors
// order); more are reduced by median cut, weighted by pixel count, and
// sorted by luminance.
Palette paletteFromSprite(const Document& doc, int maxColors = 256, bool keepAlpha = false);

// Always true now (tilesets convert with the cels); kept for callers.
bool canChangeColorMode(const Document& doc);

// Converts every cel image to `target`. RGBA / grayscale to indexed maps each
// pixel to the nearest palette color (fully transparent pixels to the
// transparent index); partial alpha is dropped. Indexed to RGBA / grayscale
// looks pixels up in the palette; background layers stay opaque. Grayscale
// uses luminance and keeps alpha. nullptr if the sprite already is in that
// mode or can't change (canChangeColorMode).
// `dither` only matters when converting to indexed. With `onlyFrame` >= 0 only
// that frame's cels are converted: enough to render a preview of it, but the
// command leaves the sprite half converted, so never keep it. `maxColors`
// (with FromSprite) counts the transparent entry too; `given` is the palette
// for IndexedPalette::Given. `keepAlpha`: partly see-through pixels keep their
// transparency as palette entries with alpha (otherwise they become opaque).
// An indexed sprite is remade the same way when the palette is new (FromSprite,
// Given).
std::unique_ptr<SetColorModeCommand> colorModeCommand(const Document& doc, ColorMode target,
                                                      IndexedPalette palette = IndexedPalette::Current,
                                                      IndexedDither dither = IndexedDither::None,
                                                      int onlyFrame = -1, int maxColors = 256,
                                                      const Palette* given = nullptr, bool keepAlpha = false,
                                                      const DitherOptions& options = {});

} // namespace core
