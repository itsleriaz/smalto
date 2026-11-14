#include "core/Raster.h"

#include "core/BlendMode.h"

#include <algorithm>
#include <map>

#include "core/Color.h"
#include "core/Transform.h"

#include <vector>

namespace core {

const std::vector<std::pair<int, int>>& brushSpans(int size, BrushShape shape)
{
    size = std::max(1, size);
    // Round brushes are computed once per size (sizes are small).
    static std::map<int, std::vector<std::pair<int, int>>> round;
    static std::map<int, std::vector<std::pair<int, int>>> square;
    auto& cache = shape == BrushShape::Round ? round : square;
    auto it = cache.find(size);
    if (it == cache.end()) {
        std::vector<std::pair<int, int>> spans;
        if (shape == BrushShape::Round)
            spans = ellipseSpans({0, 0, size, size});
        else
            spans.assign(size_t(size), {0, size - 1});
        it = cache.emplace(size, std::move(spans)).first;
    }
    return it->second;
}

Rect stampBrush(Image& image, Point center, int size, uint32_t value, BrushShape shape)
{
    size = std::max(1, size);
    const Rect area = brushRect(center, size);
    if (shape == BrushShape::Square || size <= 2) { // round 1 and 2 are squares too
        const Rect r = area.intersected(image.bounds());
        image.fillRect(r, value);
        return r;
    }
    Rect changed;
    const auto& spans = brushSpans(size, shape);
    for (int i = 0; i < size; ++i) {
        const Rect row = Rect{area.x + spans[size_t(i)].first, area.y + i,
                              spans[size_t(i)].second - spans[size_t(i)].first + 1, 1}
                             .intersected(image.bounds());
        image.fillRect(row, value);
        changed = changed.united(row);
    }
    return changed;
}

ImageBrush flipped(const ImageBrush& brush, bool horizontally, bool vertically)
{
    ImageBrush out = brush;
    if (horizontally) {
        out.pixels = flippedHorizontally(out.pixels);
        out.footprint = flippedHorizontally(out.footprint);
    }
    if (vertically) {
        out.pixels = flippedVertically(out.pixels);
        out.footprint = flippedVertically(out.footprint);
    }
    return out;
}

namespace {

// Calls f(x, y, bx, by) for each covered pixel inside the image (image and
// brush coordinates); returns the brush rect clipped to the image.
template <class F>
Rect forFootprint(const Image& image, Point center, const ImageBrush& brush, F&& f)
{
    const Rect area = brush.rectAt(center);
    const Rect r = area.intersected(image.bounds());
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x)
            if (brush.footprint.pixel(x - area.x, y - area.y))
                f(x, y, x - area.x, y - area.y);
    return r;
}

} // namespace

Rect stampImageBrush(Image& image, Point center, const ImageBrush& brush)
{
    return forFootprint(image, center, brush,
                        [&](int x, int y, int bx, int by) { image.setPixel(x, y, brush.pixels.pixel(bx, by)); });
}

Rect stampImageBrushBlended(Image& image, Point center, const ImageBrush& brush)
{
    return forFootprint(image, center, brush, [&](int x, int y, int bx, int by) {
        image.setPixel(x, y, pack(blend(unpack(image.pixel(x, y)), unpack(brush.pixels.pixel(bx, by)), 255, BlendMode::Normal)));
    });
}

Rect stampImageBrush(Image& image, Point center, const ImageBrush& brush, uint32_t value)
{
    return forFootprint(image, center, brush, [&](int x, int y, int, int) { image.setPixel(x, y, value); });
}

Rect shadeImageBrush(Image& image, const Image& before, Point center, const ImageBrush& brush,
                     const std::vector<uint32_t>& ramp, int step)
{
    return forFootprint(image, center, brush, [&](int x, int y, int, int) {
        if (const auto v = shadeValue(before.pixel(x, y), ramp, step))
            image.setPixel(x, y, *v);
    });
}

Rect opaqueBounds(const Image& rgba)
{
    Rect bounds;
    for (int y = 0; y < rgba.height(); ++y)
        for (int x = 0; x < rgba.width(); ++x)
            if (rgba.pixel(x, y) >> 24)
                bounds = bounds.united({x, y, 1, 1});
    return bounds;
}

bool ditherOn(const DitherPattern& pattern, int x, int y)
{
    static constexpr int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const auto mod = [](int v, int m) { return ((v % m) + m) % m; };
    switch (pattern.kind) {
    case DitherPattern::Checker: return mod(x + y, 2) == 0;
    case DitherPattern::Ordered: return bayer[mod(y, 4)][mod(x, 4)] < std::clamp(pattern.density, 0, 16);
    case DitherPattern::HorizontalLines: return mod(y, 2) == 0;
    case DitherPattern::VerticalLines: return mod(x, 2) == 0;
    case DitherPattern::DiagonalLines: return mod(x + y, 4) == 0;
    }
    return true;
}

void applyDither(Image& image, const Image& before, const Image& covered, Rect area, Point origin,
                 const DitherPattern& pattern, std::optional<uint32_t> offValue)
{
    area = area.intersected(image.bounds());
    for (int y = area.y; y < area.bottom(); ++y)
        for (int x = area.x; x < area.right(); ++x)
            if (covered.pixel(x, y) && !ditherOn(pattern, x + origin.x, y + origin.y))
                image.setPixel(x, y, offValue ? *offValue : before.pixel(x, y));
}

std::optional<uint32_t> shadeValue(uint32_t value, const std::vector<uint32_t>& ramp, int step)
{
    const auto it = std::find(ramp.begin(), ramp.end(), value);
    if (it == ramp.end())
        return std::nullopt;
    const int i = std::clamp(int(it - ramp.begin()) + step, 0, int(ramp.size()) - 1);
    return ramp[size_t(i)];
}

Rect shadeBrush(Image& image, const Image& before, Point center, int size, const std::vector<uint32_t>& ramp, int step,
                BrushShape shape)
{
    size = std::max(1, size);
    const Rect area = brushRect(center, size);
    const bool square = shape == BrushShape::Square || size <= 2;
    const auto& spans = brushSpans(size, square ? BrushShape::Square : shape);
    Rect changed;
    for (int i = 0; i < size; ++i) {
        const Rect row = Rect{area.x + spans[size_t(i)].first, area.y + i,
                              spans[size_t(i)].second - spans[size_t(i)].first + 1, 1}
                             .intersected(image.bounds());
        for (int x = row.x; x < row.x + row.w; ++x)
            if (const auto v = shadeValue(before.pixel(x, row.y), ramp, step))
                image.setPixel(x, row.y, *v);
        changed = changed.united(row);
    }
    return changed;
}

namespace {

bool matches(const Image& image, uint32_t a, uint32_t b, int tolerance)
{
    if (a == b)
        return true;
    if (image.format() == PixelFormat::Gray16) {
        // Value and alpha; fully transparent pixels match each other (at any tolerance).
        const int va = int(a & 0xff), aa = int(a >> 8), vb = int(b & 0xff), ab = int(b >> 8);
        if (aa == 0 && ab == 0)
            return true;
        if (tolerance <= 0)
            return false;
        return std::abs(va - vb) <= tolerance && std::abs(aa - ab) <= tolerance;
    }
    if (image.format() != PixelFormat::Rgba8)
        return false; // indexed: exact indices
    const Rgba ca = unpack(a);
    const Rgba cb = unpack(b);
    // Fully transparent pixels match each other regardless of color (and tolerance).
    if (ca.a == 0 && cb.a == 0)
        return true;
    if (tolerance <= 0)
        return false;
    return std::abs(ca.r - cb.r) <= tolerance && std::abs(ca.g - cb.g) <= tolerance
        && std::abs(ca.b - cb.b) <= tolerance && std::abs(ca.a - cb.a) <= tolerance;
}

} // namespace

// Runs of pixels a fill from `start` reaches inside `area` (which holds
// start): calls span(l, r, y) for each, l..r inclusive, every pixel once.
template <class Span>
static void matchingSpans(const Image& image, Point start, bool contiguous, int tolerance, Rect area,
                          const Mask* mask, Span&& span)
{
    auto inMask = [&](int x, int y) { return !mask || mask->contains(x, y); };
    const uint32_t target = image.pixel(start.x, start.y);
    if (!contiguous) {
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x)
                if (inMask(x, y) && matches(image, image.pixel(x, y), target, tolerance))
                    span(x, x, y);
        return;
    }

    // Scanline fill. `visited` keeps it from going round in circles (a fill
    // changes the pixels it leaves behind, and they may still match).
    std::vector<uint8_t> visited(size_t(image.width()) * image.height(), 0);
    auto open = [&](int x, int y) {
        return !visited[size_t(y) * image.width() + x] && inMask(x, y)
            && matches(image, image.pixel(x, y), target, tolerance);
    };
    std::vector<Point> stack{start};
    while (!stack.empty()) {
        Point p = stack.back();
        stack.pop_back();
        if (!open(p.x, p.y))
            continue;
        int l = p.x;
        while (l > area.x && open(l - 1, p.y))
            --l;
        int r = p.x;
        while (r + 1 < area.right() && open(r + 1, p.y))
            ++r;
        for (int x = l; x <= r; ++x)
            visited[size_t(p.y) * image.width() + x] = 1;
        for (int ny : {p.y - 1, p.y + 1}) {
            if (ny < area.y || ny >= area.bottom())
                continue;
            bool inSpan = false;
            for (int x = l; x <= r; ++x) {
                const bool o = open(x, ny);
                if (o && !inSpan)
                    stack.push_back({x, ny});
                inSpan = o;
            }
        }
        span(l, r, p.y);
    }
}

Rect floodFill(Image& image, Point start, uint32_t value, bool contiguous, int tolerance, Rect clip,
               const Mask* mask)
{
    Rect area = clip.isEmpty() ? image.bounds() : clip.intersected(image.bounds());
    if (mask)
        area = area.intersected(mask->bounds());
    if (!area.contains(start) || (mask && !mask->contains(start)))
        return {};
    if (image.pixel(start.x, start.y) == value)
        return {};
    Rect dirty;
    matchingSpans(image, start, contiguous, tolerance, area, mask, [&](int l, int r, int y) {
        for (int x = l; x <= r; ++x)
            image.setPixel(x, y, value);
        dirty = dirty.united({l, y, r - l + 1, 1});
    });
    return dirty;
}

Mask similarMaskWrapped(const Image& image, Point start, bool contiguous, int tolerance, bool wrapX, bool wrapY)
{
    if (!contiguous || (!wrapX && !wrapY))
        return similarMask(image, start, contiguous, tolerance);
    if (!image.bounds().contains(start))
        return {};
    const int w = image.width(), h = image.height();
    Image bits(w, h, PixelFormat::Indexed8);
    const uint32_t seed = image.pixel(start.x, start.y);
    std::vector<Point> queue{start};
    bits.setPixel(start.x, start.y, 1);
    for (size_t head = 0; head < queue.size(); ++head) {
        const Point p = queue[head];
        static constexpr int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : steps) {
            int x = p.x + d[0], y = p.y + d[1];
            if (x < 0 || x >= w)
                x = wrapX ? (x + w) % w : -1;
            if (y < 0 || y >= h)
                y = wrapY ? (y + h) % h : -1;
            if (x < 0 || y < 0 || bits.pixel(x, y) || !matches(image, seed, image.pixel(x, y), tolerance))
                continue;
            bits.setPixel(x, y, 1);
            queue.push_back({x, y});
        }
    }
    return Mask::fromImage(bits, {0, 0});
}

Mask similarMask(const Image& image, Point start, bool contiguous, int tolerance)
{
    if (!image.bounds().contains(start))
        return {};
    Image bits(image.width(), image.height(), PixelFormat::Indexed8);
    matchingSpans(image, start, contiguous, tolerance, image.bounds(), nullptr, [&](int l, int r, int y) {
        for (int x = l; x <= r; ++x)
            bits.setPixel(x, y, 1);
    });
    return Mask::fromImage(bits, {0, 0});
}

Mask polygonMask(const std::vector<Point>& points, Rect clip)
{
    if (points.empty() || clip.isEmpty())
        return {};
    Rect bounds{points[0].x, points[0].y, 1, 1};
    for (Point p : points)
        bounds = bounds.united({p.x, p.y, 1, 1});
    const Rect area = bounds.intersected(clip);
    if (area.isEmpty())
        return {};
    Image bits(area.w, area.h, PixelFormat::Indexed8);
    auto set = [&](int x, int y) {
        if (area.contains(x, y))
            bits.setPixel(x - area.x, y - area.y, 1);
    };
    // Inside (even-odd, at pixel centers): per row, where the closed outline
    // crosses it, filled between pairs of crossings.
    std::vector<double> xs;
    const size_t n = points.size();
    for (int y = area.y; y < area.bottom(); ++y) {
        xs.clear();
        for (size_t i = 0; i < n; ++i) {
            const Point a = points[i];
            const Point b = points[(i + 1) % n];
            if ((a.y > y) != (b.y > y))
                xs.push_back(a.x + double(y - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            for (int x = std::max(area.x, int(std::ceil(xs[i]))); x <= std::min(area.right() - 1, int(std::floor(xs[i + 1]))); ++x)
                set(x, y);
    }
    // The outline itself, so thin shapes and lines keep every pixel drawn.
    for (size_t i = 0; i < n; ++i)
        bresenham(points[i], points[(i + 1) % n], [&](Point p) { set(p.x, p.y); });
    return Mask::fromImage(bits, {area.x, area.y});
}

Rect shapeRect(Point a, Point b, bool square, bool fromCenter)
{
    int dx = b.x - a.x;
    int dy = b.y - a.y;
    if (square) {
        const int d = std::max(std::abs(dx), std::abs(dy));
        dx = dx < 0 ? -d : d;
        dy = dy < 0 ? -d : d;
    }
    if (fromCenter) {
        const int rx = std::abs(dx), ry = std::abs(dy);
        return {a.x - rx, a.y - ry, 2 * rx + 1, 2 * ry + 1};
    }
    return {std::min(a.x, a.x + dx), std::min(a.y, a.y + dy), std::abs(dx) + 1, std::abs(dy) + 1};
}

Point snapLine(Point a, Point b)
{
    const int dx = b.x - a.x;
    const int dy = b.y - a.y;
    const int adx = std::abs(dx), ady = std::abs(dy);
    // Within 22.5 degrees of an axis: that axis; otherwise the diagonal.
    if (ady * 1000 <= adx * 414)
        return {b.x, a.y};
    if (adx * 1000 <= ady * 414)
        return {a.x, b.y};
    const int d = std::max(adx, ady);
    return {a.x + (dx < 0 ? -d : d), a.y + (dy < 0 ? -d : d)};
}

std::vector<std::pair<int, int>> ellipseSpans(Rect r)
{
    std::vector<std::pair<int, int>> rows(size_t(std::max(0, r.h)), {r.right(), r.x - 1});
    ellipseOutline(r, [&](Point p) {
        if (p.y < r.y || p.y >= r.bottom())
            return;
        auto& [first, last] = rows[size_t(p.y - r.y)];
        first = std::min(first, p.x);
        last = std::max(last, p.x);
    });
    return rows;
}

std::optional<Point> PixelPerfectFilter::push(Point p)
{
    if (m_count > 0 && p == m_last)
        return std::nullopt;
    auto adjacent = [](Point a, Point b) { return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1; };
    // prev, last, p forming an L: drop `last`, keep prev as the pixel before p.
    if (m_count >= 2 && adjacent(m_last, p) && std::abs(m_prev.x - p.x) == 1 && std::abs(m_prev.y - p.y) == 1) {
        const Point corner = m_last;
        m_last = p;
        return corner;
    }
    m_prev = m_last;
    m_last = p;
    m_count = std::min(m_count + 1, 2);
    return std::nullopt;
}

void keepAlpha(Image& image, const Image& before, Rect area, uint32_t transparent)
{
    area = area.intersected(image.bounds()).intersected(before.bounds());
    const PixelFormat format = image.format();
    const auto alphaOf = [&](uint32_t v) -> uint32_t {
        switch (format) {
        case PixelFormat::Rgba8: return v >> 24;
        case PixelFormat::Gray16: return v >> 8;
        default: return v == transparent ? 0 : 255;
        }
    };
    for (int y = area.y; y < area.bottom(); ++y)
        for (int x = area.x; x < area.right(); ++x) {
            const uint32_t was = before.pixel(x, y), now = image.pixel(x, y);
            if (alphaOf(was) == 0 || alphaOf(now) == 0) {
                image.setPixel(x, y, was);
            } else if (format == PixelFormat::Rgba8) {
                image.setPixel(x, y, (now & 0x00ffffffu) | (was & 0xff000000u));
            } else if (format == PixelFormat::Gray16) {
                image.setPixel(x, y, (now & 0x00ffu) | (was & 0xff00u));
            }
        }
}

} // namespace core
