#include <doctest/doctest.h>

#include <set>

#include "core/Color.h"
#include "core/Raster.h"

#include <vector>

using namespace core;

TEST_CASE("bresenham covers endpoints without gaps")
{
    std::vector<Point> pts;
    bresenham({0, 0}, {5, 2}, [&](Point p) { pts.push_back(p); });
    CHECK(pts.front() == Point{0, 0});
    CHECK(pts.back() == Point{5, 2});
    CHECK(pts.size() == 6);
    for (size_t i = 1; i < pts.size(); ++i) {
        CHECK(std::abs(pts[i].x - pts[i - 1].x) <= 1);
        CHECK(std::abs(pts[i].y - pts[i - 1].y) <= 1);
    }
}

TEST_CASE("brush rect is centered")
{
    CHECK(brushRect({5, 5}, 1) == Rect{5, 5, 1, 1});
    CHECK(brushRect({5, 5}, 3) == Rect{4, 4, 3, 3});
    CHECK(brushRect({5, 5}, 2) == Rect{4, 4, 2, 2});
}

TEST_CASE("contiguous flood fill stops at borders")
{
    Image img(5, 5);
    // Vertical wall at x = 2.
    img.fillRect({2, 0, 1, 5}, 0xff000001);
    const Rect r = floodFill(img, {0, 0}, 7, true);
    CHECK(r == Rect{0, 0, 2, 5});
    CHECK(img.pixel(1, 4) == 7);
    CHECK(img.pixel(2, 2) == 0xff000001);
    CHECK(img.pixel(3, 0) == 0);
}

TEST_CASE("global fill replaces all matches")
{
    Image img(5, 5);
    img.fillRect({2, 0, 1, 5}, 0xff000001);
    floodFill(img, {0, 0}, 7, false);
    CHECK(img.pixel(4, 4) == 7);
    CHECK(img.pixel(2, 2) == 0xff000001);
}

TEST_CASE("fill with same color is a no-op")
{
    Image img(3, 3);
    CHECK(floodFill(img, {1, 1}, 0, true).isEmpty());
}

namespace {

std::set<std::pair<int, int>> ellipsePixels(Rect r)
{
    std::set<std::pair<int, int>> out;
    ellipseOutline(r, [&](Point p) { out.insert({p.x, p.y}); });
    return out;
}

} // namespace

TEST_CASE("ellipse fits its rect, touches every side and is symmetric")
{
    for (int w = 1; w <= 24; ++w)
        for (int h = 1; h <= 24; ++h) {
            CAPTURE(w);
            CAPTURE(h);
            const Rect r{3, 5, w, h};
            const auto pixels = ellipsePixels(r);
            bool left = false, right = false, top = false, bottom = false, inside = true, mirrored = true;
            for (const auto& [x, y] : pixels) {
                inside = inside && r.contains(x, y);
                left = left || x == r.x;
                right = right || x == r.right() - 1;
                top = top || y == r.y;
                bottom = bottom || y == r.bottom() - 1;
                const int mx = r.x + r.right() - 1 - x;
                const int my = r.y + r.bottom() - 1 - y;
                mirrored = mirrored && pixels.count({mx, y}) && pixels.count({x, my});
            }
            CHECK(inside);
            CHECK((left && right && top && bottom));
            CHECK(mirrored);

            // Filled spans cover every row and stay inside the rect.
            const auto spans = ellipseSpans(r);
            REQUIRE(spans.size() == size_t(h));
            for (const auto& [first, last] : spans) {
                CHECK(first <= last);
                CHECK(first >= r.x);
                CHECK(last < r.right());
            }
        }
    CHECK(ellipsePixels({0, 0, 1, 1}) == std::set<std::pair<int, int>>{{0, 0}});
    CHECK(ellipsePixels({0, 0, 2, 2}).size() == 4);
}

TEST_CASE("rect outline visits each border pixel once")
{
    int count = 0;
    std::set<std::pair<int, int>> seen;
    rectOutline({2, 3, 5, 4}, [&](Point p) {
        ++count;
        seen.insert({p.x, p.y});
    });
    CHECK(count == 2 * 5 + 2 * 2);
    CHECK(seen.size() == size_t(count));
    count = 0;
    rectOutline({0, 0, 1, 1}, [&](Point) { ++count; });
    CHECK(count == 1);
    count = 0;
    rectOutline({0, 0, 4, 1}, [&](Point) { ++count; });
    CHECK(count == 4);
}

TEST_CASE("shape rects and line snapping")
{
    CHECK(shapeRect({5, 5}, {2, 8}, false, false) == Rect{2, 5, 4, 4});
    CHECK(shapeRect({5, 5}, {7, 9}, true, false) == Rect{5, 5, 5, 5});
    CHECK(shapeRect({5, 5}, {3, 6}, true, false) == Rect{3, 5, 3, 3}); // grows toward the drag
    CHECK(shapeRect({5, 5}, {7, 6}, false, true) == Rect{3, 4, 5, 3});
    CHECK(shapeRect({5, 5}, {5, 5}, false, false) == Rect{5, 5, 1, 1});

    CHECK(snapLine({0, 0}, {10, 2}) == Point{10, 0});
    CHECK(snapLine({0, 0}, {-1, -9}) == Point{0, -9});
    CHECK(snapLine({0, 0}, {6, -5}) == Point{6, -6});
}

TEST_CASE("pixel-perfect filter drops L corners only")
{
    PixelPerfectFilter filter;
    // A staircase drawn as 4-connected steps: (0,0) (1,0) (1,1) (2,1) (2,2)
    std::vector<Point> dropped;
    for (Point p : {Point{0, 0}, Point{1, 0}, Point{1, 1}, Point{2, 1}, Point{2, 2}})
        if (auto d = filter.push(p))
            dropped.push_back(*d);
    CHECK(dropped == std::vector<Point>{{1, 0}, {2, 1}});

    // Straight lines and true diagonals lose nothing; repeats are ignored.
    PixelPerfectFilter straight;
    for (Point p : {Point{0, 0}, Point{1, 0}, Point{1, 0}, Point{2, 0}, Point{3, 1}, Point{4, 2}})
        CHECK_FALSE(straight.push(p));
}

TEST_CASE("flood fill stays inside the clip rect")
{
    Image img(6, 6);
    const Rect r = floodFill(img, {3, 3}, 7, true, 0, {2, 2, 3, 3});
    CHECK(r == Rect{2, 2, 3, 3});
    CHECK(img.pixel(1, 1) == 0);
    CHECK(img.pixel(4, 4) == 7);
    CHECK(floodFill(img, {0, 0}, 9, true, 0, {2, 2, 3, 3}).isEmpty()); // start outside
    Image global(4, 1);
    floodFill(global, {1, 0}, 5, false, 0, {1, 0, 2, 1});
    CHECK(global.pixel(0, 0) == 0);
    CHECK(global.pixel(2, 0) == 5);
    CHECK(global.pixel(3, 0) == 0);
}

TEST_CASE("flood fill tolerance in RGBA and grayscale")
{
    Image rgba(3, 1);
    rgba.setPixel(0, 0, pack({100, 100, 100, 255}));
    rgba.setPixel(1, 0, pack({110, 100, 100, 255}));
    rgba.setPixel(2, 0, pack({140, 100, 100, 255}));
    Image exact = rgba;
    floodFill(exact, {0, 0}, pack({0, 0, 0, 255}), true, 0);
    CHECK(exact.pixel(1, 0) == pack({110, 100, 100, 255}));
    Image loose = rgba;
    floodFill(loose, {0, 0}, pack({0, 0, 0, 255}), true, 20);
    CHECK(loose.pixel(1, 0) == pack({0, 0, 0, 255}));
    CHECK(loose.pixel(2, 0) == pack({140, 100, 100, 255}));

    Image gray(3, 1, PixelFormat::Gray16);
    gray.setPixel(0, 0, 50u | 255u << 8);
    gray.setPixel(1, 0, 60u | 255u << 8);
    gray.setPixel(2, 0, 90u | 255u << 8);
    floodFill(gray, {0, 0}, 0u | 255u << 8, true, 15);
    CHECK(gray.pixel(1, 0) == (0u | 255u << 8));
    CHECK(gray.pixel(2, 0) == (90u | 255u << 8));

    Image indexed(2, 1, PixelFormat::Indexed8);
    indexed.setPixel(1, 0, 1);
    floodFill(indexed, {0, 0}, 5, true, 255); // indices match exactly
    CHECK(indexed.pixel(0, 0) == 5);
    CHECK(indexed.pixel(1, 0) == 1);
}

TEST_CASE("flood fill stays inside a mask and doesn't cross its gaps")
{
    Image img(6, 1, PixelFormat::Indexed8);
    // Selected: 0-1 and 3-5; pixel 2 isn't, so the fill can't get past it.
    const Mask mask = Mask(Rect{0, 0, 6, 1}).subtracted(Mask(Rect{2, 0, 1, 1}));
    const Rect r = floodFill(img, {0, 0}, 7, true, 0, {}, &mask);
    CHECK(r == Rect{0, 0, 2, 1});
    CHECK(img.pixel(2, 0) == 0);
    CHECK(img.pixel(3, 0) == 0);
    floodFill(img, {4, 0}, 9, false, 0, {}, &mask); // global: every masked match
    CHECK(img.pixel(2, 0) == 0);
    CHECK(img.pixel(5, 0) == 9);
    CHECK(floodFill(img, {2, 0}, 3, true, 0, {}, &mask).isEmpty()); // start outside
}

TEST_CASE("similar mask: the pixels a fill would reach, the image untouched")
{
    // Two 0 areas split by a column of 5; one pixel of 6 inside the left one.
    Image img(5, 3, PixelFormat::Indexed8);
    for (int y = 0; y < 3; ++y)
        img.setPixel(2, y, 5);
    img.setPixel(0, 1, 6);
    const Mask left = similarMask(img, {1, 1}, true);
    CHECK(left.bounds() == Rect{0, 0, 2, 3});
    CHECK_FALSE(left.contains(0, 1));
    CHECK_FALSE(left.contains(3, 0));
    CHECK(img.pixel(1, 1) == 0);

    const Mask all = similarMask(img, {1, 1}, false); // global: both sides
    CHECK(all.contains(4, 2));
    CHECK_FALSE(all.contains(2, 0));
    CHECK(similarMask(img, {2, 0}, true) == Mask(Rect{2, 0, 1, 3}));
    CHECK(similarMask(img, {9, 9}, true).isEmpty());

    // Tolerance, as in floodFill.
    Image rgba(3, 1, PixelFormat::Rgba8);
    rgba.setPixel(0, 0, pack({100, 0, 0, 255}));
    rgba.setPixel(1, 0, pack({110, 0, 0, 255}));
    rgba.setPixel(2, 0, pack({200, 0, 0, 255}));
    CHECK(similarMask(rgba, {0, 0}, true, 10) == Mask(Rect{0, 0, 2, 1}));
    CHECK(similarMask(rgba, {0, 0}, true, 0) == Mask(Rect{0, 0, 1, 1}));
}

TEST_CASE("polygon mask: inside and outline, clipped")
{
    // A square outline: every pixel of the 5x5 square.
    const std::vector<Point> square{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    CHECK(polygonMask(square, {0, 0, 10, 10}) == Mask(Rect{0, 0, 5, 5}));
    CHECK(polygonMask(square, {2, 2, 10, 10}) == Mask(Rect{2, 2, 3, 3}));
    CHECK(polygonMask(square, {6, 6, 2, 2}).isEmpty());

    // A triangle: the right angle's corner is in, the far corner isn't.
    const Mask tri = polygonMask({{0, 0}, {6, 0}, {0, 6}}, {0, 0, 10, 10});
    CHECK(tri.bounds() == Rect{0, 0, 7, 7});
    CHECK(tri.contains(1, 1));
    CHECK(tri.contains(3, 3));
    CHECK_FALSE(tri.contains(5, 5));

    // An open stroke is closed by a line back to its start; a single point
    // is that pixel.
    const Mask line = polygonMask({{1, 1}, {3, 1}}, {0, 0, 10, 10});
    CHECK(line == Mask(Rect{1, 1, 3, 1}));
    CHECK(polygonMask({{2, 3}}, {0, 0, 10, 10}) == Mask(Rect{2, 3, 1, 1}));

    // A concave U: the notch stays out.
    const Mask u = polygonMask({{0, 0}, {2, 0}, {2, 4}, {4, 4}, {4, 0}, {6, 0}, {6, 6}, {0, 6}}, {0, 0, 10, 10});
    CHECK(u.contains(1, 2));
    CHECK(u.contains(5, 2));
    CHECK_FALSE(u.contains(3, 2));
    CHECK(u.contains(3, 5));
}

TEST_CASE("round brush: small sizes, symmetry, as the ellipse tool")
{
    auto footprint = [](int size) {
        Image img(size + 2, size + 2, PixelFormat::Indexed8);
        stampBrush(img, {1 + size / 2, 1 + size / 2}, size, 1, BrushShape::Round);
        return img;
    };
    // 1 and 2 are full squares; 3 is a plus.
    CHECK(footprint(2).pixel(1, 1) == 1);
    CHECK(footprint(2).pixel(2, 2) == 1);
    const Image three = footprint(3);
    CHECK(three.pixel(2, 1) == 1);
    CHECK(three.pixel(1, 2) == 1);
    CHECK(three.pixel(2, 2) == 1);
    CHECK(three.pixel(1, 1) == 0); // corners left out
    CHECK(three.pixel(3, 3) == 0);
    // Every size is symmetric left / right and up / down, and touches all
    // four sides of its square.
    for (int size = 1; size <= 16; ++size) {
        CAPTURE(size);
        const Image img = footprint(size);
        bool left = false, top = false;
        for (int y = 1; y <= size; ++y)
            for (int x = 1; x <= size; ++x) {
                CHECK(img.pixel(x, y) == img.pixel(size + 1 - x, y));
                CHECK(img.pixel(x, y) == img.pixel(x, size + 1 - y));
                left = left || (x == 1 && img.pixel(x, y));
                top = top || (y == 1 && img.pixel(x, y));
            }
        CHECK(left);
        CHECK(top);
        CHECK(img.pixel(0, 0) == 0); // nothing outside
    }
    // The square brush is unchanged.
    Image sq(5, 5, PixelFormat::Indexed8);
    CHECK(stampBrush(sq, {2, 2}, 3, 1) == Rect{1, 1, 3, 3});
    CHECK(sq.pixel(1, 1) == 1);
}

TEST_CASE("global fill at tolerance 0 treats all fully transparent pixels as equal")
{
    Image img(3, 1);
    img.setPixel(0, 0, 0);
    img.setPixel(1, 0, 0x00ff8040); // alpha 0, other color bits set
    img.setPixel(2, 0, 0xff0000ff); // opaque
    floodFill(img, {0, 0}, 7, false);
    CHECK(img.pixel(0, 0) == 7);
    CHECK(img.pixel(1, 0) == 7);
    CHECK(img.pixel(2, 0) == 0xff0000ff);
}

TEST_CASE("shade value: steps along the ramp, clamps at its ends, skips other values")
{
    const std::vector<uint32_t> ramp{10, 20, 30, 40};
    CHECK(shadeValue(20, ramp, 1) == 30u);
    CHECK(shadeValue(20, ramp, -1) == 10u);
    CHECK(shadeValue(40, ramp, 1) == 40u);
    CHECK(shadeValue(10, ramp, -1) == 10u);
    CHECK(shadeValue(10, ramp, 2) == 30u);
    CHECK_FALSE(shadeValue(25, ramp, 1).has_value());
    CHECK_FALSE(shadeValue(10, {}, 1).has_value());
    // A value listed twice counts at its first entry.
    CHECK(shadeValue(1, {1, 2, 1, 3}, 1) == 2u);
}

TEST_CASE("shade brush: shifts from the before image once, in every format")
{
    for (const PixelFormat format : {PixelFormat::Rgba8, PixelFormat::Gray16, PixelFormat::Indexed8}) {
        CAPTURE(int(format));
        const uint32_t a = format == PixelFormat::Rgba8 ? pack({255, 0, 0, 255}) : format == PixelFormat::Gray16 ? 0xff40u : 3u;
        const uint32_t b = format == PixelFormat::Rgba8 ? pack({0, 255, 0, 255}) : format == PixelFormat::Gray16 ? 0xff80u : 5u;
        const uint32_t c = format == PixelFormat::Rgba8 ? pack({0, 0, 255, 255}) : format == PixelFormat::Gray16 ? 0xffc0u : 7u;
        const uint32_t other = format == PixelFormat::Rgba8 ? pack({9, 9, 9, 255}) : format == PixelFormat::Gray16 ? 0xff09u : 9u;
        const std::vector<uint32_t> ramp{a, b, c};

        Image img(4, 1, format);
        img.setPixel(0, 0, a);
        img.setPixel(1, 0, b);
        img.setPixel(2, 0, other);
        img.setPixel(3, 0, c);
        const Image before = img;

        // Overlapping stamps: each pixel moves one step only.
        CHECK(shadeBrush(img, before, {1, 0}, 3, ramp, 1) == Rect{0, 0, 3, 1});
        shadeBrush(img, before, {2, 0}, 3, ramp, 1);
        CHECK(img.pixel(0, 0) == b);
        CHECK(img.pixel(1, 0) == c);
        CHECK(img.pixel(2, 0) == other);
        CHECK(img.pixel(3, 0) == c); // clamped at the end

        Image back = before;
        shadeBrush(back, before, {0, 0}, 1, ramp, -1);
        shadeBrush(back, before, {1, 0}, 1, ramp, -1);
        CHECK(back.pixel(0, 0) == a);
        CHECK(back.pixel(1, 0) == a);
    }
}

TEST_CASE("shade brush: round shape and clipping")
{
    Image img(5, 5, PixelFormat::Indexed8);
    img.clear(1);
    const Image before = img;
    const Rect r = shadeBrush(img, before, {2, 2}, 3, {1, 2}, 1, BrushShape::Round);
    CHECK(r == Rect{1, 1, 3, 3});
    CHECK(img.pixel(1, 1) == 1); // round 3 is a plus: corners untouched
    CHECK(img.pixel(2, 1) == 2);
    CHECK(img.pixel(2, 2) == 2);
    CHECK(shadeBrush(img, before, {0, 0}, 3, {1, 2}, 1) == Rect{0, 0, 2, 2});
    CHECK(shadeBrush(img, before, {-5, -5}, 1, {1, 2}, 1).isEmpty());
}

namespace {

// A 3 x 2 brush: values 1 2 . / 4 . 6 ('.' outside the footprint).
ImageBrush testBrush()
{
    ImageBrush b{Image(3, 2, PixelFormat::Indexed8), Image(3, 2, PixelFormat::Indexed8)};
    const uint32_t values[] = {1, 2, 0, 4, 0, 6};
    for (int i = 0; i < 6; ++i) {
        b.pixels.setPixel(i % 3, i / 3, values[i]);
        b.footprint.setPixel(i % 3, i / 3, values[i] ? 1 : 0);
    }
    return b;
}

} // namespace

TEST_CASE("image brush: own pixels or one value, only where the footprint covers")
{
    const ImageBrush brush = testBrush();
    CHECK(brush.rectAt({5, 5}) == Rect{4, 4, 3, 2});

    Image img(8, 8, PixelFormat::Indexed8);
    img.clear(9);
    CHECK(stampImageBrush(img, {5, 5}, brush) == Rect{4, 4, 3, 2});
    CHECK(img.pixel(4, 4) == 1);
    CHECK(img.pixel(5, 4) == 2);
    CHECK(img.pixel(6, 4) == 9); // outside the footprint: untouched
    CHECK(img.pixel(4, 5) == 4);
    CHECK(img.pixel(5, 5) == 9);
    CHECK(img.pixel(6, 5) == 6);

    img.clear(9);
    stampImageBrush(img, {5, 5}, brush, 7);
    CHECK(img.pixel(4, 4) == 7);
    CHECK(img.pixel(6, 4) == 9);
    CHECK(img.pixel(6, 5) == 7);

    // Clipped at the edges.
    img.clear(9);
    CHECK(stampImageBrush(img, {0, 0}, brush) == Rect{0, 0, 2, 1});
    CHECK(img.pixel(0, 0) == 9); // the brush's bottom row: . 6
    CHECK(img.pixel(1, 0) == 6);
    CHECK(stampImageBrush(img, {-9, -9}, brush).isEmpty());
}

TEST_CASE("image brush: flips and shading")
{
    const ImageBrush h = flipped(testBrush(), true, false);
    CHECK(h.pixels.pixel(0, 0) == 0);
    CHECK(h.pixels.pixel(2, 0) == 1);
    CHECK(h.footprint.pixel(0, 0) == 0);
    CHECK(h.footprint.pixel(1, 1) == 0);
    CHECK(h.pixels.pixel(0, 1) == 6);
    const ImageBrush v = flipped(testBrush(), false, true);
    CHECK(v.pixels.pixel(0, 0) == 4);
    CHECK(v.pixels.pixel(0, 1) == 1);
    const ImageBrush both = flipped(testBrush(), true, true);
    CHECK(both.pixels.pixel(0, 0) == 6);
    CHECK(both.pixels.pixel(2, 1) == 1);

    Image img(3, 2, PixelFormat::Indexed8);
    img.clear(1);
    const Image before = img;
    shadeImageBrush(img, before, {1, 1}, testBrush(), {1, 2, 3}, 1);
    shadeImageBrush(img, before, {1, 1}, testBrush(), {1, 2, 3}, 1); // once per stroke
    CHECK(img.pixel(0, 0) == 2);
    CHECK(img.pixel(2, 0) == 1); // outside the footprint
    CHECK(img.pixel(1, 1) == 1);
}

TEST_CASE("opaque bounds of an RGBA image")
{
    Image img(6, 6);
    CHECK(opaqueBounds(img).isEmpty());
    img.setPixel(1, 4, pack({0, 0, 0, 1}));
    img.setPixel(3, 2, pack({255, 0, 0, 255}));
    img.setPixel(2, 2, pack({255, 0, 0, 0})); // transparent, any color
    CHECK(opaqueBounds(img) == Rect{1, 2, 3, 3});
}

TEST_CASE("dither patterns: anchored, with the expected pixels on")
{
    auto onIn4x4 = [](const DitherPattern& p, int ox = 0, int oy = 0) {
        int n = 0;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                n += ditherOn(p, x + ox, y + oy);
        return n;
    };
    for (int density = 0; density <= 16; ++density) {
        CAPTURE(density);
        CHECK(onIn4x4({DitherPattern::Ordered, density}) == density);
        CHECK(onIn4x4({DitherPattern::Ordered, density}, -7, 13) == density); // any 4 x 4 tile, negatives too
    }
    for (int y = -4; y < 4; ++y)
        for (int x = -4; x < 4; ++x)
            CHECK(ditherOn({DitherPattern::Ordered, 8}, x, y) == ditherOn({DitherPattern::Checker}, x, y));
    CHECK(onIn4x4({DitherPattern::Checker}) == 8);
    CHECK(ditherOn({DitherPattern::HorizontalLines}, 3, 2));
    CHECK_FALSE(ditherOn({DitherPattern::HorizontalLines}, 3, -1));
    CHECK(ditherOn({DitherPattern::VerticalLines}, -2, 7));
    CHECK(onIn4x4({DitherPattern::DiagonalLines}) == 4);
    CHECK(ditherOn({DitherPattern::DiagonalLines}, 1, 3));
}

TEST_CASE("dither pass: covered pixels the pattern skips keep or take the other value")
{
    Image before(4, 1, PixelFormat::Indexed8);
    before.clear(1);
    Image covered(4, 1, PixelFormat::Indexed8);
    covered.fillRect({0, 0, 3, 1}, 1); // the stroke covered 0..2
    Image img = before;
    img.fillRect({0, 0, 3, 1}, 5); // what it painted

    // At origin (1, 0) the checker is on where x + 1 is even: x = 1, 3.
    Image keep = img;
    applyDither(keep, before, covered, keep.bounds(), {1, 0}, {DitherPattern::Checker}, std::nullopt);
    CHECK(keep.pixel(0, 0) == 1);
    CHECK(keep.pixel(1, 0) == 5);
    CHECK(keep.pixel(2, 0) == 1);
    CHECK(keep.pixel(3, 0) == 1); // not covered

    Image two = img;
    applyDither(two, before, covered, two.bounds(), {1, 0}, {DitherPattern::Checker}, 7u);
    CHECK(two.pixel(0, 0) == 7);
    CHECK(two.pixel(1, 0) == 5);
    CHECK(two.pixel(2, 0) == 7);
    CHECK(two.pixel(3, 0) == 1);
}

TEST_CASE("raster: keepAlpha leaves empty pixels empty and keeps alpha")
{
    Image before(3, 1);
    before.setPixel(1, 0, pack({255, 0, 0, 128}));
    before.setPixel(2, 0, pack({255, 0, 0, 255}));
    Image after(3, 1);
    after.clear(pack({0, 0, 255, 255}));
    after.setPixel(2, 0, 0); // erased
    keepAlpha(after, before, after.bounds(), 0);
    CHECK(after.pixel(0, 0) == 0);                          // was empty: stays
    CHECK(unpack(after.pixel(1, 0)) == Rgba{0, 0, 255, 128}); // recolored, its alpha kept
    CHECK(unpack(after.pixel(2, 0)) == Rgba{255, 0, 0, 255}); // can't be erased

    Image indexedBefore(2, 1, PixelFormat::Indexed8), indexedAfter(2, 1, PixelFormat::Indexed8);
    indexedBefore.setPixel(1, 0, 3);
    indexedAfter.clear(5);
    keepAlpha(indexedAfter, indexedBefore, indexedAfter.bounds(), 0);
    CHECK(indexedAfter.pixel(0, 0) == 0);
    CHECK(indexedAfter.pixel(1, 0) == 5);
}
