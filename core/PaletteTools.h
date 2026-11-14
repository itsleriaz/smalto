#pragma once

#include "core/Color.h"

#include <cstdint>
#include <vector>

namespace core {

struct ColorCount {
    Rgba color;
    uint64_t count; // weight, > 0
};

// Median cut: splits the box with the widest channel at its weighted median
// until there are `maxColors` boxes (or nothing left to split), then takes
// each box's weighted average (opaque), in box order. Alpha is ignored.
std::vector<Rgba> medianCut(std::vector<ColorCount> colors, int maxColors);

// OKLab (perceptual lightness L 0..1 and two opponent axes).
struct Oklab {
    float L = 0, a = 0, b = 0;
};
Oklab toOklab(Rgba c);
Rgba fromOklab(Oklab c, uint8_t alpha = 255);

// `count` colors (>= 2) going evenly from `from` to `to` in OKLab, alpha
// linearly; the ends are exactly `from` and `to`.
//   Straight blend (default) goes through the middle of the OKLab color plane,
//   so two different hues lose saturation on the way.
//   hueArc keeps the chroma and turns the hue the short way around (OKLCh),
//   as hue-shifted ramps do: dark purple to light yellow passes through red
//   and orange. A gray end (little chroma) takes the other end's hue.
std::vector<Rgba> rampColors(Rgba from, Rgba to, int count, bool hueArc = false);

enum class PaletteSort : uint8_t {
    Hue,        // grays first (by lightness), then by hue from red
    Saturation, // least saturated first
    Lightness,  // darkest first (OKLab L)
};

// Stable sorted order: position i of the result holds colors[order[i]].
std::vector<int> sortOrder(const std::vector<Rgba>& colors, PaletteSort key);

// `colors` merged down to `maxColors`: the two closest (OKLab, alpha too)
// merge into their average, weighted by counts[i] + 1 (pixels using each;
// unused colors still count a little), until few enough are left. Near
// duplicates go first, unlike median cut, which suits a small palette. The
// kept colors are in the order of their first entry; `map` gives each old
// entry's new index. Unchanged (an identity map) when there are no more than
// `maxColors` colors already.
struct ColorReduction {
    std::vector<Rgba> colors;
    std::vector<int> map;
};
ColorReduction reduceColors(const std::vector<Rgba>& colors, const std::vector<uint64_t>& counts, int maxColors);

} // namespace core
