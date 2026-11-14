#pragma once

#include "core/Color.h"
#include "core/ColorConvert.h"
#include "core/Document.h"
#include "core/Image.h"
#include "core/Mask.h"
#include "core/Raster.h"

#include <array>
#include <vector>

namespace core {

// Whole-image effects (Edit > Outline, Replace Color, Adjust). Each reads
// `src` and returns a copy with the effect applied. With a `mask` (document
// coordinates; the image's top-left is at `origin`) only pixels inside it
// change. Pixels are read and written as colors through the document, so
// RGBA, grayscale and indexed images all work (indexed: nearest palette
// entry).

struct OutlineOptions {
    Rgba color;
    bool inside = false; // on the shape's own edge pixels instead of around it
    bool square = false; // diagonal neighbours count too (corners filled)
};
Image outline(const Document& doc, const Image& src, const OutlineOptions& options, const Mask* mask = nullptr,
              Point origin = {});

// Every pixel within `tolerance` (0..255, per RGBA channel) of `from`
// becomes `to`.
Image replaceColor(const Document& doc, const Image& src, Rgba from, Rgba to, int tolerance,
                   const Mask* mask = nullptr, Point origin = {});

// Hue turned by `hue` degrees (-180..180); saturation and lightness moved
// toward their ends by `saturation` / `lightness` percent (-100..100: -100
// is gray / black, 100 is full / white). Alpha is kept.
Image adjustHsl(const Document& doc, const Image& src, int hue, int saturation, int lightness,
                const Mask* mask = nullptr, Point origin = {});

// Blurs with a box or Gaussian kernel of `radius` pixels (1..16) each way,
// weighting colors by their alpha so transparent pixels don't darken edges.
// keepShape: every pixel keeps its alpha (empty ones stay empty), only the
// colors soften; otherwise the blur spreads into the empty pixels too.
Image blur(const Document& doc, const Image& src, int radius, bool gaussian, bool keepShape,
           const Mask* mask = nullptr, Point origin = {});

// Convolution with a `size` x `size` kernel (3 or 5, row by row): each
// color channel becomes sum(k * neighbour) / divisor + bias (divisor 0: the
// kernel's sum, or 1 if that's 0). Only colors change: every pixel keeps
// its alpha, empty ones stay empty, and an empty neighbour counts as the
// pixel itself (so edges don't darken).
Image convolve(const Document& doc, const Image& src, const std::vector<int>& kernel, int size, int divisor,
               int bias, const Mask* mask = nullptr, Point origin = {});

// Every colored pixel takes the nearest color of `palette` (entry 0 is its
// transparent one), spread by `dither`; each keeps its own alpha. For RGBA and
// grayscale images.
Image quantize(const Document& doc, const Image& src, const Palette& palette, IndexedDither dither,
               const Mask* mask = nullptr, Point origin = {}, const DitherOptions& options = {});

// Pixel cleanup, in this order:
//   orphans:  an opaque pixel with no opaque pixel among its 8 neighbours
//             goes away;
//   pinholes: an empty pixel with opaque pixels on all 4 sides takes their
//             most common color;
//   strays:   a pixel whose 4 neighbours all share one other color takes it;
//   doubles:  on one-color 1 px lines, the extra pixel of an L-step goes
//             (one with exactly two neighbours of its color, one across and
//             one up or down, and nothing on the diagonal between them), as
//             pixel perfect does while drawing; checked pixel by pixel, so a
//             staircase becomes a clean diagonal without breaking.
struct CleanupOptions {
    bool orphans = true;
    bool pinholes = true;
    bool strays = true;
    bool doubles = true;
};
Image cleanup(const Document& doc, const Image& src, const CleanupOptions& options, const Mask* mask = nullptr,
              Point origin = {});

// Posterize: each color channel snaps to the nearest of `levels` (2..16)
// evenly spaced values, so a sprite with many colors ends up with at most
// levels^3. Alpha is kept.
Image posterize(const Document& doc, const Image& src, int levels, const Mask* mask = nullptr, Point origin = {});

// Drop shadow: a copy of the shape's alpha, moved by (`dx`, `dy`) and
// painted in `color` (its alpha is the shadow's opacity), goes behind the
// pixels. Whatever falls outside the image is lost.
Image dropShadow(const Document& doc, const Image& src, Rgba color, int dx, int dy, const Mask* mask = nullptr,
                 Point origin = {});

// Color curves. A curve is control points (input x, output y, both 0..255)
// joined by a smooth monotone cubic; outside the first and last point it
// stays flat, and fewer than two points is the identity. `curveTable` gives
// the output for each input value.
struct CurvePoint {
    int x = 0;
    int y = 0;
};
using CurveTable = std::array<uint8_t, 256>;
CurveTable curveTable(std::vector<CurvePoint> points);

// Each color channel goes through `master`, then its own table; alpha is
// kept.
Image applyCurves(const Document& doc, const Image& src, const CurveTable& master, const CurveTable& red,
                  const CurveTable& green, const CurveTable& blue, const Mask* mask = nullptr, Point origin = {});

// Brush tools: change the brush's pixels around `center` (size and shape
// as the pencil's), reading `before` (the image when the stroke began), so
// stamping the same pixel again in one stroke gives the same result.
// Returns the affected rect (clipped).
//   blurBrush: each pixel becomes the alpha-weighted average of itself and
//   its 8 neighbours (alpha averages too, so edges soften).
//   jumbleBrush: each pixel takes the value of a neighbour picked by `seed`
//   and its position (a new seed per stroke).
// With a `footprint` (Indexed8, 1 where the brush covers, centered like an
// ImageBrush) the brush has that shape instead of size and shape: a turned brush.
Rect blurBrush(const Document& doc, Image& image, const Image& before, Point center, int size, BrushShape shape,
               const Image* footprint = nullptr);
Rect jumbleBrush(Image& image, const Image& before, Point center, int size, BrushShape shape, uint32_t seed,
                 const Image* footprint = nullptr);

// HSL helpers: h in degrees [0, 360), s and l in [0, 1].
struct Hsl {
    float h = 0, s = 0, l = 0;
};
Hsl toHsl(Rgba c);
Rgba fromHsl(Hsl c, uint8_t alpha = 255);

} // namespace core
