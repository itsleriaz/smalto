#include "core/Transform.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cmath>

namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Nearest-neighbor rotation of `src` into a w x h image, both centered.
Image rotateNearest(const Image& src, double radians, int w, int h, uint32_t transparent)
{
    Image out(w, h, src.format());
    out.clear(transparent);
    const double c = std::cos(radians), s = std::sin(radians);
    const double cx = w / 2.0, cy = h / 2.0;
    const double sx = src.width() / 2.0, sy = src.height() / 2.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // Inverse rotation of the pixel center.
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const double u = c * dx + s * dy + sx;
            const double v = -s * dx + c * dy + sy;
            const int ix = int(std::floor(u)), iy = int(std::floor(v));
            if (ix >= 0 && iy >= 0 && ix < src.width() && iy < src.height())
                out.setPixel(x, y, src.pixel(ix, iy));
        }
    return out;
}

} // namespace

Image flippedHorizontally(const Image& src)
{
    Image out(src.width(), src.height(), src.format());
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
            out.setPixel(src.width() - 1 - x, y, src.pixel(x, y));
    return out;
}

Image flippedVertically(const Image& src)
{
    Image out(src.width(), src.height(), src.format());
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
            out.setPixel(x, src.height() - 1 - y, src.pixel(x, y));
    return out;
}

Image rotated90(const Image& src, bool clockwise)
{
    Image out(src.height(), src.width(), src.format());
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (clockwise)
                out.setPixel(src.height() - 1 - y, x, src.pixel(x, y));
            else
                out.setPixel(y, src.width() - 1 - x, src.pixel(x, y));
        }
    return out;
}

Image scaledNearest(const Image& src, int w, int h)
{
    w = std::max(1, w);
    h = std::max(1, h);
    if (w == src.width() && h == src.height())
        return src;
    Image out(w, h, src.format());
    for (int y = 0; y < h; ++y) {
        const int sy = int((int64_t(y) * src.height()) / h);
        for (int x = 0; x < w; ++x)
            out.setPixel(x, y, src.pixel(int((int64_t(x) * src.width()) / w), sy));
    }
    return out;
}

namespace {

struct Tap {
    int index;
    float weight;
};

// For each of `dst` output positions along one axis, the source pixels that
// feed it: two (linear) when enlarging, every covered pixel (a box) when shrinking.
std::vector<std::vector<Tap>> axisTaps(int src, int dst)
{
    std::vector<std::vector<Tap>> taps(static_cast<size_t>(dst));
    const double scale = double(src) / dst;
    for (int d = 0; d < dst; ++d) {
        auto& list = taps[size_t(d)];
        if (scale <= 1.0) { // linear between the two nearest centers
            const double c = (d + 0.5) * scale - 0.5;
            const int i0 = int(std::floor(c));
            const float f = float(c - i0);
            list.push_back({std::clamp(i0, 0, src - 1), 1.0f - f});
            list.push_back({std::clamp(i0 + 1, 0, src - 1), f});
        } else { // the average of what the output pixel covers
            const double from = d * scale, to = (d + 1) * scale;
            for (int i = int(std::floor(from)); i < std::min(src, int(std::ceil(to))); ++i) {
                const double lo = std::max(from, double(i)), hi = std::min(to, double(i + 1));
                if (hi > lo)
                    list.push_back({i, float((hi - lo) / scale)});
            }
        }
    }
    return taps;
}

} // namespace

Image scaled(const Image& src, int w, int h, Resample method)
{
    w = std::max(1, w);
    h = std::max(1, h);
    if (w == src.width() && h == src.height())
        return src;
    if (method == Resample::Nearest)
        return scaledNearest(src, w, h);
    const bool blendable = src.format() == PixelFormat::Rgba8 || src.format() == PixelFormat::Gray16;
    if (method == Resample::Smooth || !blendable) {
        // Scale2x three times (8 x 8 the pixels), fewer for a big image.
        const int64_t pixels = int64_t(src.width()) * src.height();
        const int levels = pixels <= 256 * 256 ? 3 : pixels <= 1024 * 1024 ? 1 : 0;
        Image big = src;
        for (int i = 0; i < levels; ++i)
            big = scale2x(big);
        return scaledNearest(big, w, h);
    }
    // Bilinear: premultiplied color and alpha as floats, one axis at a time.
    const bool gray = src.format() == PixelFormat::Gray16;
    const auto decode = [&](uint32_t v, float out[4]) {
        const float a = gray ? float(v >> 8) : float(v >> 24);
        out[3] = a;
        if (gray) {
            out[0] = out[1] = out[2] = float(v & 0xff) * a / 255.0f;
        } else {
            out[0] = float(v & 0xff) * a / 255.0f;
            out[1] = float((v >> 8) & 0xff) * a / 255.0f;
            out[2] = float((v >> 16) & 0xff) * a / 255.0f;
        }
    };
    const auto encode = [&](const float in[4]) -> uint32_t {
        const float a = std::clamp(in[3], 0.0f, 255.0f);
        const auto channel = [&](float c) { return a > 0 ? uint32_t(std::clamp(std::lround(c * 255.0f / a), 0L, 255L)) : 0u; };
        const uint32_t alpha = uint32_t(std::lround(a));
        if (gray)
            return channel(in[0]) | (alpha << 8);
        return channel(in[0]) | (channel(in[1]) << 8) | (channel(in[2]) << 16) | (alpha << 24);
    };
    const auto tx = axisTaps(src.width(), w);
    const auto ty = axisTaps(src.height(), h);
    // Horizontal pass: h_src rows of w pixels (4 floats each).
    std::vector<float> mid(size_t(src.height()) * w * 4, 0.0f);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (const Tap& t : tx[size_t(x)]) {
                float p[4];
                decode(src.pixel(t.index, y), p);
                for (int k = 0; k < 4; ++k)
                    acc[k] += p[k] * t.weight;
            }
            for (int k = 0; k < 4; ++k)
                mid[(size_t(y) * w + x) * 4 + size_t(k)] = acc[k];
        }
    Image out(w, h, src.format());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (const Tap& t : ty[size_t(y)])
                for (int k = 0; k < 4; ++k)
                    acc[k] += mid[(size_t(t.index) * w + x) * 4 + size_t(k)] * t.weight;
            out.setPixel(x, y, encode(acc));
        }
    return out;
}

Image scale2x(const Image& src)
{
    const int w = src.width(), h = src.height();
    Image out(w * 2, h * 2, src.format());
    auto at = [&](int x, int y) { return src.pixel(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1)); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = src.pixel(x, y);
            const uint32_t a = at(x, y - 1), b = at(x + 1, y), c = at(x - 1, y), d = at(x, y + 1);
            out.setPixel(2 * x, 2 * y, (c == a && c != d && a != b) ? a : p);
            out.setPixel(2 * x + 1, 2 * y, (a == b && a != c && b != d) ? b : p);
            out.setPixel(2 * x, 2 * y + 1, (d == c && d != b && c != a) ? c : p);
            out.setPixel(2 * x + 1, 2 * y + 1, (b == d && b != a && d != c) ? d : p);
        }
    return out;
}

void rotatedSize(int w, int h, double degrees, int& outW, int& outH)
{
    const double r = degrees * kPi / 180.0;
    const double c = std::abs(std::cos(r)), s = std::abs(std::sin(r));
    // Slack so exact multiples of 90 degrees don't grow by a pixel.
    outW = std::max(1, int(std::ceil(w * c + h * s - 1e-6)));
    outH = std::max(1, int(std::ceil(w * s + h * c - 1e-6)));
}

Image rotated(const Image& src, double degrees, uint32_t transparent)
{
    double turns = std::fmod(degrees, 360.0);
    if (turns < 0)
        turns += 360.0;
    // Exact for quarter turns.
    for (int q = 0; q <= 4; ++q)
        if (std::abs(turns - q * 90.0) < 1e-6) {
            switch (q % 4) {
            case 0: return src;
            case 1: return rotated90(src, true);
            case 2: return rotated90(rotated90(src, true), true);
            default: return rotated90(src, false);
            }
        }

    const double r = turns * kPi / 180.0;
    int w, h;
    rotatedSize(src.width(), src.height(), turns, w, h);
    if (int64_t(src.width()) * src.height() > 256 * 256)
        return rotateNearest(src, r, w, h, transparent);

    // RotSprite: 8x up with Scale2x, rotate there, sample each 8x8 block's center.
    const Image big = scale2x(scale2x(scale2x(src)));
    const Image turned = rotateNearest(big, r, w * 8, h * 8, transparent);
    Image out(w, h, src.format());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.setPixel(x, y, turned.pixel(x * 8 + 4, y * 8 + 4));
    return out;
}

} // namespace core
