#include "core/BlendMode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace core {

const char* blendModeName(BlendMode mode)
{
    static constexpr const char* names[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Color Dodge",
        "Color Burn", "Hard Light", "Soft Light", "Difference", "Exclusion", "Hue",
        "Saturation", "Color", "Luminosity", "Addition", "Subtract", "Divide",
    };
    const int i = int(mode);
    return i >= 0 && i < kBlendModeCount ? names[i] : "Unknown";
}

namespace {

int screen(int b, int s) { return b + s - mul255(b, s); }
int hardLight(int b, int s) { return s <= 127 ? mul255(b, 2 * s) : screen(b, 2 * s - 255); }

int softLight(int b, int s)
{
    const double cb = b / 255.0;
    const double cs = s / 255.0;
    double r;
    if (cs <= 0.5) {
        r = cb - (1 - 2 * cs) * cb * (1 - cb);
    } else {
        const double d = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : std::sqrt(cb);
        r = cb + (2 * cs - 1) * (d - cb);
    }
    return int(std::lround(r * 255));
}

int separable(BlendMode mode, int b, int s)
{
    switch (mode) {
    case BlendMode::Multiply: return mul255(b, s);
    case BlendMode::Screen: return screen(b, s);
    case BlendMode::Overlay: return hardLight(s, b);
    case BlendMode::Darken: return std::min(b, s);
    case BlendMode::Lighten: return std::max(b, s);
    case BlendMode::ColorDodge:
        if (b == 0) return 0;
        if (s == 255) return 255;
        return std::min(255, b * 255 / (255 - s));
    case BlendMode::ColorBurn:
        if (b == 255) return 255;
        if (s == 0) return 0;
        return 255 - std::min(255, (255 - b) * 255 / s);
    case BlendMode::HardLight: return hardLight(b, s);
    case BlendMode::SoftLight: return softLight(b, s);
    case BlendMode::Difference: return std::abs(b - s);
    case BlendMode::Exclusion: return b + s - 2 * mul255(b, s);
    case BlendMode::Addition: return std::min(255, b + s);
    case BlendMode::Subtract: return std::max(0, b - s);
    case BlendMode::Divide:
        if (b == 0) return 0;
        if (b >= s) return 255;
        return b * 255 / s;
    default: return s;
    }
}

// Non-separable modes, W3C compositing spec.
struct Vec3 {
    double c[3];
};

double lum(const Vec3& v) { return 0.3 * v.c[0] + 0.59 * v.c[1] + 0.11 * v.c[2]; }

Vec3 clipColor(Vec3 v)
{
    const double l = lum(v);
    const double n = std::min({v.c[0], v.c[1], v.c[2]});
    const double x = std::max({v.c[0], v.c[1], v.c[2]});
    for (double& c : v.c) {
        if (n < 0)
            c = l + (c - l) * l / (l - n);
        if (x > 1)
            c = l + (c - l) * (1 - l) / (x - l);
    }
    return v;
}

Vec3 setLum(Vec3 v, double l)
{
    const double d = l - lum(v);
    for (double& c : v.c)
        c += d;
    return clipColor(v);
}

double sat(const Vec3& v)
{
    return std::max({v.c[0], v.c[1], v.c[2]}) - std::min({v.c[0], v.c[1], v.c[2]});
}

Vec3 setSat(Vec3 v, double s)
{
    int mx = 0, mn = 0;
    for (int i = 1; i < 3; ++i) {
        if (v.c[i] > v.c[mx]) mx = i;
        if (v.c[i] < v.c[mn]) mn = i;
    }
    if (mx == mn)
        return {{0, 0, 0}};
    const int md = 3 - mx - mn;
    v.c[md] = (v.c[md] - v.c[mn]) * s / (v.c[mx] - v.c[mn]);
    v.c[mx] = s;
    v.c[mn] = 0;
    return v;
}

Rgba nonSeparable(BlendMode mode, Rgba b, Rgba s)
{
    const Vec3 cb{{b.r / 255.0, b.g / 255.0, b.b / 255.0}};
    const Vec3 cs{{s.r / 255.0, s.g / 255.0, s.b / 255.0}};
    Vec3 r;
    switch (mode) {
    case BlendMode::Hue: r = setLum(setSat(cs, sat(cb)), lum(cb)); break;
    case BlendMode::Saturation: r = setLum(setSat(cb, sat(cs)), lum(cb)); break;
    case BlendMode::Color: r = setLum(cs, lum(cb)); break;
    default: r = setLum(cb, lum(cs)); break; // Luminosity
    }
    auto to8 = [](double c) { return uint8_t(std::clamp(std::lround(c * 255), 0L, 255L)); };
    return {to8(r.c[0]), to8(r.c[1]), to8(r.c[2]), s.a};
}

Rgba normal(Rgba b, Rgba s, int opacity)
{
    const int sa = mul255(s.a, opacity);
    if (sa == 0)
        return b;
    if (b.a == 0 || sa == 255)
        return {s.r, s.g, s.b, uint8_t(sa)};
    const int bw = mul255(b.a, 255 - sa); // backdrop weight
    const int ra = sa + bw;
    auto ch = [&](int bc, int sc) { return uint8_t((sc * sa + bc * bw + ra / 2) / ra); };
    return {ch(b.r, s.r), ch(b.g, s.g), ch(b.b, s.b), uint8_t(ra)};
}

} // namespace

Rgba blend(Rgba dst, Rgba src, int opacity, BlendMode mode)
{
    if (mode == BlendMode::Normal || dst.a == 0)
        return normal(dst, src, opacity);

    Rgba mixed;
    if (mode >= BlendMode::Hue && mode <= BlendMode::Luminosity) {
        mixed = nonSeparable(mode, dst, src);
    } else {
        mixed = {uint8_t(separable(mode, dst.r, src.r)), uint8_t(separable(mode, dst.g, src.g)),
                 uint8_t(separable(mode, dst.b, src.b)), src.a};
    }
    // W3C: Cs' = (1 - ab) * Cs + ab * B(Cb, Cs)
    auto lerp = [&](int s, int m) { return uint8_t(s + mul255(m - s, dst.a)); };
    const Rgba s2{lerp(src.r, mixed.r), lerp(src.g, mixed.g), lerp(src.b, mixed.b), src.a};
    return normal(dst, s2, opacity);
}

} // namespace core
