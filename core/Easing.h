#pragma once

namespace core {

// How a value gets from its start to its end, as a cubic bezier from (0, 0) to (1, 1) with two handles
// (the curve's x is time, its y how far along). The named easings are exactly such curves:
// linear (1/3, 1/3) (2/3, 2/3), in t^2 (1/3, 0) (2/3, 1/3), out 1 - (1 - t)^2 (1/3, 2/3) (2/3, 1),
// in-out 3t^2 - 2t^3 (1/3, 0) (2/3, 1). A handle's y may go past 0..1 (an overshoot); its x is kept
// within 0..1 so time never runs back.
struct EasingCurve {
    double x1 = 1.0 / 3, y1 = 1.0 / 3;
    double x2 = 2.0 / 3, y2 = 2.0 / 3;

    bool operator==(const EasingCurve&) const = default;
};

// The curve's y at time t (0..1).
double evalEasing(const EasingCurve& curve, double t);

// A named easing (0 linear, 1 in, 2 out, 3 in-out) as its curve.
EasingCurve namedEasing(int ease);

} // namespace core
