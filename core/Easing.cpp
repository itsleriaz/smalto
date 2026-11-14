#include "core/Easing.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

// One coordinate of the bezier from 0 to 1 with handles a and b, at s.
double bezier(double a, double b, double s)
{
    const double u = 1 - s;
    return 3 * u * u * s * a + 3 * u * s * s * b + s * s * s;
}

double slope(double a, double b, double s)
{
    const double u = 1 - s;
    return 3 * u * u * a + 6 * u * s * (b - a) + 3 * s * s * (1 - b);
}

} // namespace

double evalEasing(const EasingCurve& c, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const double x1 = std::clamp(c.x1, 0.0, 1.0), x2 = std::clamp(c.x2, 0.0, 1.0);
    // The s where the curve's x is t: Newton from t, bisection when it strays (x(s) never falls, the
    // handles' x being within 0..1).
    double s = t;
    for (int i = 0; i < 8; ++i) {
        const double d = slope(x1, x2, s);
        if (std::abs(d) < 1e-9)
            break;
        const double next = s - (bezier(x1, x2, s) - t) / d;
        if (next < 0 || next > 1)
            break;
        s = next;
    }
    if (std::abs(bezier(x1, x2, s) - t) > 1e-7) {
        double lo = 0, hi = 1;
        for (int i = 0; i < 60; ++i) {
            s = (lo + hi) / 2;
            (bezier(x1, x2, s) < t ? lo : hi) = s;
        }
    }
    return bezier(c.y1, c.y2, s);
}

EasingCurve namedEasing(int ease)
{
    switch (ease) {
    case 1: return {1.0 / 3, 0, 2.0 / 3, 1.0 / 3};
    case 2: return {1.0 / 3, 2.0 / 3, 2.0 / 3, 1};
    case 3: return {1.0 / 3, 0, 2.0 / 3, 1};
    default: return {};
    }
}

} // namespace core
