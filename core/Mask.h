#pragma once

#include "core/Geometry.h"
#include "core/Image.h"

#include <cstdint>
#include <vector>

namespace core {

// A set of pixels, e.g. the selection: one byte per pixel over its bounds,
// kept tight (bounds = the smallest rect holding every set pixel), so two
// masks of the same pixels compare equal. Coordinates may be negative.
class Mask {
public:
    Mask() = default;
    explicit Mask(Rect rect); // every pixel of rect

    bool isEmpty() const { return m_bounds.isEmpty(); }
    Rect bounds() const { return m_bounds; }
    bool contains(int x, int y) const;
    bool contains(Point p) const { return contains(p.x, p.y); }
    // True when every pixel of the bounds is set.
    bool isRect() const;

    Mask united(const Mask& other) const;
    Mask subtracted(const Mask& other) const;
    Mask intersected(Rect rect) const;
    Mask intersected(const Mask& other) const;
    // The pixels of `area` not in this mask.
    Mask inverted(Rect area) const;
    Mask translated(int dx, int dy) const;

    // As an image the core transforms can work on: Indexed8, 1 = set, over
    // the bounds. fromImage takes any non-zero pixel as set; `at` is where
    // the image's top-left goes.
    Image toImage() const;
    static Mask fromImage(const Image& image, Point at);

    bool operator==(const Mask&) const = default;

private:
    void normalize(); // shrinks to the set pixels

    Rect m_bounds;
    std::vector<uint8_t> m_bits; // row-major over m_bounds
};

} // namespace core
