#include "core/PaletteTools.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace core {

namespace {

int channel(Rgba c, int ch) { return ch == 0 ? c.r : ch == 1 ? c.g : ch == 2 ? c.b : c.a; }

float toLinear(uint8_t v)
{
    const float c = v / 255.f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

uint8_t fromLinear(float c)
{
    c = std::clamp(c, 0.f, 1.f);
    const float v = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
    return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255));
}

// HSV hue (0..360) and saturation (0..1).
void hueSaturation(Rgba c, float& hue, float& saturation)
{
    const int hi = std::max({c.r, c.g, c.b}), lo = std::min({c.r, c.g, c.b});
    const int chroma = hi - lo;
    saturation = hi == 0 ? 0.f : float(chroma) / float(hi);
    if (chroma == 0) {
        hue = 0;
        return;
    }
    float h;
    if (hi == c.r)
        h = float(c.g - c.b) / float(chroma);
    else if (hi == c.g)
        h = 2 + float(c.b - c.r) / float(chroma);
    else
        h = 4 + float(c.r - c.g) / float(chroma);
    hue = std::fmod(h * 60 + 360, 360.f);
}

} // namespace

std::vector<Rgba> medianCut(std::vector<ColorCount> colors, int maxColors)
{
    struct Box {
        size_t begin, end;
    };
    // Colors with see-through shades are cut on their alpha too.
    const int channels = std::any_of(colors.begin(), colors.end(), [](const ColorCount& c) { return c.color.a < 255; }) ? 4 : 3;
    auto widest = [&](const Box& box, int& range) {
        int best = 0;
        range = -1;
        for (int ch = 0; ch < channels; ++ch) {
            int lo = 255, hi = 0;
            for (size_t i = box.begin; i < box.end; ++i) {
                lo = std::min(lo, channel(colors[i].color, ch));
                hi = std::max(hi, channel(colors[i].color, ch));
            }
            if (hi - lo > range) {
                range = hi - lo;
                best = ch;
            }
        }
        return best;
    };

    std::vector<Box> boxes{{0, colors.size()}};
    while (int(boxes.size()) < maxColors) {
        int pick = -1, pickRange = 0, pickChannel = 0;
        for (size_t b = 0; b < boxes.size(); ++b) {
            if (boxes[b].end - boxes[b].begin < 2)
                continue;
            int range = 0;
            const int ch = widest(boxes[b], range);
            if (range > pickRange) {
                pick = int(b);
                pickRange = range;
                pickChannel = ch;
            }
        }
        if (pick < 0)
            break;
        Box& box = boxes[size_t(pick)];
        const auto first = colors.begin() + std::ptrdiff_t(box.begin);
        const auto last = colors.begin() + std::ptrdiff_t(box.end);
        std::sort(first, last, [&](const ColorCount& a, const ColorCount& b) {
            return channel(a.color, pickChannel) < channel(b.color, pickChannel);
        });
        uint64_t total = 0;
        for (auto it = first; it != last; ++it)
            total += it->count;
        // First index of the upper half: past half the pixels, both halves non-empty.
        size_t split = box.begin + 1;
        uint64_t sum = colors[box.begin].count;
        while (split < box.end - 1 && sum * 2 < total)
            sum += colors[split++].count;
        const Box upper{split, box.end};
        box.end = split;
        boxes.push_back(upper);
    }

    std::vector<Rgba> out;
    for (const Box& box : boxes) {
        uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
        for (size_t i = box.begin; i < box.end; ++i) {
            const ColorCount& c = colors[i];
            r += c.color.r * c.count;
            g += c.color.g * c.count;
            b += c.color.b * c.count;
            a += c.color.a * c.count;
            n += c.count;
        }
        out.push_back({uint8_t((r + n / 2) / n), uint8_t((g + n / 2) / n), uint8_t((b + n / 2) / n),
                       channels == 4 ? uint8_t((a + n / 2) / n) : uint8_t(255)});
    }
    return out;
}


Oklab toOklab(Rgba c)
{
    const float r = toLinear(c.r), g = toLinear(c.g), b = toLinear(c.b);
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

Rgba fromOklab(Oklab c, uint8_t alpha)
{
    const float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
    return {fromLinear(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3),
            fromLinear(-1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3),
            fromLinear(-0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3), alpha};
}

std::vector<Rgba> rampColors(Rgba from, Rgba to, int count, bool hueArc)
{
    count = std::max(2, count);
    const Oklab a = toOklab(from), b = toOklab(to);
    const float chromaA = std::hypot(a.a, a.b), chromaB = std::hypot(b.a, b.b);
    constexpr float kGray = 0.02f;
    float hueA = std::atan2(a.b, a.a), hueB = std::atan2(b.b, b.a);
    if (chromaA < kGray)
        hueA = hueB;
    else if (chromaB < kGray)
        hueB = hueA;
    // The short way around.
    constexpr float kPi = 3.14159265358979f;
    float turn = hueB - hueA;
    if (turn > kPi)
        turn -= 2 * kPi;
    else if (turn < -kPi)
        turn += 2 * kPi;
    std::vector<Rgba> out;
    for (int i = 0; i < count; ++i) {
        const float t = float(i) / float(count - 1);
        const uint8_t alpha = uint8_t(std::lround(from.a + (to.a - from.a) * t));
        const float L = a.L + (b.L - a.L) * t;
        if (hueArc) {
            const float chroma = chromaA + (chromaB - chromaA) * t, hue = hueA + turn * t;
            out.push_back(fromOklab({L, chroma * std::cos(hue), chroma * std::sin(hue)}, alpha));
        } else {
            out.push_back(fromOklab({L, a.a + (b.a - a.a) * t, a.b + (b.b - a.b) * t}, alpha));
        }
    }
    out.front() = from;
    out.back() = to;
    return out;
}

std::vector<int> sortOrder(const std::vector<Rgba>& colors, PaletteSort key)
{
    struct Key {
        float primary = 0, secondary = 0;
    };
    std::vector<Key> keys;
    for (const Rgba c : colors) {
        float hue, saturation;
        hueSaturation(c, hue, saturation);
        const float light = toOklab(c).L;
        switch (key) {
        case PaletteSort::Hue: keys.push_back({saturation == 0 ? -1.f : hue, light}); break; // grays first
        case PaletteSort::Saturation: keys.push_back({saturation, light}); break;
        case PaletteSort::Lightness: keys.push_back({light, 0}); break;
        }
    }
    std::vector<int> order(colors.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Key& ka = keys[size_t(a)];
        const Key& kb = keys[size_t(b)];
        return ka.primary != kb.primary ? ka.primary < kb.primary : ka.secondary < kb.secondary;
    });
    return order;
}

ColorReduction reduceColors(const std::vector<Rgba>& colors, const std::vector<uint64_t>& counts, int maxColors)
{
    ColorReduction out;
    maxColors = std::max(1, maxColors);
    if (int(colors.size()) <= maxColors) {
        out.colors = colors;
        out.map.resize(colors.size());
        std::iota(out.map.begin(), out.map.end(), 0);
        return out;
    }
    // Every entry starts as its own group; the two closest groups (OKLab
    // distance of their weighted averages) merge until few enough are left.
    struct Group {
        double r = 0, g = 0, b = 0, a = 0, weight = 0;
        Oklab lab;
        Rgba mean() const
        {
            auto ch = [&](double v) { return uint8_t(std::lround(v / weight)); };
            return {ch(r), ch(g), ch(b), ch(a)};
        }
    };
    std::vector<Group> groups;
    std::vector<int> groupOf(colors.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        const double w = double((i < counts.size() ? counts[i] : 0) + 1);
        const Rgba c = colors[i];
        groups.push_back({c.r * w, c.g * w, c.b * w, c.a * w, w, toOklab(c)});
        groupOf[i] = int(i);
    }
    std::vector<bool> alive(groups.size(), true);
    auto dist = [](const Oklab& x, const Oklab& y, double da) {
        const double dl = x.L - y.L, d1 = x.a - y.a, d2 = x.b - y.b;
        return dl * dl + d1 * d1 + d2 * d2 + da * da;
    };
    for (size_t left = groups.size(); left > size_t(maxColors); --left) {
        size_t bi = 0, bj = 0;
        double best = 1e30;
        for (size_t i = 0; i < groups.size(); ++i) {
            if (!alive[i])
                continue;
            for (size_t j = i + 1; j < groups.size(); ++j) {
                if (!alive[j])
                    continue;
                const double da = (groups[i].a / groups[i].weight - groups[j].a / groups[j].weight) / 255.0;
                const double d = dist(groups[i].lab, groups[j].lab, da);
                if (d < best) {
                    best = d;
                    bi = i;
                    bj = j;
                }
            }
        }
        Group& g = groups[bi];
        const Group& h = groups[bj];
        g.r += h.r;
        g.g += h.g;
        g.b += h.b;
        g.a += h.a;
        g.weight += h.weight;
        g.lab = toOklab(g.mean());
        alive[bj] = false;
        for (int& k : groupOf)
            if (k == int(bj))
                k = int(bi);
    }
    // Groups in the order of their first entry.
    std::vector<int> newIndex(groups.size(), -1);
    for (size_t i = 0; i < colors.size(); ++i) {
        int& n = newIndex[size_t(groupOf[i])];
        if (n < 0) {
            n = int(out.colors.size());
            out.colors.push_back(groups[size_t(groupOf[i])].mean());
        }
        out.map.push_back(n);
    }
    return out;
}

} // namespace core
