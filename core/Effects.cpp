#include "core/Effects.h"

#include "core/Compositor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace core {

namespace {

// Colors to pixel values, remembered: an indexed lookup searches the palette.
class Encoder {
public:
    explicit Encoder(const Document& doc) : m_doc(doc) {}
    uint32_t operator()(Rgba c)
    {
        const uint32_t key = pack(c);
        auto it = m_cache.find(key);
        if (it == m_cache.end())
            it = m_cache.emplace(key, fromRgba(m_doc, c)).first;
        return it->second;
    }

private:
    const Document& m_doc;
    std::unordered_map<uint32_t, uint32_t> m_cache;
};

bool inArea(const Mask* mask, Point origin, int x, int y)
{
    return !mask || mask->contains(x + origin.x, y + origin.y);
}

} // namespace

Image outline(const Document& doc, const Image& src, const OutlineOptions& options, const Mask* mask, Point origin)
{
    const int w = src.width(), h = src.height();
    std::vector<uint8_t> opaque(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            opaque[size_t(y) * size_t(w) + size_t(x)] = toRgba(doc, src, x, y).a > 0;
    // Outside the image counts as empty.
    const auto filled = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && opaque[size_t(y) * size_t(w) + size_t(x)];
    };
    static constexpr int kSides[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    static constexpr int kCorners[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

    Image dst = src.copy(src.bounds());
    const uint32_t value = fromRgba(doc, options.color);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!inArea(mask, origin, x, y) || filled(x, y) != options.inside)
                continue;
            // Outside: an empty pixel next to the shape. Inside: a shape
            // pixel next to an empty one.
            bool edge = false;
            for (const auto& d : kSides)
                edge = edge || filled(x + d[0], y + d[1]) != options.inside;
            if (options.square)
                for (const auto& d : kCorners)
                    edge = edge || filled(x + d[0], y + d[1]) != options.inside;
            if (edge)
                dst.setPixel(x, y, value);
        }
    return dst;
}

Image replaceColor(const Document& doc, const Image& src, Rgba from, Rgba to, int tolerance, const Mask* mask,
                   Point origin)
{
    Image dst = src.copy(src.bounds());
    const uint32_t value = fromRgba(doc, to);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const Rgba c = toRgba(doc, src, x, y);
            const int d = std::max({std::abs(c.r - from.r), std::abs(c.g - from.g), std::abs(c.b - from.b),
                                    std::abs(c.a - from.a)});
            if (d <= tolerance)
                dst.setPixel(x, y, value);
        }
    return dst;
}

Image adjustHsl(const Document& doc, const Image& src, int hue, int saturation, int lightness, const Mask* mask,
                Point origin)
{
    Image dst = src.copy(src.bounds());
    if (hue == 0 && saturation == 0 && lightness == 0)
        return dst;
    // Toward the end the sign points at, by that share of the way.
    const auto move = [](float v, int percent) {
        const float k = std::clamp(percent, -100, 100) / 100.f;
        return k >= 0 ? v + (1 - v) * k : v * (1 + k);
    };
    Encoder encode(doc);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const Rgba c = toRgba(doc, src, x, y);
            if (c.a == 0)
                continue;
            Hsl hsl = toHsl(c);
            hsl.h = std::fmod(hsl.h + float(hue) + 720.f, 360.f);
            hsl.s = move(hsl.s, saturation);
            hsl.l = move(hsl.l, lightness);
            dst.setPixel(x, y, encode(fromHsl(hsl, c.a)));
        }
    return dst;
}

Image quantize(const Document& doc, const Image& src, const Palette& palette, IndexedDither dither, const Mask* mask,
               Point origin, const DitherOptions& options)
{
    Image dst = src.copy(src.bounds());
    const Image indices = ditherToPalette(doc, src, false, palette, 0, dither, false, options);
    Encoder encode(doc);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const Rgba c = toRgba(doc, src, x, y);
            if (c.a == 0)
                continue;
            const Rgba p = palette.color(int(indices.pixel(x, y)));
            dst.setPixel(x, y, encode({p.r, p.g, p.b, c.a}));
        }
    return dst;
}

Image posterize(const Document& doc, const Image& src, int levels, const Mask* mask, Point origin)
{
    Image dst = src.copy(src.bounds());
    levels = std::clamp(levels, 2, 16);
    const float steps = float(levels - 1);
    const auto snap = [&](uint8_t v) { return uint8_t(std::lround(std::round(v / 255.f * steps) * 255.f / steps)); };
    Encoder encode(doc);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const Rgba c = toRgba(doc, src, x, y);
            if (c.a == 0)
                continue;
            dst.setPixel(x, y, encode({snap(c.r), snap(c.g), snap(c.b), c.a}));
        }
    return dst;
}

Image dropShadow(const Document& doc, const Image& src, Rgba color, int dx, int dy, const Mask* mask, Point origin)
{
    Image dst = src.copy(src.bounds());
    Encoder encode(doc);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const int sx = x - dx, sy = y - dy;
            if (sx < 0 || sy < 0 || sx >= src.width() || sy >= src.height())
                continue;
            const int shadowA = toRgba(doc, src, sx, sy).a * color.a / 255;
            if (shadowA == 0)
                continue;
            const Rgba top = toRgba(doc, src, x, y);
            if (top.a == 255)
                continue;
            // The shape over the shadow (straight alpha, source-over).
            const int outA = top.a + shadowA * (255 - top.a) / 255;
            const auto mix = [&](uint8_t t, uint8_t s) {
                return uint8_t((t * top.a * 255 + s * shadowA * (255 - top.a)) / (outA * 255));
            };
            dst.setPixel(x, y, encode({mix(top.r, color.r), mix(top.g, color.g), mix(top.b, color.b), uint8_t(outA)}));
        }
    return dst;
}

CurveTable curveTable(std::vector<CurvePoint> points)
{
    CurveTable table;
    for (int i = 0; i < 256; ++i)
        table[size_t(i)] = uint8_t(i);
    std::sort(points.begin(), points.end(), [](CurvePoint a, CurvePoint b) { return a.x < b.x; });
    // Points sharing an x: the last one counts.
    std::vector<CurvePoint> p;
    for (const CurvePoint& c : points) {
        const CurvePoint q{std::clamp(c.x, 0, 255), std::clamp(c.y, 0, 255)};
        if (!p.empty() && p.back().x == q.x)
            p.back() = q;
        else
            p.push_back(q);
    }
    const size_t n = p.size();
    if (n < 2)
        return table;
    // Fritsch-Carlson tangents: smooth, and never overshooting between points.
    std::vector<double> d(n - 1), m(n);
    for (size_t i = 0; i + 1 < n; ++i)
        d[i] = double(p[i + 1].y - p[i].y) / double(p[i + 1].x - p[i].x);
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (size_t i = 1; i + 1 < n; ++i)
        m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (d[i] == 0) {
            m[i] = m[i + 1] = 0;
            continue;
        }
        const double a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b;
        if (s > 9) {
            const double t = 3 / std::sqrt(s);
            m[i] = t * a * d[i];
            m[i + 1] = t * b * d[i];
        }
    }
    for (int x = 0; x < 256; ++x) {
        double y;
        if (x <= p.front().x) {
            y = p.front().y;
        } else if (x >= p.back().x) {
            y = p.back().y;
        } else {
            size_t i = 0;
            while (p[i + 1].x < x)
                ++i;
            const double h = p[i + 1].x - p[i].x, t = (x - p[i].x) / h, t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * p[i].y + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * p[i + 1].y
                + (t3 - t2) * h * m[i + 1];
        }
        table[size_t(x)] = uint8_t(std::clamp(std::lround(y), 0L, 255L));
    }
    return table;
}

Image applyCurves(const Document& doc, const Image& src, const CurveTable& master, const CurveTable& red,
                  const CurveTable& green, const CurveTable& blue, const Mask* mask, Point origin)
{
    Image dst = src.copy(src.bounds());
    Encoder encode(doc);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const Rgba c = toRgba(doc, src, x, y);
            if (c.a == 0)
                continue;
            dst.setPixel(x, y, encode({red[master[c.r]], green[master[c.g]], blue[master[c.b]], c.a}));
        }
    return dst;
}

Image blur(const Document& doc, const Image& src, int radius, bool gaussian, bool keepShape, const Mask* mask,
           Point origin)
{
    radius = std::clamp(radius, 1, 16);
    const int w = src.width(), h = src.height();
    // The kernel (Gaussian: sigma = radius / 2), one side and the middle.
    std::vector<float> kernel(size_t(radius) + 1, 1.f);
    if (gaussian) {
        const float sigma = std::max(0.5f, radius / 2.f);
        for (int i = 0; i <= radius; ++i)
            kernel[size_t(i)] = std::exp(-float(i * i) / (2 * sigma * sigma));
    }
    // Premultiplied RGBA as floats; two separable passes (edges clamp).
    std::vector<float> a(size_t(w) * size_t(h) * 4), b(a.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Rgba c = toRgba(doc, src, x, y);
            const float alpha = c.a / 255.f;
            float* p = &a[(size_t(y) * size_t(w) + size_t(x)) * 4];
            p[0] = c.r * alpha;
            p[1] = c.g * alpha;
            p[2] = c.b * alpha;
            p[3] = alpha;
        }
    const auto pass = [&](const std::vector<float>& in, std::vector<float>& out, bool horizontal) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float sum[4] = {0, 0, 0, 0}, weight = 0;
                for (int d = -radius; d <= radius; ++d) {
                    const int sx = horizontal ? std::clamp(x + d, 0, w - 1) : x;
                    const int sy = horizontal ? y : std::clamp(y + d, 0, h - 1);
                    const float k = kernel[size_t(std::abs(d))];
                    const float* p = &in[(size_t(sy) * size_t(w) + size_t(sx)) * 4];
                    for (int i = 0; i < 4; ++i)
                        sum[i] += p[i] * k;
                    weight += k;
                }
                float* q = &out[(size_t(y) * size_t(w) + size_t(x)) * 4];
                for (int i = 0; i < 4; ++i)
                    q[i] = sum[i] / weight;
            }
    };
    pass(a, b, true);
    pass(b, a, false);

    Image dst = src.copy(src.bounds());
    Encoder encode(doc);
    const auto byte = [](float v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!inArea(mask, origin, x, y))
                continue;
            const float* p = &a[(size_t(y) * size_t(w) + size_t(x)) * 4];
            const uint8_t alpha = keepShape ? toRgba(doc, src, x, y).a : byte(p[3] * 255);
            if (alpha == 0 || p[3] <= 0) {
                dst.setPixel(x, y, encode(Rgba{}));
                continue;
            }
            dst.setPixel(x, y, encode({byte(p[0] / p[3]), byte(p[1] / p[3]), byte(p[2] / p[3]), alpha}));
        }
    return dst;
}

Image convolve(const Document& doc, const Image& src, const std::vector<int>& kernel, int size, int divisor,
               int bias, const Mask* mask, Point origin)
{
    Image dst = src.copy(src.bounds());
    if ((size != 3 && size != 5) || int(kernel.size()) != size * size)
        return dst;
    if (divisor == 0) {
        int sum = 0;
        for (const int k : kernel)
            sum += k;
        divisor = sum == 0 ? 1 : sum;
    }
    const int w = src.width(), h = src.height(), half = size / 2;
    std::vector<Rgba> colors(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            colors[size_t(y) * size_t(w) + size_t(x)] = toRgba(doc, src, x, y);
    Encoder encode(doc);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Rgba self = colors[size_t(y) * size_t(w) + size_t(x)];
            if (self.a == 0 || !inArea(mask, origin, x, y))
                continue;
            int sum[3] = {0, 0, 0};
            for (int ky = 0; ky < size; ++ky)
                for (int kx = 0; kx < size; ++kx) {
                    const int sx = std::clamp(x + kx - half, 0, w - 1), sy = std::clamp(y + ky - half, 0, h - 1);
                    Rgba n = colors[size_t(sy) * size_t(w) + size_t(sx)];
                    if (n.a == 0)
                        n = self;
                    const int k = kernel[size_t(ky * size + kx)];
                    sum[0] += k * n.r;
                    sum[1] += k * n.g;
                    sum[2] += k * n.b;
                }
            const auto channel = [&](int v) {
                return uint8_t(std::clamp(int(std::lround(double(v) / divisor)) + bias, 0, 255));
            };
            dst.setPixel(x, y, encode({channel(sum[0]), channel(sum[1]), channel(sum[2]), self.a}));
        }
    return dst;
}

Image cleanup(const Document& doc, const Image& src, const CleanupOptions& options, const Mask* mask, Point origin)
{
    const int w = src.width(), h = src.height();
    std::vector<Rgba> c(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            c[size_t(y) * size_t(w) + size_t(x)] = toRgba(doc, src, x, y);
    // Outside the image counts as empty.
    const auto at = [&](const std::vector<Rgba>& v, int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h ? v[size_t(y) * size_t(w) + size_t(x)] : Rgba{};
    };
    static constexpr int kSides[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    // One rule over a snapshot, so a pass doesn't feed on its own changes.
    const auto pass = [&](const auto& rule) {
        const std::vector<Rgba> before = c;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (inArea(mask, origin, x, y))
                    rule(before, x, y, c[size_t(y) * size_t(w) + size_t(x)]);
    };

    if (options.orphans)
        pass([&](const std::vector<Rgba>& v, int x, int y, Rgba& out) {
            if (at(v, x, y).a == 0)
                return;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && at(v, x + dx, y + dy).a > 0)
                        return;
            out = Rgba{};
        });
    if (options.pinholes)
        pass([&](const std::vector<Rgba>& v, int x, int y, Rgba& out) {
            if (at(v, x, y).a > 0)
                return;
            Rgba n[4];
            for (int i = 0; i < 4; ++i) {
                n[i] = at(v, x + kSides[i][0], y + kSides[i][1]);
                if (n[i].a == 0)
                    return;
            }
            // The most common of the four (ties: the first, from the top).
            int best = 0, bestCount = 0;
            for (int i = 0; i < 4; ++i) {
                const int count = int(std::count(std::begin(n), std::end(n), n[i]));
                if (count > bestCount) {
                    best = i;
                    bestCount = count;
                }
            }
            out = n[best];
        });
    if (options.strays)
        pass([&](const std::vector<Rgba>& v, int x, int y, Rgba& out) {
            const Rgba self = at(v, x, y);
            const Rgba first = at(v, x + kSides[0][0], y + kSides[0][1]);
            if (self.a == 0 || first.a == 0 || first == self)
                return;
            for (int i = 1; i < 4; ++i)
                if (at(v, x + kSides[i][0], y + kSides[i][1]) != first)
                    return;
            out = first;
        });
    if (options.doubles) {
        // In place, row by row: each removal is seen by the next pixels.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Rgba self = at(c, x, y);
                if (self.a == 0 || !inArea(mask, origin, x, y))
                    continue;
                int across = 0, upDown = 0, others = 0, hx = 0, vy = 0;
                for (const auto& d : kSides) {
                    const Rgba n = at(c, x + d[0], y + d[1]);
                    if (n.a == 0)
                        continue;
                    if (n != self)
                        ++others;
                    else if (d[0] != 0) {
                        ++across;
                        hx = d[0];
                    } else {
                        ++upDown;
                        vy = d[1];
                    }
                }
                if (across == 1 && upDown == 1 && others == 0 && at(c, x + hx, y + vy).a == 0)
                    c[size_t(y) * size_t(w) + size_t(x)] = Rgba{};
            }
    }

    Image dst = src.copy(src.bounds());
    Encoder encode(doc);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Rgba now = c[size_t(y) * size_t(w) + size_t(x)];
            if (now != toRgba(doc, src, x, y))
                dst.setPixel(x, y, encode(now));
        }
    return dst;
}

namespace {

// Calls fn(x, y) for every pixel of the brush at `center`, inside `bounds`.
template <typename Fn>
Rect forBrush(Rect bounds, Point center, int size, BrushShape shape, const Image* footprint, Fn fn)
{
    if (footprint && !footprint->isNull()) {
        const Rect area{center.x - footprint->width() / 2, center.y - footprint->height() / 2, footprint->width(),
                        footprint->height()};
        Rect changed;
        for (int y = 0; y < footprint->height(); ++y)
            for (int x = 0; x < footprint->width(); ++x) {
                const int px = area.x + x, py = area.y + y;
                if (footprint->pixel(x, y) == 0 || !bounds.contains(px, py))
                    continue;
                fn(px, py);
                changed = changed.united(Rect{px, py, 1, 1});
            }
        return changed;
    }
    size = std::max(1, size);
    const Rect area = brushRect(center, size);
    const auto& spans = brushSpans(size, size <= 2 ? BrushShape::Square : shape);
    Rect changed;
    for (int i = 0; i < size; ++i) {
        const Rect row = Rect{area.x + spans[size_t(i)].first, area.y + i,
                              spans[size_t(i)].second - spans[size_t(i)].first + 1, 1}
                             .intersected(bounds);
        for (int x = row.x; x < row.x + row.w; ++x)
            fn(x, row.y);
        changed = changed.united(row);
    }
    return changed;
}

uint32_t mix(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

} // namespace

Rect blurBrush(const Document& doc, Image& image, const Image& before, Point center, int size, BrushShape shape,
               const Image* footprint)
{
    const int w = before.width(), h = before.height();
    return forBrush(image.bounds(), center, size, shape, footprint, [&](int x, int y) {
        float sum[4] = {0, 0, 0, 0};
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Rgba c = toRgba(doc, before, std::clamp(x + dx, 0, w - 1), std::clamp(y + dy, 0, h - 1));
                const float a = c.a / 255.f;
                sum[0] += c.r * a;
                sum[1] += c.g * a;
                sum[2] += c.b * a;
                sum[3] += a;
            }
        if (sum[3] <= 0) {
            image.setPixel(x, y, fromRgba(doc, Rgba{}));
            return;
        }
        const auto byte = [](float v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); };
        image.setPixel(x, y, fromRgba(doc, {byte(sum[0] / sum[3]), byte(sum[1] / sum[3]), byte(sum[2] / sum[3]),
                                            byte(sum[3] / 9 * 255)}));
    });
}

Rect jumbleBrush(Image& image, const Image& before, Point center, int size, BrushShape shape, uint32_t seed,
                 const Image* footprint)
{
    static constexpr int kNear[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    const int w = before.width(), h = before.height();
    return forBrush(image.bounds(), center, size, shape, footprint, [&](int x, int y) {
        const auto& d = kNear[mix(uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ seed) % 8];
        image.setPixel(x, y, before.pixel(std::clamp(x + d[0], 0, w - 1), std::clamp(y + d[1], 0, h - 1)));
    });
}

Hsl toHsl(Rgba c)
{
    const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    const float hi = std::max({r, g, b}), lo = std::min({r, g, b});
    Hsl out;
    out.l = (hi + lo) / 2;
    const float d = hi - lo;
    if (d <= 0)
        return out; // gray: no hue or saturation
    out.s = d / (1 - std::abs(2 * out.l - 1));
    if (hi == r)
        out.h = 60 * std::fmod((g - b) / d + 6, 6.f);
    else if (hi == g)
        out.h = 60 * ((b - r) / d + 2);
    else
        out.h = 60 * ((r - g) / d + 4);
    return out;
}

Rgba fromHsl(Hsl c, uint8_t alpha)
{
    const float s = std::clamp(c.s, 0.f, 1.f), l = std::clamp(c.l, 0.f, 1.f);
    const float chroma = (1 - std::abs(2 * l - 1)) * s;
    const float hp = std::fmod(std::fmod(c.h, 360.f) + 360.f, 360.f) / 60;
    const float x = chroma * (1 - std::abs(std::fmod(hp, 2.f) - 1));
    float r = 0, g = 0, b = 0;
    switch (int(hp)) {
    case 0: r = chroma; g = x; break;
    case 1: r = x; g = chroma; break;
    case 2: g = chroma; b = x; break;
    case 3: g = x; b = chroma; break;
    case 4: r = x; b = chroma; break;
    default: r = chroma; b = x; break;
    }
    const float m = l - chroma / 2;
    const auto byte = [m](float v) { return uint8_t(std::clamp(std::lround((v + m) * 255), 0L, 255L)); };
    return {byte(r), byte(g), byte(b), alpha};
}

} // namespace core
