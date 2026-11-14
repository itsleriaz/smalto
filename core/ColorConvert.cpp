#include "core/ColorConvert.h"

#include "core/Compositor.h"
#include "core/PaletteTools.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace core {

namespace {

// Visible colors (opaque, alpha dropped) with their pixel counts, in first
// seen order. Stops once there are more than `limit`.
std::vector<ColorCount> histogram(const Document& doc, size_t limit, bool keepAlpha = false)
{
    std::vector<ColorCount> colors;
    std::unordered_map<uint32_t, size_t> index;
    std::unordered_set<const Image*> seen;
    bool full = false;
    doc.forEachLayer([&](const Layer& layer) {
        if (full || layer.kind != LayerKind::Image)
            return;
        for (const CelRef& cel : layer.cels) {
            if (full)
                break;
            if (!cel || !cel->image || !seen.insert(cel->image.get()).second)
                continue;
            const Image& image = *cel->image;
            for (int y = 0; y < image.height() && !full; ++y)
                for (int x = 0; x < image.width(); ++x) {
                    Rgba c = toRgba(doc, image, x, y, layer.isBackground());
                    if (c.a == 0)
                        continue;
                    if (!keepAlpha)
                        c.a = 255;
                    const auto [it, added] = index.emplace(pack(c), colors.size());
                    if (added) {
                        if (colors.size() == limit) {
                            full = true;
                            break;
                        }
                        colors.push_back({c, 0});
                    }
                    ++colors[it->second].count;
                }
        }
    });
    return colors;
}

int luminance(Rgba c) { return c.r * 30 + c.g * 59 + c.b * 11; }

} // namespace

std::vector<Rgba> spriteColors(const Document& doc, size_t limit)
{
    std::vector<Rgba> out;
    for (const ColorCount& c : histogram(doc, limit))
        out.push_back(c.color);
    return out;
}

Palette paletteFromSprite(const Document& doc, int maxColors, bool keepAlpha)
{
    const size_t room = size_t(std::max(1, maxColors - 1)); // index 0 is transparent
    std::vector<Rgba> picked;
    if (const auto colors = histogram(doc, room + 1, keepAlpha); colors.size() <= room) {
        for (const ColorCount& c : colors)
            picked.push_back(c.color);
    } else {
        picked = medianCut(histogram(doc, size_t(-1), keepAlpha), int(room));
        std::stable_sort(picked.begin(), picked.end(), [](Rgba a, Rgba b) { return luminance(a) < luminance(b); });
        picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
    }
    Palette palette;
    palette.entries.push_back({Rgba{}, {}});
    for (Rgba c : picked)
        palette.entries.push_back({c, {}});
    return palette;
}

bool canChangeColorMode(const Document& doc)
{
    (void)doc;
    return true; // tilesets convert with the cels
}

namespace {

// A Bayer matrix of side 2^levels at (x, y), 0 .. side^2 - 1.
int bayerRank(int x, int y, int levels)
{
    int v = 0;
    for (int i = 0; i < levels; ++i) {
        const int xb = (x >> i) & 1, yb = (y >> i) & 1;
        v = (v << 2) | ((xb ^ yb) << 1) | yb;
    }
    return v;
}

// Void and cluster (Ulichney): a 64 x 64 tile where every threshold is as far
// from its neighbours as it can be, wrapping at the edges.
std::vector<uint16_t> makeBlueNoise()
{
    constexpr int N = 64, T = N * N, R = 6;
    constexpr float sigma = 1.5f;
    float kernel[2 * R + 1][2 * R + 1];
    for (int dy = -R; dy <= R; ++dy)
        for (int dx = -R; dx <= R; ++dx)
            kernel[dy + R][dx + R] = std::exp(-float(dx * dx + dy * dy) / (2 * sigma * sigma));
    std::vector<float> energy(T, 0.0f);
    std::vector<uint8_t> on(T, 0);
    const auto splat = [&](int index, float sign) {
        const int cx = index % N, cy = index / N;
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx)
                energy[size_t(((cy + dy + N) % N) * N + (cx + dx + N) % N)] += sign * kernel[dy + R][dx + R];
    };
    const auto tightest = [&] { // the set pixel with the most set neighbours
        int best = -1;
        float value = -1;
        for (int i = 0; i < T; ++i)
            if (on[size_t(i)] && energy[size_t(i)] > value) {
                value = energy[size_t(i)];
                best = i;
            }
        return best;
    };
    const auto largestVoid = [&] { // the empty pixel with the fewest set neighbours
        int best = -1;
        float value = 1e30f;
        for (int i = 0; i < T; ++i)
            if (!on[size_t(i)] && energy[size_t(i)] < value) {
                value = energy[size_t(i)];
                best = i;
            }
        return best;
    };

    // A scattered start, then moved until no set pixel would rather be in the
    // biggest hole.
    uint32_t seed = 0x9E3779B9u;
    const int ones = T / 10;
    for (int placed = 0; placed < ones;) {
        seed = seed * 1664525u + 1013904223u;
        const int i = int((seed >> 8) % unsigned(T));
        if (!on[size_t(i)]) {
            on[size_t(i)] = 1;
            splat(i, 1);
            ++placed;
        }
    }
    for (int round = 0; round < 4 * T; ++round) {
        const int c = tightest();
        on[size_t(c)] = 0;
        splat(c, -1);
        const int v = largestVoid();
        on[size_t(v)] = 1;
        splat(v, 1);
        if (v == c)
            break;
    }

    std::vector<uint16_t> rank(T, 0);
    const auto startOn = on;
    const auto startEnergy = energy;
    for (int r = ones - 1; r >= 0; --r) { // the densest clusters rank last among the first
        const int c = tightest();
        on[size_t(c)] = 0;
        splat(c, -1);
        rank[size_t(c)] = uint16_t(r);
    }
    on = startOn;
    energy = startEnergy;
    for (int r = ones; r < T; ++r) { // then each biggest hole in turn
        const int v = largestVoid();
        on[size_t(v)] = 1;
        splat(v, 1);
        rank[size_t(v)] = uint16_t(r);
    }
    return rank;
}

// sRGB (0..255) to OKLab, for finding the nearest palette color the way colors look.
struct Lab {
    float l, a, b;
};
Lab toOklab(float r, float g, float b)
{
    const auto lin = [](float c) {
        c /= 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    const float R = lin(r), G = lin(g), B = lin(b);
    const float l = std::cbrt(0.4122214708f * R + 0.5363325363f * G + 0.0514459929f * B);
    const float m = std::cbrt(0.2119034982f * R + 0.6806995451f * G + 0.1073969566f * B);
    const float s = std::cbrt(0.0883024619f * R + 0.2817188376f * G + 0.6299787005f * B);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

// The palette entry nearest a color: by distance in RGB (as everywhere else), or in OKLab.
class NearestColor {
public:
    NearestColor(const Palette& palette, int transparentIndex, bool background, bool oklab)
        : m_palette(palette), m_transparent(transparentIndex), m_background(background), m_oklab(oklab)
    {
        if (oklab)
            for (int i = 0; i < palette.size(); ++i) {
                const Rgba c = palette.color(i);
                m_labs.push_back(toOklab(c.r, c.g, c.b));
            }
    }
    uint32_t operator()(Rgba want) const
    {
        if (!m_oklab)
            return fromRgba(ColorMode::Indexed, m_palette, m_transparent, want, m_background);
        if (want.a == 0)
            return uint32_t(m_transparent);
        const Lab w = toOklab(want.r, want.g, want.b);
        int best = -1;
        float bestDistance = 0;
        for (int i = 0; i < m_palette.size(); ++i) {
            if (i == m_transparent && !m_background)
                continue;
            const Lab& p = m_labs[size_t(i)];
            const float da = (m_palette.color(i).a - want.a) / 255.0f;
            const float d = (p.l - w.l) * (p.l - w.l) + (p.a - w.a) * (p.a - w.a) + (p.b - w.b) * (p.b - w.b) + da * da;
            if (best < 0 || d < bestDistance) {
                best = i;
                bestDistance = d;
            }
        }
        return uint32_t(best < 0 ? m_transparent : best);
    }

private:
    const Palette& m_palette;
    int m_transparent;
    bool m_background;
    bool m_oklab;
    std::vector<Lab> m_labs;
};

// The point `d` along a Hilbert curve over an n x n grid (n a power of two).
void hilbertPoint(int n, int d, int& x, int& y)
{
    x = y = 0;
    int t = d;
    for (int s = 1; s < n; s *= 2) {
        const int rx = 1 & (t / 2);
        const int ry = 1 & (t ^ rx);
        if (ry == 0) { // turn the square so far
            if (rx == 1) {
                x = s - 1 - x;
                y = s - 1 - y;
            }
            std::swap(x, y);
        }
        x += s * rx;
        y += s * ry;
        t /= 4;
    }
}

// Riemersma dithering: pixels in Hilbert curve order, each taking in a fading sum of the
// errors of the last 16 before it.
std::shared_ptr<Image> riemersmaIndexed(const Document& doc, const Image& image, bool background, const Palette& palette,
                                        int transparentIndex, bool keepAlpha, const DitherOptions& options)
{
    const int w = image.width(), h = image.height();
    auto out = std::make_shared<Image>(w, h, PixelFormat::Indexed8);
    const NearestColor nearest(palette, transparentIndex, background, options.oklab);
    constexpr int kQueue = 16;
    float weights[kQueue];
    float total = 0;
    for (int a = 0; a < kQueue; ++a) { // age 0 is the latest error
        weights[a] = std::exp(-float(a) * std::log(16.0f) / float(kQueue - 1));
        total += weights[a];
    }
    for (float& v : weights)
        v /= total;
    float queue[kQueue][3] = {};
    int head = 0;
    const float strength = float(std::clamp(options.strength, 0, 100)) / 100.0f;
    const auto clamp8 = [](float v) { return std::clamp(v, 0.0f, 255.0f); };
    int n = 1;
    while (n < std::max(w, h))
        n *= 2;
    for (int d = 0; d < n * n; ++d) {
        int x, y;
        hilbertPoint(n, d, x, y);
        if (x >= w || y >= h)
            continue;
        const Rgba c = toRgba(doc, image, x, y, background);
        if (c.a == 0 && !background) {
            out->setPixel(x, y, uint32_t(transparentIndex));
            continue;
        }
        float v[3] = {float(c.r), float(c.g), float(c.b)};
        for (int a = 0; a < kQueue; ++a) {
            const float* e = queue[(head - a + kQueue) % kQueue];
            for (int i = 0; i < 3; ++i)
                v[i] += weights[a] * e[i];
        }
        const Rgba want{uint8_t(clamp8(v[0]) + 0.5f), uint8_t(clamp8(v[1]) + 0.5f), uint8_t(clamp8(v[2]) + 0.5f),
                        keepAlpha ? c.a : uint8_t(255)};
        const uint32_t index = nearest(want);
        out->setPixel(x, y, index);
        const Rgba got = palette.color(int(index));
        head = (head + 1) % kQueue;
        queue[head][0] = (clamp8(v[0]) - got.r) * strength;
        queue[head][1] = (clamp8(v[1]) - got.g) * strength;
        queue[head][2] = (clamp8(v[2]) - got.b) * strength;
    }
    return out;
}

// One tap of an error diffusion kernel: where the error goes (dx columns, dy rows on) and how much of it.
struct Tap {
    int dx, dy;
    float weight;
};

const std::vector<Tap>& diffusionKernel(IndexedDither dither)
{
    static const std::vector<Tap> floyd{{1, 0, 7 / 16.0f}, {-1, 1, 3 / 16.0f}, {0, 1, 5 / 16.0f}, {1, 1, 1 / 16.0f}};
    // Atkinson: 6 / 8 of the error goes on, 2 / 8 is dropped.
    static const std::vector<Tap> atkinson{{1, 0, 1 / 8.0f}, {2, 0, 1 / 8.0f}, {-1, 1, 1 / 8.0f},
                                           {0, 1, 1 / 8.0f}, {1, 1, 1 / 8.0f}, {0, 2, 1 / 8.0f}};
    static const std::vector<Tap> sierra{{1, 0, 5 / 32.0f},  {2, 0, 3 / 32.0f}, {-2, 1, 2 / 32.0f}, {-1, 1, 4 / 32.0f},
                                         {0, 1, 5 / 32.0f},  {1, 1, 4 / 32.0f}, {2, 1, 2 / 32.0f},  {-1, 2, 2 / 32.0f},
                                         {0, 2, 3 / 32.0f},  {1, 2, 2 / 32.0f}};
    static const std::vector<Tap> stucki{{1, 0, 8 / 42.0f},  {2, 0, 4 / 42.0f}, {-2, 1, 2 / 42.0f}, {-1, 1, 4 / 42.0f},
                                         {0, 1, 8 / 42.0f},  {1, 1, 4 / 42.0f}, {2, 1, 2 / 42.0f},  {-2, 2, 1 / 42.0f},
                                         {-1, 2, 2 / 42.0f}, {0, 2, 4 / 42.0f}, {1, 2, 2 / 42.0f},  {2, 2, 1 / 42.0f}};
    switch (dither) {
    case IndexedDither::Atkinson: return atkinson;
    case IndexedDither::Sierra: return sierra;
    case IndexedDither::Stucki: return stucki;
    default: return floyd;
    }
}

// An image in `target`'s format with its pixels spread over the palette:
// fully transparent pixels take the transparent index, every other pixel an
// opaque palette color (partial alpha is dropped, as without dithering).
std::shared_ptr<Image> ditheredIndexed(const Document& doc, const Image& image, bool background,
                                       const Palette& palette, int transparentIndex, IndexedDither dither,
                                       bool keepAlpha, const DitherOptions& options)
{
    if (dither == IndexedDither::Riemersma)
        return riemersmaIndexed(doc, image, background, palette, transparentIndex, keepAlpha, options);
    const float strength = float(std::clamp(options.strength, 0, 100)) / 100.0f;
    const float kOrderedSpread = 48.0f * strength;
    const int w = image.width(), h = image.height();
    auto out = std::make_shared<Image>(w, h, PixelFormat::Indexed8);
    const NearestColor nearest(palette, transparentIndex, background, options.oklab);

    const bool diffuses = dither == IndexedDither::Floyd || dither == IndexedDither::Atkinson
                          || dither == IndexedDither::Sierra || dither == IndexedDither::Stucki;
    const std::vector<Tap>& kernel = diffusionKernel(dither);
    // Error still to place, per channel: this row and the next two, with two
    // columns of room each side.
    const size_t stride = size_t(w + 4) * 3;
    std::vector<float> rows[3] = {std::vector<float>(stride, 0.0f), std::vector<float>(stride, 0.0f),
                                  std::vector<float>(stride, 0.0f)};
    const auto spread = [&](int x, int dx, int dy, const float err[3], float weight) {
        float* e = &rows[dy][size_t(x + 2 + dx) * 3];
        for (int i = 0; i < 3; ++i)
            e[i] += err[i] * weight;
    };
    const auto threshold = [&](int x, int y) { return ditherThreshold(dither, x, y); };
    const bool pattern = !diffuses && dither != IndexedDither::None;
    const auto clamp8 = [](float v) { return std::clamp(v, 0.0f, 255.0f); };
    for (int y = 0; y < h; ++y) {
        // Serpentine: every other row runs the other way, and the kernel is mirrored with it.
        const bool reverse = options.serpentine && diffuses && (y & 1);
        for (int i = 0; i < w; ++i) {
            const int x = reverse ? w - 1 - i : i;
            const Rgba c = toRgba(doc, image, x, y, background);
            if (c.a == 0 && !background) {
                out->setPixel(x, y, uint32_t(transparentIndex));
                continue;
            }
            float r = c.r, g = c.g, b = c.b;
            if (diffuses) {
                const float* e = &rows[0][size_t(x + 2) * 3];
                r += e[0];
                g += e[1];
                b += e[2];
            } else if (pattern) {
                const float t = (threshold(x, y) - 0.5f) * kOrderedSpread;
                r += t;
                g += t;
                b += t;
            }
            const Rgba want{uint8_t(clamp8(r) + 0.5f), uint8_t(clamp8(g) + 0.5f), uint8_t(clamp8(b) + 0.5f), keepAlpha ? c.a : uint8_t(255)};
            const uint32_t index = nearest(want);
            out->setPixel(x, y, index);
            if (diffuses) {
                const Rgba got = palette.color(int(index));
                const float err[3] = {clamp8(r) - got.r, clamp8(g) - got.g, clamp8(b) - got.b};
                for (const Tap& tap : kernel)
                    spread(x, reverse ? -tap.dx : tap.dx, tap.dy, err, tap.weight * strength);
            }
        }
        std::rotate(std::begin(rows), std::begin(rows) + 1, std::end(rows));
        std::fill(rows[2].begin(), rows[2].end(), 0.0f);
    }
    return out;
}

} // namespace

float ditherThreshold(IndexedDither pattern, int x, int y)
{
    switch (pattern) {
    case IndexedDither::Bayer2: return (bayerRank(x, y, 1) + 0.5f) / 4.0f;
    case IndexedDither::Ordered: return (bayerRank(x, y, 2) + 0.5f) / 16.0f;
    case IndexedDither::Bayer8: return (bayerRank(x, y, 3) + 0.5f) / 64.0f;
    case IndexedDither::BlueNoise: return (blueNoiseRank(x, y) + 0.5f) / 4096.0f;
    default: return 0.5f;
    }
}

Image ditherToPalette(const Document& doc, const Image& image, bool background, const Palette& palette,
                      int transparentIndex, IndexedDither dither, bool keepAlpha, const DitherOptions& options)
{
    return *ditheredIndexed(doc, image, background, palette, transparentIndex, dither, keepAlpha, options);
}

int blueNoiseRank(int x, int y)
{
    static const std::vector<uint16_t> tile = makeBlueNoise();
    return tile[size_t((y & 63) * 64 + (x & 63))];
}

std::unique_ptr<SetColorModeCommand> colorModeCommand(const Document& doc, ColorMode target, IndexedPalette palette,
                                                      IndexedDither dither, int onlyFrame, int maxColors,
                                                      const Palette* given, bool keepAlpha, const DitherOptions& options)
{
    // An indexed sprite can be remade with a new palette (its own colors, fewer,
    // or another); only "the same palette" is nothing to do.
    const bool remake = target == ColorMode::Indexed && doc.colorMode() == ColorMode::Indexed
                        && palette != IndexedPalette::Current;
    if ((target == doc.colorMode() && !remake) || !canChangeColorMode(doc))
        return nullptr;

    SetColorModeCommand::State before{doc.colorMode(), doc.palette, doc.transparentIndex, doc.tilesets};
    SetColorModeCommand::State after = before;
    after.mode = target;
    if (target == ColorMode::Indexed && palette == IndexedPalette::FromSprite) {
        after.palette = paletteFromSprite(doc, maxColors, keepAlpha);
        after.transparentIndex = 0;
    } else if (target == ColorMode::Indexed && palette == IndexedPalette::Given && given) {
        after.palette = *given;
        after.transparentIndex = 0;
    }
    Document shape(1, 1, target); // only for its pixel format
    const PixelFormat format = shape.pixelFormat();

    std::vector<SetColorModeCommand::CelChange> changes;
    std::unordered_set<const CelData*> seen;
    std::unordered_map<const Image*, std::shared_ptr<Image>> converted; // images shared by cels stay shared
    doc.forEachLayer([&](const Layer& layer) {
        if (layer.kind != LayerKind::Image)
            return;
        const bool background = layer.isBackground();
        for (size_t f = 0; f < layer.cels.size(); ++f) {
            const CelRef& cel = layer.cels[f];
            if (onlyFrame >= 0 && int(f) != onlyFrame)
                continue;
            if (!cel || !cel->image || !seen.insert(cel.get()).second)
                continue;
            const Image& image = *cel->image;
            auto& out = converted[&image];
            const bool dithered = target == ColorMode::Indexed && (dither != IndexedDither::None || options.oklab);
            if (!out && dithered) {
                // A shared image is dithered once, whichever layer sees it first.
                out = ditheredIndexed(doc, image, background, after.palette, after.transparentIndex, dither, keepAlpha, options);
            } else if (!out) {
                out = std::make_shared<Image>(image.width(), image.height(), format);
                std::unordered_map<uint32_t, uint32_t> cache; // nearest-color search is slow
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const uint32_t v = image.pixel(x, y);
                        auto it = cache.find(v);
                        if (it == cache.end()) {
                            const Rgba c = toRgba(doc, image, x, y, background);
                            it = cache.emplace(v, fromRgba(target, after.palette, after.transparentIndex, c,
                                                           background)).first;
                        }
                        out->setPixel(x, y, it->second);
                    }
            }
            changes.push_back({cel, cel->image, out});
        }
    });
    // Tiles convert the same way (never as a background).
    for (Tileset& set : after.tilesets)
        for (auto& tile : set.tiles) {
            if (!tile)
                continue;
            if (target == ColorMode::Indexed && (dither != IndexedDither::None || options.oklab)) {
                tile = ditheredIndexed(doc, *tile, false, after.palette, after.transparentIndex, dither, keepAlpha, options);
                continue;
            }
            auto out = std::make_shared<Image>(tile->width(), tile->height(), format);
            for (int y = 0; y < tile->height(); ++y)
                for (int x = 0; x < tile->width(); ++x)
                    out->setPixel(x, y, fromRgba(target, after.palette, after.transparentIndex, toRgba(doc, *tile, x, y)));
            tile = std::move(out);
        }
    return std::make_unique<SetColorModeCommand>(std::move(before), std::move(after), std::move(changes));
}

} // namespace core
