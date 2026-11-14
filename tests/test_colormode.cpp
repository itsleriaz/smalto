#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "core/ColorConvert.h"
#include "core/Compositor.h"

#include <cstdlib>

using namespace core;

namespace {

CelRef rgbaCel(int w, int h, Point pos = {})
{
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(w, h);
    cel->position = pos;
    return cel;
}

Image render(const Document& doc, int frame = 0)
{
    Image out(doc.width(), doc.height());
    renderFrame(doc, frame, out, doc.bounds());
    return out;
}

} // namespace

TEST_CASE("color mode: RGBA to indexed from the sprite's colors and back is lossless")
{
    Document doc(4, 2);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    doc.insertFrame(1, Frame{});
    CelRef cel = rgbaCel(4, 2);
    const Rgba colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {10, 20, 30, 255}};
    for (int x = 0; x < 4; ++x)
        cel->image->setPixel(x, 0, pack(colors[x]));
    cel->image->setPixel(1, 1, pack(colors[0])); // the rest stays transparent
    layer->cels[0] = cel;
    layer->cels[1] = cel; // linked
    const Image before0 = render(doc, 0);
    const auto original = cel->image;
    const int paletteSize = doc.palette.size();

    auto command = colorModeCommand(doc, ColorMode::Indexed, IndexedPalette::FromSprite);
    REQUIRE(command);
    UndoStack undo;
    undo.execute(doc, std::move(command));

    CHECK(doc.colorMode() == ColorMode::Indexed);
    CHECK(doc.transparentIndex == 0);
    REQUIRE(doc.palette.size() == 5);
    CHECK(doc.palette.color(0).a == 0);
    CHECK(doc.palette.color(1) == colors[0]); // reading order
    CHECK(doc.palette.color(4) == colors[3]);
    CHECK(layer->cels[0] == layer->cels[1]);
    CHECK(cel->image->format() == PixelFormat::Indexed8);
    CHECK(cel->image->pixel(0, 0) == 1);
    CHECK(cel->image->pixel(0, 1) == 0);
    CHECK(render(doc, 0) == before0);
    CHECK(render(doc, 1) == before0);

    undo.execute(doc, colorModeCommand(doc, ColorMode::Rgba));
    CHECK(doc.colorMode() == ColorMode::Rgba);
    CHECK(cel->image->format() == PixelFormat::Rgba8);
    CHECK(*cel->image == *original);

    undo.undo(doc);
    undo.undo(doc);
    CHECK(doc.colorMode() == ColorMode::Rgba);
    CHECK(cel->image == original); // the same image object is back
    CHECK(doc.palette.size() == paletteSize);
    undo.redo(doc);
    CHECK(doc.colorMode() == ColorMode::Indexed);
    CHECK(render(doc, 1) == before0);
}

TEST_CASE("color mode: indexed from the current palette picks the nearest entry")
{
    Document doc(3, 1);
    doc.palette.entries = {{Rgba{0, 0, 0, 255}, {}}, {Rgba{255, 255, 255, 255}, {}}, {Rgba{200, 0, 0, 255}, {}}};
    doc.transparentIndex = 0;
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    CelRef cel = rgbaCel(3, 1);
    cel->image->setPixel(0, 0, pack({250, 10, 5, 255}));
    cel->image->setPixel(1, 0, pack({5, 5, 5, 128})); // near black, but black is the transparent index
    layer->cels[0] = cel;

    UndoStack undo;
    undo.execute(doc, colorModeCommand(doc, ColorMode::Indexed));
    CHECK(doc.palette.size() == 3); // kept
    CHECK(cel->image->pixel(0, 0) == 2);
    CHECK(cel->image->pixel(1, 0) == 2); // nearest that isn't transparent; alpha dropped
    CHECK(cel->image->pixel(2, 0) == 0);
}

TEST_CASE("color mode: background layers stay opaque both ways")
{
    Document doc(2, 1, ColorMode::Indexed);
    doc.palette.entries = {{Rgba{0, 0, 0, 255}, {}}, {Rgba{255, 255, 255, 255}, {}}};
    doc.transparentIndex = 0;
    Layer* bg = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("Background"));
    bg->extraFlags |= Layer::kBackgroundFlag;
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(2, 1, PixelFormat::Indexed8);
    cel->image->setPixel(0, 0, 0); // the transparent index is black on a background
    cel->image->setPixel(1, 0, 1);
    bg->cels[0] = cel;
    const Image before = render(doc);

    UndoStack undo;
    undo.execute(doc, colorModeCommand(doc, ColorMode::Rgba));
    CHECK(unpack(cel->image->pixel(0, 0)) == Rgba{0, 0, 0, 255});
    CHECK(render(doc) == before);

    undo.execute(doc, colorModeCommand(doc, ColorMode::Indexed));
    CHECK(cel->image->pixel(0, 0) == 0); // black may use the transparent index here
    CHECK(render(doc) == before);
}

TEST_CASE("color mode: grayscale uses luminance and keeps alpha")
{
    Document doc(2, 1);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    CelRef cel = rgbaCel(2, 1);
    cel->image->setPixel(0, 0, pack({255, 0, 0, 255}));
    cel->image->setPixel(1, 0, pack({100, 100, 100, 77}));
    layer->cels[0] = cel;

    UndoStack undo;
    undo.execute(doc, colorModeCommand(doc, ColorMode::Grayscale));
    CHECK(cel->image->format() == PixelFormat::Gray16);
    CHECK(cel->image->pixel(0, 0) == (77u | 255u << 8));
    CHECK(cel->image->pixel(1, 0) == (100u | 77u << 8));

    undo.execute(doc, colorModeCommand(doc, ColorMode::Rgba));
    CHECK(unpack(cel->image->pixel(0, 0)) == Rgba{77, 77, 77, 255});
    CHECK(unpack(cel->image->pixel(1, 0)) == Rgba{100, 100, 100, 77});
}

TEST_CASE("color mode: many colors are reduced by median cut")
{
    Document doc(32, 32);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    CelRef cel = rgbaCel(32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            cel->image->setPixel(x, y, pack({uint8_t(x * 8), uint8_t(y * 8), uint8_t((x + y) * 4), 255}));
    layer->cels[0] = cel;
    CHECK(spriteColors(doc, 2000).size() == 1024);
    CHECK(spriteColors(doc, 10).size() == 10);

    const Image before = render(doc);
    const Palette palette = paletteFromSprite(doc);
    CHECK(palette.size() == 256);
    CHECK(palette.color(0).a == 0);

    UndoStack undo;
    undo.execute(doc, colorModeCommand(doc, ColorMode::Indexed, IndexedPalette::FromSprite));
    CHECK(doc.palette.size() == 256);
    const Image after = render(doc);
    int worst = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const Rgba a = unpack(before.pixel(x, y)), b = unpack(after.pixel(x, y));
            CHECK(b.a == 255);
            worst = std::max({worst, std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
        }
    CHECK(worst <= 24);
}

TEST_CASE("color mode: nothing to do; tilesets convert with the cels")
{
    Document doc(2, 2);
    doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    CHECK(colorModeCommand(doc, ColorMode::Rgba) == nullptr);
    CHECK(canChangeColorMode(doc));

    // A tilemap layer whose tile 1 is red: to grayscale and back by undo.
    Tileset set;
    set.tileWidth = 2;
    set.tileHeight = 2;
    set.tiles = {std::make_shared<Image>(2, 2), std::make_shared<Image>(2, 2)};
    set.tiles[1]->clear(pack({255, 0, 0, 255}));
    doc.tilesets.push_back(set);
    auto layer = std::make_unique<Layer>(LayerKind::Tilemap, "tiles");
    layer->tilesetIndex = 0;
    layer->cels.resize(1);
    doc.insertLayer(doc.root(), 1, std::move(layer));
    CHECK(canChangeColorMode(doc));
    auto command = colorModeCommand(doc, ColorMode::Grayscale);
    REQUIRE(command);
    command->redo(doc);
    CHECK(doc.tilesets[0].tiles[1]->format() == PixelFormat::Gray16);
    CHECK((doc.tilesets[0].tiles[1]->pixel(0, 0) & 0xff) == 77); // red's luminance
    command->undo(doc);
    CHECK(doc.tilesets[0].tiles[1]->format() == PixelFormat::Rgba8);
    CHECK(unpack(doc.tilesets[0].tiles[1]->pixel(0, 0)) == Rgba{255, 0, 0, 255});
}

TEST_CASE("blue noise: a 64 x 64 tile ranking every pixel once, and wrapping")
{
    std::vector<int> seen(4096, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int r = blueNoiseRank(x, y);
            REQUIRE(r >= 0);
            REQUIRE(r < 4096);
            ++seen[size_t(r)];
            CHECK(blueNoiseRank(x + 64, y + 128) == r);
        }
    CHECK(std::count(seen.begin(), seen.end(), 1) == 4096);

    // The lowest ranks are spread out: no two of the first 64 are neighbours.
    std::vector<std::pair<int, int>> first;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (blueNoiseRank(x, y) < 64)
                first.push_back({x, y});
    for (size_t i = 0; i < first.size(); ++i)
        for (size_t j = i + 1; j < first.size(); ++j) {
            const int dx = std::min(std::abs(first[i].first - first[j].first), 64 - std::abs(first[i].first - first[j].first));
            const int dy = std::min(std::abs(first[i].second - first[j].second), 64 - std::abs(first[i].second - first[j].second));
            CHECK(dx * dx + dy * dy >= 4);
        }
}

namespace {

// Black and white, index 0 the transparent entry.
Palette blackAndWhite()
{
    Palette p;
    p.entries.push_back({Rgba{0, 0, 0, 0}, {}});
    p.entries.push_back({Rgba{0, 0, 0, 255}, {}});
    p.entries.push_back({Rgba{255, 255, 255, 255}, {}});
    return p;
}

Image flat(int w, int h, uint8_t gray)
{
    Image image(w, h);
    image.clear(pack({gray, gray, gray, 255}));
    return image;
}

int whites(const Image& indices)
{
    int n = 0;
    for (int y = 0; y < indices.height(); ++y)
        for (int x = 0; x < indices.width(); ++x)
            n += indices.pixel(x, y) == 2;
    return n;
}

} // namespace

TEST_CASE("dither: Sierra, Stucki and Riemersma spread a flat gray over black and white")
{
    Document doc(16, 16);
    const Palette palette = blackAndWhite();
    const Image gray = flat(16, 16, 128);
    // Plain: every pixel rounds the same way (128 is nearer white).
    CHECK(whites(ditherToPalette(doc, gray, false, palette, 0, IndexedDither::None)) == 256);
    for (const IndexedDither kind : {IndexedDither::Sierra, IndexedDither::Stucki, IndexedDither::Riemersma}) {
        const int white = whites(ditherToPalette(doc, gray, false, palette, 0, kind));
        CAPTURE(int(kind));
        CHECK(white > 80); // about half
        CHECK(white < 176);
    }
}

TEST_CASE("dither: Riemersma reaches every pixel of an image that isn't a power of two")
{
    Document doc(5, 3);
    const Palette palette = blackAndWhite();
    for (const auto& size : {std::pair{5, 3}, std::pair{1, 1}, std::pair{7, 2}, std::pair{3, 9}}) {
        const Image image = flat(size.first, size.second, 100);
        const Image out = ditherToPalette(doc, image, false, palette, 0, IndexedDither::Riemersma);
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x)
                CHECK(out.pixel(x, y) != 0); // never left as the transparent index
    }
}

TEST_CASE("dither: strength 0 is no dither, and serpentine changes the path of an error diffusion")
{
    Document doc(16, 8);
    const Palette palette = blackAndWhite();
    const Image gray = flat(16, 8, 128);
    for (const IndexedDither kind : {IndexedDither::Ordered, IndexedDither::Bayer8, IndexedDither::BlueNoise,
                                     IndexedDither::Floyd, IndexedDither::Sierra, IndexedDither::Stucki,
                                     IndexedDither::Riemersma}) {
        CAPTURE(int(kind));
        DitherOptions none;
        none.strength = 0;
        CHECK(whites(ditherToPalette(doc, gray, false, palette, 0, kind, false, none)) == 128); // all white, as plain
        CHECK(whites(ditherToPalette(doc, gray, false, palette, 0, kind)) < 128);                // the dither mixes
    }
    // A ramp, so the error has somewhere to go: running back along every other row makes a
    // different picture from running one way.
    Image ramp(16, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x)
            ramp.setPixel(x, y, pack({uint8_t(x * 16), uint8_t(x * 16), uint8_t(x * 16), 255}));
    DitherOptions serpentine;
    serpentine.serpentine = true;
    CHECK_FALSE(ditherToPalette(doc, ramp, false, palette, 0, IndexedDither::Floyd, false, serpentine)
                == ditherToPalette(doc, ramp, false, palette, 0, IndexedDither::Floyd));
    // The first row runs the same way either way.
    const Image a = ditherToPalette(doc, ramp, false, palette, 0, IndexedDither::Floyd, false, serpentine);
    const Image b = ditherToPalette(doc, ramp, false, palette, 0, IndexedDither::Floyd);
    for (int x = 0; x < 16; ++x)
        CHECK(a.pixel(x, 0) == b.pixel(x, 0));
}

TEST_CASE("dither: finding the nearest color in OKLab keeps exact palette colors and the transparent index")
{
    Document doc(4, 1);
    Palette palette = blackAndWhite();
    palette.entries.push_back({Rgba{200, 30, 30, 255}, {}}); // a red as index 3
    Image image(4, 1);
    image.setPixel(0, 0, pack({255, 255, 255, 255}));
    image.setPixel(1, 0, pack({0, 0, 0, 255}));
    image.setPixel(2, 0, pack({200, 30, 30, 255}));
    image.setPixel(3, 0, pack({9, 9, 9, 0}));
    DitherOptions oklab;
    oklab.oklab = true;
    const Image out = ditherToPalette(doc, image, false, palette, 0, IndexedDither::None, false, oklab);
    CHECK(out.pixel(0, 0) == 2);
    CHECK(out.pixel(1, 0) == 1);
    CHECK(out.pixel(2, 0) == 3);
    CHECK(out.pixel(3, 0) == 0); // fully transparent
    // A dark red is red, not black, however you measure it.
    Image dark(1, 1);
    dark.setPixel(0, 0, pack({120, 20, 20, 255}));
    CHECK(ditherToPalette(doc, dark, false, palette, 0, IndexedDither::None, false, oklab).pixel(0, 0) == 3);
}
