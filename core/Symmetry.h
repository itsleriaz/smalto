#pragma once

#include "core/Geometry.h"
#include "core/Image.h"
#include "core/Mask.h"

#include <vector>

namespace core {

// The 8 symmetries of a square (turns by 90° and mirrors), as used by the
// drawing symmetry. An element maps (x, y), relative to the center, by first
// swapping x and y (bit 2), then negating x (bit 0) and y (bit 1).
enum Mirror : int {
    MirrorNone = 0,
    MirrorLeftRight = 1,    // across the vertical axis
    MirrorTopBottom = 2,    // across the horizontal axis
    MirrorFalling = 4,      // across the "\" diagonal (-45°): (x, y) -> (y, x)
    MirrorRising = 7,       // across the "/" diagonal (45°): (x, y) -> (-y, -x)
};

// The element applied to (x, y).
Point applySymmetry(int element, Point v);
// a after b.
int composeSymmetry(int a, int b);
// Every element the generators produce together, identity first (1 to 8).
std::vector<int> symmetryGroup(const std::vector<int>& generators);

// `r` (pixels) mapped by `element` around `center2`, twice the center in
// pixels (so it can sit on a pixel edge or a pixel's middle). The result is
// a rect again, its width and height swapped by the diagonals. When it lands
// between pixels (a diagonal with the center's x and y half a pixel apart)
// it rounds toward the top left.
Rect mirrorRect(Rect r, int element, Point center2);

// `mask` mapped by `element` around `center2` (as mirrorRect does).
Mask mirroredMask(const Mask& mask, int element, Point center2);

// `mask` mirrored left to right (`mirror`) across the vertical line through the center, then turned by
// `radians` clockwise (y points down) round it. A pixel is set where its middle lands in a set pixel.
Mask turnedMask(const Mask& mask, double radians, bool mirror, Point center2);

// An image turned / mirrored by `element` (its width and height swapped by
// the diagonals): what a custom brush looks like in a mirrored copy.
Image orientedImage(const Image& image, int element);

} // namespace core
