#pragma once

#include "core/Geometry.h"

#include <vector>

namespace core {

// The shapes of a gradient dragged from `a` to `b`: position 0 is at `a`, 1 at `b`'s distance.
enum class GradientShape {
    Linear,    // along the drag, flat beyond both ends
    Radial,    // by distance from `a`
    Reflected, // along the drag, mirrored on both sides of `a`
    Diamond,   // by |along| + |across| (a diamond turned with the drag)
    Conical,   // by the angle around `a`, starting in the drag's direction, once around
};
constexpr int kGradientShapes = 5;

// What a gradient does beyond its end: stay at the last color, start over, or run back.
enum class GradientRepeat { Off, Repeat, Mirror };
constexpr int kGradientRepeats = 3;

// Folds a position (any real number) into 0..1 the way `repeat` says.
double repeatPosition(double t, GradientRepeat repeat);

// Where pixel (x, y) is on the gradient from `a` to `b`, 0..1 (with a repeat, what the position
// beyond the end folds to). Nothing is defined for a == b (0). A conical gradient goes once
// around, so a repeat doesn't apply to it.
double gradientPosition(GradientShape shape, Point a, Point b, int x, int y,
                        GradientRepeat repeat = GradientRepeat::Off);

// A position `t` (0..1) over `stops` colors (two or more, evenly spaced): the stop below it and
// how far (0..1) it is towards the next one. At 1 it is the last-but-one stop, all the way.
struct RampStep {
    int lower = 0;
    double frac = 0;
};
RampStep rampStep(double t, int stops);

// The same for stops at `positions` (0..1, ascending, two or more): before the first it is the
// first color, after the last the last one.
RampStep rampStepAt(double t, const std::vector<double>& positions);

} // namespace core
