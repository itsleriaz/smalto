#include "core/Gradient.h"

#include <algorithm>
#include <cmath>

namespace core {

double repeatPosition(double t, GradientRepeat repeat)
{
    switch (repeat) {
    case GradientRepeat::Off:
        return std::clamp(t, 0.0, 1.0);
    case GradientRepeat::Repeat:
        return t - std::floor(t);
    case GradientRepeat::Mirror: {
        const double m = t - 2 * std::floor(t / 2); // 0 <= m < 2
        return m <= 1 ? m : 2 - m;
    }
    }
    return 0;
}

double gradientPosition(GradientShape shape, Point a, Point b, int x, int y, GradientRepeat repeat)
{
    const double dx = double(b.x - a.x), dy = double(b.y - a.y);
    const double length2 = dx * dx + dy * dy;
    if (length2 <= 0)
        return 0;
    const double px = x - a.x, py = y - a.y;
    const double length = std::sqrt(length2);
    double t = 0;
    switch (shape) {
    case GradientShape::Linear:
        t = (px * dx + py * dy) / length2;
        break;
    case GradientShape::Radial:
        t = std::sqrt(px * px + py * py) / length;
        break;
    case GradientShape::Reflected:
        t = std::abs(px * dx + py * dy) / length2;
        break;
    case GradientShape::Diamond: {
        const double along = (px * dx + py * dy) / length;
        const double across = (-px * dy + py * dx) / length;
        t = (std::abs(along) + std::abs(across)) / length;
        break;
    }
    case GradientShape::Conical: {
        constexpr double kTwoPi = 6.283185307179586;
        double turn = (std::atan2(py, px) - std::atan2(dy, dx)) / kTwoPi;
        turn -= std::floor(turn); // 0 <= turn < 1
        return turn;
    }
    }
    return repeatPosition(t, repeat);
}

RampStep rampStep(double t, int stops)
{
    if (stops < 2)
        return {0, 0};
    const double s = std::clamp(t, 0.0, 1.0) * (stops - 1);
    int lower = int(std::floor(s));
    if (lower >= stops - 1)
        lower = stops - 2;
    return {lower, s - lower};
}

RampStep rampStepAt(double t, const std::vector<double>& positions)
{
    const int n = int(positions.size());
    if (n < 2)
        return {0, 0};
    if (t <= positions[0])
        return {0, 0};
    if (t >= positions[size_t(n) - 1])
        return {n - 2, 1};
    int lower = 0;
    while (lower < n - 2 && t >= positions[size_t(lower) + 1])
        ++lower;
    const double span = positions[size_t(lower) + 1] - positions[size_t(lower)];
    return {lower, span > 0 ? (t - positions[size_t(lower)]) / span : 0};
}

} // namespace core
