#include "core/Pen.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace core {

double pressureLevel(const EasingCurve& curve, double pressure)
{
    if (!(pressure > 0)) // also NaN
        return 0;
    return std::clamp(evalEasing(curve, std::min(pressure, 1.0)), 0.0, 1.0);
}

int pressureSize(int size, double level, int minPercent)
{
    if (size <= 1)
        return 1;
    const double m = std::clamp(minPercent, 0, 100) / 100.0;
    const double t = m + (1 - m) * std::clamp(level, 0.0, 1.0);
    return std::clamp(int(std::lround(1 + (size - 1) * t)), 1, size);
}

namespace {

uint8_t channel(double v) { return uint8_t(std::clamp(std::lround(v), 0l, 255l)); }

} // namespace

Rgba mixRgba(Rgba low, Rgba high, double level)
{
    const double t = std::clamp(level, 0.0, 1.0);
    auto mix = [t](uint8_t a, uint8_t b) { return channel(a + (b - a) * t); };
    return {mix(low.r, high.r), mix(low.g, high.g), mix(low.b, high.b), mix(low.a, high.a)};
}

Rgba paintOver(Rgba base, Rgba paint, double strength)
{
    const double pa = paint.a / 255.0 * std::clamp(strength, 0.0, 1.0);
    const double ba = base.a / 255.0;
    const double a = pa + ba * (1 - pa);
    if (a <= 0)
        return {};
    auto mix = [&](uint8_t p, uint8_t b) { return channel((p * pa + b * ba * (1 - pa)) / a); };
    return {mix(paint.r, base.r), mix(paint.g, base.g), mix(paint.b, base.b), channel(a * 255)};
}

Rgba eraseFrom(Rgba base, double strength)
{
    const uint8_t a = channel(base.a * (1 - std::clamp(strength, 0.0, 1.0)));
    return a == 0 ? Rgba{} : Rgba{base.r, base.g, base.b, a};
}

int tiltAngle(double xTilt, double yTilt, int previous, int step)
{
    step = std::max(1, step);
    // Under about 8 degrees of lean the direction is mostly noise.
    if (std::hypot(xTilt, yTilt) < 8)
        return previous;
    const double raw = std::atan2(yTilt, xTilt) * 180 / std::numbers::pi;
    double diff = std::fmod(raw - previous + 540.0, 360.0) - 180.0; // -180..180
    if (std::abs(diff) < step * 0.75)
        return previous;
    int angle = int(std::lround(raw / step)) * step;
    angle = (angle % 360 + 360) % 360;
    return angle > 180 ? angle - 360 : angle;
}

bool Rope::pull(double tx, double ty, double length)
{
    const double dx = tx - x, dy = ty - y;
    const double d = std::hypot(dx, dy);
    if (d <= length || d == 0)
        return false;
    const double k = (d - std::max(0.0, length)) / d;
    x += dx * k;
    y += dy * k;
    return true;
}

} // namespace core
