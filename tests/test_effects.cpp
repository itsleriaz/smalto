#include <doctest/doctest.h>

#include "core/Compositor.h"
#include "core/Effects.h"

using namespace core;

namespace {

const Rgba kRed{255, 0, 0, 255};
const Rgba kBlue{0, 0, 255, 255};

// A 5 x 5 RGBA image with one red pixel in the middle.
Image dot()
{
    Image image(5, 5);
    image.setPixel(2, 2, pack(kRed));
    return image;
}

// '#' where the pixel is `c`, '.' elsewhere, row by row.
std::string pattern(const Image& image, Rgba c)
{
    std::string out;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            out += unpack(image.pixel(x, y)) == c ? '#' : '.';
        out += '\n';
    }
    return out;
}

} // namespace

TEST_CASE("effects: outline outside, round or square")
{
    Document doc(5, 5, ColorMode::Rgba);
    const Image round = outline(doc, dot(), {kBlue, false, false});
    CHECK(pattern(round, kBlue) == ".....\n"
                                   "..#..\n"
                                   ".#.#.\n"
                                   "..#..\n"
                                   ".....\n");
    CHECK(unpack(round.pixel(2, 2)) == kRed); // the shape stays
    const Image square = outline(doc, dot(), {kBlue, false, true});
    CHECK(pattern(square, kBlue) == ".....\n"
                                    ".###.\n"
                                    ".#.#.\n"
                                    ".###.\n"
                                    ".....\n");
}

TEST_CASE("effects: outline inside, limited by a mask")
{
    Document doc(5, 5, ColorMode::Rgba);
    Image block(5, 5);
    for (int y = 1; y < 4; ++y)
        for (int x = 1; x < 4; ++x)
            block.setPixel(x, y, pack(kRed));
    const Image inside = outline(doc, block, {kBlue, true, false});
    CHECK(pattern(inside, kBlue) == ".....\n"
                                    ".###.\n"
                                    ".#.#.\n"
                                    ".###.\n"
                                    ".....\n");
    CHECK(unpack(inside.pixel(2, 2)) == kRed);

    // Only the left column may change; the image sits at (10, 10).
    const Mask left(Rect{10, 10, 2, 5});
    const Image limited = outline(doc, block, {kBlue, true, false}, &left, {10, 10});
    CHECK(pattern(limited, kBlue) == ".....\n"
                                     ".#...\n"
                                     ".#...\n"
                                     ".#...\n"
                                     ".....\n");
}

TEST_CASE("effects: replace color with tolerance")
{
    Document doc(3, 1, ColorMode::Rgba);
    Image image(3, 1);
    image.setPixel(0, 0, pack(kRed));
    image.setPixel(1, 0, pack({250, 5, 0, 255}));
    image.setPixel(2, 0, pack({200, 0, 0, 255}));
    CHECK(pattern(replaceColor(doc, image, kRed, kBlue, 0), kBlue) == "#..\n");
    CHECK(pattern(replaceColor(doc, image, kRed, kBlue, 10), kBlue) == "##.\n");
    // Transparent pixels can be the "from" color too.
    CHECK(pattern(replaceColor(doc, Image(3, 1), Rgba{}, kBlue, 0), kBlue) == "###\n");
}

TEST_CASE("effects: hue / saturation / lightness")
{
    Document doc(1, 1, ColorMode::Rgba);
    Image image(1, 1);
    image.setPixel(0, 0, pack(kRed));
    CHECK(unpack(adjustHsl(doc, image, 120, 0, 0).pixel(0, 0)) == Rgba{0, 255, 0, 255});
    CHECK(unpack(adjustHsl(doc, image, -120, 0, 0).pixel(0, 0)) == kBlue);
    CHECK(unpack(adjustHsl(doc, image, 0, -100, 0).pixel(0, 0)) == Rgba{128, 128, 128, 255});
    CHECK(unpack(adjustHsl(doc, image, 0, 0, 100).pixel(0, 0)) == Rgba{255, 255, 255, 255});
    CHECK(unpack(adjustHsl(doc, image, 0, 0, -100).pixel(0, 0)) == Rgba{0, 0, 0, 255});
    // Transparent pixels and alpha are left alone.
    Image clear(1, 1);
    CHECK(adjustHsl(doc, clear, 90, 50, 50).pixel(0, 0) == 0);
    image.setPixel(0, 0, pack({255, 0, 0, 100}));
    CHECK(unpack(adjustHsl(doc, image, 120, 0, 0).pixel(0, 0)).a == 100);

    for (const Rgba c : {Rgba{12, 200, 99, 255}, Rgba{255, 255, 255, 255}, Rgba{40, 40, 40, 255}})
        CHECK(fromHsl(toHsl(c)) == c);
}

TEST_CASE("effects: indexed images take the nearest palette entry")
{
    Document doc(1, 1, ColorMode::Indexed);
    doc.palette.entries = {{Rgba{}, ""}, {kRed, ""}, {kBlue, ""}, {Rgba{0, 255, 0, 255}, ""}};
    doc.transparentIndex = 0;
    Image image(1, 1, PixelFormat::Indexed8);
    image.setPixel(0, 0, 1); // red
    CHECK(adjustHsl(doc, image, 120, 0, 0).pixel(0, 0) == 3);
    CHECK(replaceColor(doc, image, kRed, kBlue, 0).pixel(0, 0) == 2);
}

TEST_CASE("effects: blur softens colors, keeps the shape or spreads")
{
    Document doc(5, 1, ColorMode::Rgba);
    Image image(5, 1);
    image.setPixel(1, 0, pack(kRed));
    image.setPixel(2, 0, pack(kBlue));
    image.setPixel(3, 0, pack(kBlue));

    // Keep shape: empty pixels stay empty, the colors mix, and nothing
    // darkens toward the transparent edge.
    const Image kept = blur(doc, image, 1, false, true);
    CHECK(kept.pixel(0, 0) == 0);
    CHECK(kept.pixel(4, 0) == 0);
    const Rgba left = unpack(kept.pixel(1, 0));
    CHECK(left.a == 255);
    CHECK(left.r == 128);
    CHECK(left.b == 128);
    CHECK(unpack(kept.pixel(3, 0)) == kBlue); // blue and an empty pixel: still blue

    // Spread: the empty neighbours get some of it, see-through.
    const Image spread = blur(doc, image, 1, true, false);
    const Rgba edge = unpack(spread.pixel(0, 0));
    CHECK(edge.a > 0);
    CHECK(edge.a < 255);
    CHECK(edge.r == 255); // only red reached it: the color isn't darkened

    // Only inside a mask.
    const Mask first(Rect{1, 0, 1, 1});
    const Image limited = blur(doc, image, 1, false, true, &first);
    CHECK(unpack(limited.pixel(2, 0)) == kBlue);
    CHECK(limited.pixel(1, 0) != pack(kRed));
}

TEST_CASE("effects: convolution keeps alpha, divisor and bias")
{
    Document doc(3, 1, ColorMode::Rgba);
    Image image(3, 1);
    image.setPixel(0, 0, pack({100, 0, 0, 255}));
    image.setPixel(1, 0, pack({200, 0, 0, 128}));
    // Identity changes nothing (and the empty pixel stays empty).
    const std::vector<int> identity{0, 0, 0, 0, 1, 0, 0, 0, 0};
    const Image same = convolve(doc, image, identity, 3, 0, 0);
    CHECK(same.pixel(0, 0) == image.pixel(0, 0));
    CHECK(same.pixel(1, 0) == image.pixel(1, 0));
    CHECK(same.pixel(2, 0) == 0);

    // A horizontal average (auto divisor 3): the empty neighbour counts as the pixel itself.
    const std::vector<int> average{0, 0, 0, 1, 1, 1, 0, 0, 0};
    const Image soft = convolve(doc, image, average, 3, 0, 0);
    CHECK(unpack(soft.pixel(0, 0)).r == 133); // (100 + 100 + 200) / 3, the left edge clamps
    CHECK(unpack(soft.pixel(1, 0)) == Rgba{167, 0, 0, 128}); // (100 + 200 + 200) / 3, alpha kept

    // Divisor and bias by hand; results clamp.
    const Image bright = convolve(doc, image, identity, 3, 2, 50);
    CHECK(unpack(bright.pixel(0, 0)).r == 100);
    CHECK(unpack(bright.pixel(0, 0)).g == 50);
    // A wrong-sized kernel does nothing.
    CHECK(convolve(doc, image, identity, 5, 0, 0).pixel(0, 0) == image.pixel(0, 0));
}

namespace {

// '#' = red, 'b' = blue, '.' = empty; rows separated by '\n'.
Image fromPattern(const std::string& rows)
{
    int w = 0, h = 0;
    for (size_t i = 0, col = 0; i < rows.size(); ++i) {
        if (rows[i] == '\n') {
            ++h;
            col = 0;
        } else {
            w = std::max(w, int(++col));
        }
    }
    Image image(w, h);
    int x = 0, y = 0;
    for (const char ch : rows) {
        if (ch == '\n') {
            ++y;
            x = 0;
            continue;
        }
        if (ch == '#')
            image.setPixel(x, y, pack(kRed));
        else if (ch == 'b')
            image.setPixel(x, y, pack(kBlue));
        ++x;
    }
    return image;
}

std::string toPattern(const Image& image)
{
    std::string out;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const Rgba c = unpack(image.pixel(x, y));
            out += c == kRed ? '#' : c == kBlue ? 'b' : '.';
        }
        out += '\n';
    }
    return out;
}

CleanupOptions only(bool orphans, bool pinholes, bool strays, bool doubles)
{
    return {orphans, pinholes, strays, doubles};
}

} // namespace

TEST_CASE("effects: cleanup removes orphans, fills pinholes, fixes strays")
{
    Document doc(5, 5, ColorMode::Rgba);
    CHECK(toPattern(cleanup(doc, fromPattern(".....\n..#..\n.....\n.###.\n.....\n"), only(true, false, false, false)))
          == ".....\n.....\n.....\n.###.\n.....\n");
    CHECK(toPattern(cleanup(doc, fromPattern("###\n#.#\n###\n"), only(false, true, false, false))) == "###\n###\n###\n");
    CHECK(toPattern(cleanup(doc, fromPattern("###\n#b#\n###\n"), only(false, false, true, false))) == "###\n###\n###\n");
    // A pinhole with a blue side takes the most common color (red).
    CHECK(toPattern(cleanup(doc, fromPattern("#b#\n#.#\n###\n"), only(false, true, false, false))) == "#b#\n###\n###\n");
}

TEST_CASE("effects: cleanup turns a staircase into a clean diagonal")
{
    Document doc(5, 4, ColorMode::Rgba);
    const Image stairs = fromPattern("#....\n##...\n.##..\n..##.\n");
    CHECK(toPattern(cleanup(doc, stairs, only(false, false, false, true))) == "#....\n.#...\n..#..\n...#.\n");
    // A filled block's corners stay (the diagonal between is filled).
    const Image block = fromPattern("##\n##\n");
    CHECK(toPattern(cleanup(doc, block, only(false, false, false, true))) == "##\n##\n");
    // Only same-colored lines are touched.
    const Image mixed = fromPattern("#.\nbb\n");
    CHECK(toPattern(cleanup(doc, mixed, only(false, false, false, true))) == "#.\nbb\n");
}

TEST_CASE("effects: blur and jumble brushes read the image from before the stroke")
{
    Document doc(3, 1, ColorMode::Rgba);
    Image before(3, 1);
    before.setPixel(0, 0, pack(kRed));
    before.setPixel(1, 0, pack(kBlue));
    before.setPixel(2, 0, pack(kBlue));

    Image image = before.copy(before.bounds());
    CHECK(blurBrush(doc, image, before, {0, 0}, 1, BrushShape::Square) == Rect{0, 0, 1, 1});
    const Rgba once = unpack(image.pixel(0, 0));
    CHECK(once.b > 0); // red took some blue
    CHECK(once.r > 0);
    // Stamping again in the same stroke gives the same pixel (from `before`).
    blurBrush(doc, image, before, {0, 0}, 1, BrushShape::Square);
    CHECK(unpack(image.pixel(0, 0)) == once);
    CHECK(image.pixel(2, 0) == before.pixel(2, 0)); // outside the brush

    // Jumble: only values that were there, the same for the same seed.
    Image a = before.copy(before.bounds()), b = before.copy(before.bounds());
    jumbleBrush(a, before, {1, 0}, 3, BrushShape::Square, 42);
    jumbleBrush(b, before, {1, 0}, 3, BrushShape::Square, 42);
    CHECK(a == b);
    for (int x = 0; x < 3; ++x)
        CHECK((a.pixel(x, 0) == pack(kRed) || a.pixel(x, 0) == pack(kBlue)));
}

TEST_CASE("effects: blur and jumble brushes can take a footprint instead of a size")
{
    Document doc(5, 5, ColorMode::Rgba);
    Image before(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            before.setPixel(x, y, pack(x < 2 ? kRed : kBlue));

    // A plus-shaped footprint: 1 in the middle row and column of a 3 x 3.
    Image plus(3, 3, PixelFormat::Indexed8);
    for (int i = 0; i < 3; ++i) {
        plus.setPixel(i, 1, 1);
        plus.setPixel(1, i, 1);
    }
    Image image = before.copy(before.bounds());
    const Rect changed = blurBrush(doc, image, before, {2, 2}, 3, BrushShape::Square, &plus);
    CHECK(changed == Rect{1, 1, 3, 3});
    CHECK(image.pixel(1, 1) == before.pixel(1, 1)); // a corner of the 3 x 3 isn't in the plus
    CHECK(image.pixel(3, 3) == before.pixel(3, 3));
    CHECK(image.pixel(2, 2) != before.pixel(2, 2)); // the center is blurred (it has red and blue around)

    Image a = before.copy(before.bounds());
    jumbleBrush(a, before, {2, 2}, 3, BrushShape::Square, 7, &plus);
    CHECK(a.pixel(1, 1) == before.pixel(1, 1));
    CHECK(a.pixel(3, 1) == before.pixel(3, 1));
}

#include "core/Tilemap.h"

TEST_CASE("tilemap: tiles flip and come back; maps lay out as pixels")
{
    Image tile(3, 2, PixelFormat::Indexed8);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            tile.setPixel(x, y, uint32_t(1 + y * 3 + x));
    for (uint32_t flips = 0; flips < 8; ++flips) {
        const uint32_t bits = flips << 29;
        CAPTURE(flips);
        const Image shown = orientTile(tile, bits);
        CHECK(shown.width() == ((bits & kTileFlipDiagonal) ? 2 : 3));
        CHECK(unorientTile(shown, bits) == tile);
    }
    CHECK(orientTile(tile, kTileFlipX).pixel(0, 0) == 3);
    CHECK(orientTile(tile, kTileFlipY).pixel(0, 0) == 4);

    Tileset set;
    set.tileWidth = 3;
    set.tileHeight = 2;
    set.tiles = {std::make_shared<Image>(3, 2, PixelFormat::Indexed8), std::make_shared<Image>(tile)};
    set.tiles[0]->clear(0);
    Image map(2, 1, PixelFormat::Tilemap32);
    map.setPixel(0, 0, 0);
    map.setPixel(1, 0, 1 | kTileFlipX);
    const Image flat = flattenTilemap(map, set, PixelFormat::Indexed8, 0);
    CHECK(flat.width() == 6);
    CHECK(flat.pixel(0, 0) == 0); // the empty tile
    CHECK(flat.pixel(3, 0) == 3); // flipped
    CHECK(isBlank(flat.copy({0, 0, 3, 2}), 0));
}

TEST_CASE("effects: posterize snaps channels to a few levels and keeps alpha")
{
    Document doc(2, 1, ColorMode::Rgba);
    Image image(2, 1);
    image.setPixel(0, 0, pack({100, 200, 30, 128}));
    const Image two = posterize(doc, image, 2);
    CHECK(unpack(two.pixel(0, 0)) == Rgba{0, 255, 0, 128});
    CHECK(unpack(two.pixel(1, 0)).a == 0); // empty stays empty
    CHECK(unpack(posterize(doc, image, 16).pixel(0, 0)) == Rgba{102, 204, 34, 128});
    const Mask second(Rect{1, 0, 1, 1});
    CHECK(unpack(posterize(doc, image, 2, &second).pixel(0, 0)) == Rgba{100, 200, 30, 128});
}

TEST_CASE("effects: drop shadow goes behind the shape, offset")
{
    Document doc(5, 5, ColorMode::Rgba);
    const Rgba black{0, 0, 0, 255};
    const Image shadow = dropShadow(doc, dot(), black, 1, 1);
    CHECK(unpack(shadow.pixel(2, 2)) == kRed); // the shape stays on top
    CHECK(unpack(shadow.pixel(3, 3)) == black);
    CHECK(unpack(shadow.pixel(1, 1)).a == 0);
    // Half-opaque shadow color, and a shadow off the edge is lost.
    CHECK(unpack(dropShadow(doc, dot(), {0, 0, 0, 128}, 1, 0).pixel(3, 2)).a == 128);
    CHECK(pattern(dropShadow(doc, dot(), black, 9, 0), black) == ".....\n.....\n.....\n.....\n.....\n");
}

TEST_CASE("effects: curve tables follow their points")
{
    const CurveTable identity = curveTable({});
    CHECK(identity[0] == 0);
    CHECK(identity[77] == 77);
    CHECK(curveTable({{0, 0}, {255, 255}})[100] == 100);
    const CurveTable inverted = curveTable({{0, 255}, {255, 0}});
    CHECK(inverted[0] == 255);
    CHECK(inverted[255] == 0);
    // Passes through its points, flat outside them, and never overshoots.
    const CurveTable lift = curveTable({{64, 128}, {192, 200}});
    CHECK(lift[64] == 128);
    CHECK(lift[192] == 200);
    CHECK(lift[10] == 128);
    CHECK(lift[250] == 200);
    for (int x = 64; x <= 192; ++x)
        CHECK((lift[size_t(x)] >= 128 && lift[size_t(x)] <= 200));
    const CurveTable bright = curveTable({{0, 0}, {128, 192}, {255, 255}});
    CHECK(bright[128] == 192);
    CHECK(bright[64] > 64);
}

TEST_CASE("effects: applyCurves changes the colors only")
{
    Document doc(2, 1, ColorMode::Rgba);
    Image image(2, 1);
    image.setPixel(0, 0, pack({10, 100, 200, 90}));
    const CurveTable id = curveTable({});
    const CurveTable invert = curveTable({{0, 255}, {255, 0}});
    const Image out = applyCurves(doc, image, id, invert, id, id);
    CHECK(unpack(out.pixel(0, 0)) == Rgba{245, 100, 200, 90});
    CHECK(unpack(out.pixel(1, 0)).a == 0);
    const Image both = applyCurves(doc, image, invert, invert, id, id); // master, then red: back to the start
    CHECK(unpack(both.pixel(0, 0)) == Rgba{10, 155, 55, 90});
}
