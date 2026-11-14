#pragma once

#include "core/Image.h"
#include "core/Mask.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace core {

// Calls plot(Point) for every pixel of the line from a to b, inclusive.
template <class F>
void bresenham(Point a, Point b, F&& plot)
{
    const int dx = std::abs(b.x - a.x);
    const int dy = -std::abs(b.y - a.y);
    const int sx = a.x < b.x ? 1 : -1;
    const int sy = a.y < b.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(a);
        if (a == b)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            a.x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            a.y += sy;
        }
    }
}

enum class BrushShape : uint8_t { Square, Round };

// Rectangle covered by a brush of `size` centered on p (either shape).
inline Rect brushRect(Point p, int size)
{
    return {p.x - size / 2, p.y - size / 2, size, size};
}

// The brush's pixels, row by row: for each row of brushRect the first and
// last column covered (relative to the rect). A round brush is the filled
// ellipse in its size x size square, as the ellipse tool draws it (size 3 is
// a plus); it's symmetric both ways, like the square one.
const std::vector<std::pair<int, int>>& brushSpans(int size, BrushShape shape);

// Stamps a brush. Returns the affected rect (clipped).
Rect stampBrush(Image& image, Point center, int size, uint32_t value, BrushShape shape = BrushShape::Square);

// --- custom brushes ---------------------------------------------------------------

// A brush made of pixels (a custom brush), in the format of the image it
// stamps into. Its handle is the center, as with the built-in brushes.
struct ImageBrush {
    Image pixels;
    Image footprint; // Indexed8, same size: 1 where the brush covers

    int width() const { return pixels.width(); }
    int height() const { return pixels.height(); }
    // The rect it covers centered on p: [p - size/2, p - size/2 + size) both ways.
    Rect rectAt(Point p) const { return {p.x - width() / 2, p.y - height() / 2, width(), height()}; }
};

// The brush mirrored left-right and / or top-bottom (for symmetry).
ImageBrush flipped(const ImageBrush& brush, bool horizontally, bool vertically);

// Stamps the brush's own pixels where its footprint covers. Returns the
// affected rect (clipped).
Rect stampImageBrush(Image& image, Point center, const ImageBrush& brush);
// The same on an RGBA image, but a semi-transparent brush pixel is laid over what is there (normal
// blend) instead of replacing it.
Rect stampImageBrushBlended(Image& image, Point center, const ImageBrush& brush);
// Stamps the brush's shape in one value.
Rect stampImageBrush(Image& image, Point center, const ImageBrush& brush, uint32_t value);
// Shades the brush's shape (see shadeBrush).
Rect shadeImageBrush(Image& image, const Image& before, Point center, const ImageBrush& brush,
                     const std::vector<uint32_t>& ramp, int step);

// Bounds of the pixels with any alpha in an RGBA image (empty if none).
Rect opaqueBounds(const Image& rgba);

// --- dithering -------------------------------------------------------------------

// Which pixels a dithered tool paints, anchored to the canvas (document
// coordinates), so separate strokes line up.
struct DitherPattern {
    enum Kind : uint8_t {
        Checker,         // every other pixel
        Ordered,         // Bayer 4 x 4: `density` of 16 pixels (8 = Checker)
        HorizontalLines, // every other row
        VerticalLines,   // every other column
        DiagonalLines,   // every fourth diagonal ("/")
    };
    Kind kind = Checker;
    int density = 8; // Ordered only, 0..16
};

// Whether the pattern paints document pixel (x, y).
bool ditherOn(const DitherPattern& pattern, int x, int y);

// The dither pass over `area` of a stroke's image: each pixel the stroke
// covered (`covered`, Indexed8, non-zero) that the pattern skips gets
// `offValue`, or its value in `before` without one. `origin` is the image's
// position in the document. Pixels the stroke didn't cover aren't touched.
void applyDither(Image& image, const Image& before, const Image& covered, Rect area, Point origin,
                 const DitherPattern& pattern, std::optional<uint32_t> offValue);

// --- shading ink ----------------------------------------------------------------

// The shading ink's new value for a pixel: `value` moved `step` entries along
// `ramp` (pixel values in the image's format), clamped to its ends. Nothing if
// `value` isn't in the ramp. A value listed twice counts at its first entry.
std::optional<uint32_t> shadeValue(uint32_t value, const std::vector<uint32_t>& ramp, int step);

// Stamps a brush in shading ink: every brush pixel gets the shade of its value
// in `before` (the image as it was when the stroke began), so stamping the same
// pixel again in one stroke never shifts it twice. `before` has the image's
// size. Returns the affected rect (clipped).
Rect shadeBrush(Image& image, const Image& before, Point center, int size, const std::vector<uint32_t>& ramp, int step,
                BrushShape shape = BrushShape::Square);

// Flood fill from start. With contiguous = false every matching pixel is
// replaced. tolerance (0..255) applies per RGBA channel (grayscale: value
// and alpha); indexed images match exact indices. Only pixels inside `clip` change (empty = whole image),
// and with a `mask` (image coordinates) only its pixels: the fill doesn't cross the others.
// Returns the affected rect.
Rect floodFill(Image& image, Point start, uint32_t value, bool contiguous, int tolerance = 0, Rect clip = {},
               const Mask* mask = nullptr);

// The pixels a fill from `start` would reach (the magic wand), as a mask in
// image coordinates; same matching as floodFill.
Mask similarMask(const Image& image, Point start, bool contiguous, int tolerance = 0);

// Like similarMask, but with `wrapX` / `wrapY` the image is a torus on that axis:
// a contiguous area continues across the opposite edge (the tiled view).
Mask similarMaskWrapped(const Image& image, Point start, bool contiguous, int tolerance, bool wrapX, bool wrapY);

// The pixels inside the closed outline through `points` (the lasso), the
// outline included, clipped to `clip`.
Mask polygonMask(const std::vector<Point>& points, Rect clip);

// Alpha lock: inside `area`, undoes what an edit did to pixels that were
// transparent in `before` and to ones it made transparent, and gives the
// rest their alpha from `before` (indexed: only the first two). `transparent`
// is the image's transparent value (the transparent index for indexed).
void keepAlpha(Image& image, const Image& before, Rect area, uint32_t transparent);

// --- shapes -------------------------------------------------------------------

// Inclusive rectangle spanned by two corner points. With `square` the shorter
// side grows to match the longer one (toward the drag direction); with
// `fromCenter`, `a` is the center instead of a corner.
Rect shapeRect(Point a, Point b, bool square, bool fromCenter);

// End point snapped so the line from a is horizontal, vertical or 45 degrees.
Point snapLine(Point a, Point b);

// Calls plot(Point) for every pixel on the outline of `r` (once each).
template <class F>
void rectOutline(Rect r, F&& plot)
{
    if (r.isEmpty())
        return;
    for (int x = r.x; x < r.right(); ++x) {
        plot(Point{x, r.y});
        if (r.h > 1)
            plot(Point{x, r.bottom() - 1});
    }
    for (int y = r.y + 1; y < r.bottom() - 1; ++y) {
        plot(Point{r.x, y});
        if (r.w > 1)
            plot(Point{r.right() - 1, y});
    }
}

// Calls plot(Point) for the outline of the ellipse inscribed in `r`, which
// touches all four sides (any width and height, odd or even). Pixels may
// repeat. Based on Alois Zingl's "A Rasterizing Algorithm for Drawing Curves"
// (ellipse in a rectangle).
template <class F>
void ellipseOutline(Rect r, F&& plot)
{
    if (r.isEmpty())
        return;
    int64_t x0 = r.x, x1 = r.right() - 1, y0 = r.y, y1 = r.bottom() - 1;
    int64_t a = x1 - x0, b = y1 - y0, b1 = b & 1;
    int64_t dx = 4 * (1 - a) * b * b, dy = 4 * (b1 + 1) * a * a;
    int64_t err = dx + dy + b1 * a * a;
    y0 += (b + 1) / 2;
    y1 = y0 - b1;
    a *= 8 * a;
    b1 = 8 * b * b;
    do {
        plot(Point{int(x1), int(y0)});
        plot(Point{int(x0), int(y0)});
        plot(Point{int(x0), int(y1)});
        plot(Point{int(x1), int(y1)});
        const int64_t e2 = 2 * err;
        if (e2 <= dy) {
            ++y0;
            --y1;
            err += dy += a;
        }
        if (e2 >= dx || 2 * err > dy) {
            ++x0;
            --x1;
            err += dx += b1;
        }
    } while (x0 <= x1);
    while (y0 - y1 <= b) { // flat ellipses: finish the tips
        plot(Point{int(x0 - 1), int(y0)});
        plot(Point{int(x1 + 1), int(y0++)});
        plot(Point{int(x0 - 1), int(y1)});
        plot(Point{int(x1 + 1), int(y1--)});
    }
}

// Horizontal extent [first, last] of a filled ellipse in `r`, per row from
// r.y to r.bottom() - 1.
std::vector<std::pair<int, int>> ellipseSpans(Rect r);

// --- pixel-perfect strokes ------------------------------------------------

// Removes the L-shaped corners a 1px freehand stroke leaves, so diagonals
// stay one pixel thin. Feed it every pixel of the stroke in order; when it
// returns a point, that pixel should be put back as it was.
class PixelPerfectFilter {
public:
    std::optional<Point> push(Point p);

private:
    Point m_prev; // two pixels back
    Point m_last; // last kept pixel
    int m_count = 0;
};

} // namespace core
