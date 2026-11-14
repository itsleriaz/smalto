#pragma once

#include "core/Color.h"
#include "core/Easing.h"

namespace core {

// Pen (tablet) dynamics and stroke stabilizing: the math only, no input handling.

// Pressure (0..1, clamped) through the pressure curve (x: the pressure, y: the level), clamped to 0..1.
double pressureLevel(const EasingCurve& curve, double pressure);

// The brush size at a pressure level (0..1): `size` at full pressure, down to `minPercent` (0..100) of
// it at none, never under 1 px.
int pressureSize(int size, double level, int minPercent);

// The color between `low` (level 0) and `high` (level 1), straight alpha.
Rgba mixRgba(Rgba low, Rgba high, double level);

// `paint` laid over `base` at `strength` (0..1, times the paint's own alpha), source-over.
Rgba paintOver(Rgba base, Rgba paint, double strength);

// `base` with `strength` (0..1) of its alpha taken away (a partial erase).
Rgba eraseFrom(Rgba base, double strength);

// The way a pen leans (Qt's tilts in degrees: x toward the right, y toward the bottom of the tablet) as a
// brush angle in degrees, clockwise on screen, 0 = leaning right, in -179..180 and in steps of `step`.
// `previous` is kept while the pen stands nearly upright (the direction is noise then) and while the
// lean stays within most of a step of it, so the brush doesn't flicker between two steps.
int tiltAngle(double xTilt, double yTilt, int previous, int step = 5);

// A stabilizer on a string: the brush (x, y) follows the pointer only when the pointer is more than
// `length` away, and then by just enough to be `length` behind it. Shaky input inside the string's
// reach does nothing and curves come out smooth; the brush lags the pointer by `length`.
struct Rope {
    double x = 0;
    double y = 0;

    // Pulls the brush toward the pointer at (tx, ty); true if it moved.
    bool pull(double tx, double ty, double length);
};

} // namespace core
