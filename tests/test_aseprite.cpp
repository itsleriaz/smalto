#include <doctest/doctest.h>

#include "core/Compositor.h"
#include "formats/Aseprite.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace core;

// Files in tests/corpus are written by Aseprite itself (see generate.lua).
// NAME.rgba holds Aseprite's render of every frame.

namespace {

const std::filesystem::path kCorpus = SMALTO_CORPUS_DIR;

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::unique_ptr<Document> load(const char* name)
{
    std::string error;
    auto doc = formats::loadAseprite(kCorpus / (std::string(name) + ".aseprite"), &error);
    INFO(error);
    REQUIRE(doc);
    return doc;
}

std::unique_ptr<Document> reload(const Document& doc)
{
    std::string error;
    auto copy = formats::readAseprite(formats::writeAseprite(doc), &error);
    INFO(error);
    REQUIRE(copy);
    return copy;
}

Image render(const Document& doc, int frame)
{
    Image out(doc.width(), doc.height());
    renderFrame(doc, frame, out, doc.bounds());
    return out;
}

// Largest per-channel difference against Aseprite's render. Fully
// transparent pixels compare equal whatever their color.
int maxDiffToReference(const Document& doc, const char* name)
{
    const auto ref = readFile(kCorpus / (std::string(name) + ".rgba"));
    const size_t frameBytes = size_t(doc.width()) * doc.height() * 4;
    REQUIRE(ref.size() == frameBytes * size_t(doc.frameCount()));
    int worst = 0;
    for (int f = 0; f < doc.frameCount(); ++f) {
        const Image img = render(doc, f);
        const uint8_t* expected = ref.data() + frameBytes * size_t(f);
        for (size_t i = 0; i < frameBytes; i += 4) {
            if (img.data()[i + 3] == 0 && expected[i + 3] == 0)
                continue;
            for (size_t c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs(int(img.data()[i + c]) - int(expected[i + c])));
        }
    }
    return worst;
}

std::vector<const Layer*> layers(const Document& doc)
{
    std::vector<const Layer*> out;
    doc.forEachLayer([&](const Layer& l) { out.push_back(&l); });
    return out;
}

void checkSameDocument(const Document& a, const Document& b)
{
    REQUIRE(a.width() == b.width());
    REQUIRE(a.height() == b.height());
    CHECK(a.colorMode() == b.colorMode());
    CHECK(a.transparentIndex == b.transparentIndex);
    CHECK(a.pixelRatio == b.pixelRatio);
    CHECK(a.grid == b.grid);
    CHECK(a.userData == b.userData);
    REQUIRE(a.palette.size() == b.palette.size());
    for (int i = 0; i < a.palette.size(); ++i) {
        CHECK(a.palette.entries[size_t(i)].color == b.palette.entries[size_t(i)].color);
        CHECK(a.palette.entries[size_t(i)].name == b.palette.entries[size_t(i)].name);
    }

    REQUIRE(a.frameCount() == b.frameCount());
    for (int f = 0; f < a.frameCount(); ++f) {
        CHECK(a.frame(f).durationMs == b.frame(f).durationMs);
        CHECK(render(a, f) == render(b, f));
    }

    const auto la = layers(a);
    const auto lb = layers(b);
    REQUIRE(la.size() == lb.size());
    for (size_t i = 0; i < la.size(); ++i) {
        const Layer& x = *la[i];
        const Layer& y = *lb[i];
        CHECK(x.name == y.name);
        CHECK(x.kind == y.kind);
        CHECK(x.visible == y.visible);
        CHECK(x.locked == y.locked);
        CHECK(x.opacity == y.opacity);
        CHECK(x.blend == y.blend);
        CHECK(x.extraFlags == y.extraFlags);
        CHECK(x.tilesetIndex == y.tilesetIndex);
        CHECK(x.uuid == y.uuid);
        CHECK(x.userData == y.userData);
        CHECK((x.parent == &a.root()) == (y.parent == &b.root()));
        for (int f = 0; f < a.frameCount() && x.hasCels(); ++f) {
            const CelRef cx = x.cel(f);
            const CelRef cy = y.cel(f);
            if (cx && !cy) {
                // Only fully transparent cels without user data are dropped.
                CHECK(cx->userData.empty());
                const Image& img = *cx->image;
                for (int py = 0; py < img.height(); ++py)
                    for (int px = 0; px < img.width(); ++px)
                        CHECK(toRgba(a, img, px, py, x.isBackground()).a == 0);
            }
            REQUIRE((bool(cx) || !cy));
            if (!cx || !cy)
                continue;
            CHECK(cx->opacity == cy->opacity);
            CHECK(cx->zIndex == cy->zIndex);
            CHECK(cx->userData == cy->userData);
            CHECK(cx->extraData == cy->extraData);
            // Links are kept: same sharing pattern with the previous frame.
            if (f > 0)
                CHECK((x.cel(f - 1) == cx) == (y.cel(f - 1) == cy));
        }
    }

    REQUIRE(a.tags.size() == b.tags.size());
    for (size_t i = 0; i < a.tags.size(); ++i) {
        CHECK(a.tags[i].name == b.tags[i].name);
        CHECK(a.tags[i].from == b.tags[i].from);
        CHECK(a.tags[i].to == b.tags[i].to);
        CHECK(a.tags[i].direction == b.tags[i].direction);
        CHECK(a.tags[i].repeat == b.tags[i].repeat);
        CHECK(a.tags[i].color == b.tags[i].color);
        CHECK(a.tags[i].userData == b.tags[i].userData);
    }

    REQUIRE(a.opaqueChunks.size() == b.opaqueChunks.size());
    for (size_t i = 0; i < a.opaqueChunks.size(); ++i) {
        CHECK(a.opaqueChunks[i].frame == b.opaqueChunks[i].frame);
        CHECK(a.opaqueChunks[i].count == b.opaqueChunks[i].count);
        CHECK(a.opaqueChunks[i].bytes == b.opaqueChunks[i].bytes);
    }
}

uint16_t firstChunkType(const OpaqueChunks& o) { return uint16_t(o.bytes[4] | o.bytes[5] << 8); }

bool hasOpaque(const Document& doc, uint16_t type)
{
    for (const auto& o : doc.opaqueChunks)
        if (firstChunkType(o) == type)
            return true;
    return false;
}

const char* const kCorpusFiles[] = {"rgba_features", "blend_modes", "indexed", "grayscale", "tilemap"};

} // namespace

TEST_CASE("aseprite: rgba features are read")
{
    auto doc = load("rgba_features");
    CHECK(doc->width() == 24);
    CHECK(doc->height() == 16);
    CHECK(doc->colorMode() == ColorMode::Rgba);
    REQUIRE(doc->frameCount() == 4);
    CHECK(doc->frame(0).durationMs == 100);
    CHECK(doc->frame(1).durationMs == 150);
    CHECK(doc->frame(2).durationMs == 80);
    CHECK(doc->frame(3).durationMs == 200);
    CHECK(doc->pixelRatio == Point{2, 1});
    CHECK(doc->grid == Rect{2, 3, 8, 8});
    CHECK_FALSE(doc->userData.empty());

    const auto& top = doc->root().children;
    REQUIRE(top.size() == 4);
    CHECK(top[0]->name == "bg");
    CHECK(top[1]->name == "multiply");
    CHECK(top[1]->blend == BlendMode::Multiply);
    CHECK(top[1]->opacity == 200);
    const Layer& group = *top[2];
    CHECK(group.kind == LayerKind::Group);
    // Aseprite didn't flag group opacity/blend as valid (header flag 2), so
    // the group composites as pass-through.
    CHECK(group.opacity == 255);
    CHECK(group.blend == BlendMode::Normal);
    REQUIRE(group.children.size() == 2);
    CHECK(group.children[0]->blend == BlendMode::Overlay);
    CHECK_FALSE(group.children[1]->visible);
    CHECK(group.children[1]->parent == &group);
    const Layer& locked = *top[3];
    CHECK(locked.name == "locked \xc3\xbcn\xc3\xaf" "code");
    CHECK(locked.locked);
    CHECK_FALSE(locked.userData.empty());

    REQUIRE(top[1]->cel(1));
    CHECK(top[1]->cel(1)->opacity == 128);
    CHECK(top[1]->cel(2)->zIndex == -1);
    REQUIRE(locked.cel(0));
    CHECK(locked.cel(0)->position == Point{-1, -1});
    CHECK_FALSE(locked.cel(0)->userData.empty());

    // Frame 4 was created with linked cels.
    CHECK(top[0]->cel(3) == top[0]->cel(2));
    CHECK(top[1]->cel(3) == top[1]->cel(2));

    REQUIRE(doc->tags.size() == 2);
    CHECK(doc->tags[0].name == "walk");
    CHECK(doc->tags[0].from == 0);
    CHECK(doc->tags[0].to == 1);
    CHECK(doc->tags[0].direction == AniDirection::PingPong);
    CHECK(doc->tags[0].repeat == 2);
    CHECK(doc->tags[0].color == Rgba{255, 0, 0, 255});
    CHECK_FALSE(doc->tags[0].userData.empty());
    CHECK(doc->tags[1].direction == AniDirection::Reverse);

    // The slice Aseprite made: bounds, 9-slice center, pivot, user data.
    CHECK_FALSE(hasOpaque(*doc, 0x2022)); // modeled now, not kept as bytes
    REQUIRE(doc->slices.size() == 1);
    const Slice& slice = doc->slices[0];
    CHECK(slice.name == "hitbox");
    CHECK(slice.nineSlice);
    CHECK(slice.hasPivot);
    REQUIRE(slice.keys.size() == 1);
    CHECK(slice.keys[0].frame == 0);
    CHECK(slice.keys[0].bounds == Rect{2, 2, 10, 10});
    CHECK(slice.keys[0].center == Rect{1, 1, 8, 8});
    CHECK(slice.keys[0].pivot == Point{5, 5});
    CHECK_FALSE(slice.userData.empty());
    CHECK(slice.keyAt(3) == &slice.keys[0]); // in effect for every later frame
}

TEST_CASE("aseprite: slices survive a round trip, with their color and keys")
{
    auto doc = load("rgba_features");
    REQUIRE(doc->slices.size() == 1);
    Slice extra;
    extra.name = "button";
    extra.color = Rgba{10, 200, 30, 255};
    extra.keys = {{0, Rect{0, 0, 4, 4}, {}, {}}, {2, Rect{1, 1, 6, 5}, {}, {}}, {3, Rect{}, {}, {}}};
    doc->slices.push_back(extra);
    doc->slices[0].color = Rgba{255, 128, 0, 255};

    const auto bytes = formats::writeAseprite(*doc);
    auto back = formats::readAseprite(bytes);
    REQUIRE(back);
    REQUIRE(back->slices.size() == 2);
    CHECK(back->slices[0].name == "hitbox");
    CHECK(back->slices[0].color == Rgba{255, 128, 0, 255});
    CHECK(back->slices[0].keys == doc->slices[0].keys);
    // The color went into the existing user data, the text stayed.
    CHECK(back->slices[0].userData.size() == doc->slices[0].userData.size() + (doc->slices[0].userData[0] & 2 ? 0 : 4));
    CHECK(back->slices[1].color == extra.color);
    CHECK(back->slices[1].keys == extra.keys);
    CHECK(back->slices[1].keyAt(2)->bounds == Rect{1, 1, 6, 5});
    CHECK(back->slices[1].keyAt(3)->bounds.isEmpty()); // hidden from frame 3
}

TEST_CASE("aseprite: indexed, grayscale and tilemap are read")
{
    auto indexed = load("indexed");
    CHECK(indexed->colorMode() == ColorMode::Indexed);
    CHECK(indexed->transparentIndex == 3);
    CHECK(indexed->palette.size() == 8);
    CHECK(indexed->root().children[0]->isBackground());

    auto gray = load("grayscale");
    CHECK(gray->colorMode() == ColorMode::Grayscale);
    CHECK(gray->root().children[1]->opacity == 128);

    auto tiles = load("tilemap");
    const Layer& layer = *tiles->root().children[1];
    CHECK(layer.kind == LayerKind::Tilemap);
    REQUIRE(layer.cel(0));
    const Image& map = *layer.cel(0)->image;
    CHECK(map.format() == PixelFormat::Tilemap32);
    CHECK(map.pixel(1, 2) == 3);
    // The tileset Aseprite made: 4 x 4 tiles, the empty one and three more,
    // with their user data kept in order (the tiles' "tile N").
    CHECK_FALSE(hasOpaque(*tiles, 0x2023));
    REQUIRE(tiles->tilesets.size() > layer.tilesetIndex);
    const Tileset& set = tiles->tilesets[layer.tilesetIndex];
    CHECK(set.name == "set");
    CHECK(set.tileWidth == 4);
    CHECK(set.tileHeight == 4);
    CHECK(set.tiles.size() == 4);
    CHECK(set.tiles[0]->pixel(1, 1) == 0); // the empty tile
    CHECK(unpack(set.tiles[1]->pixel(0, 0)) == Rgba{80, 40, 175, 255});
    CHECK_FALSE(set.userData.empty());
}

TEST_CASE("aseprite: rendering matches Aseprite")
{
    for (const char* name : {"rgba_features", "blend_modes", "indexed", "grayscale", "tilemap"}) {
        const std::string file = name;
        CAPTURE(file);
        auto doc = load(name);
        // HSL modes (W3C formulas, double precision) can round off by 2.
        CHECK(maxDiffToReference(*doc, name) <= (file == "blend_modes" ? 2 : 1));
    }
}

TEST_CASE("aseprite: round trip keeps everything")
{
    for (const char* name : kCorpusFiles) {
        const std::string file = name;
        CAPTURE(file);
        auto original = load(name);
        auto copy = reload(*original);
        checkSameDocument(*original, *copy);

        // Writing is deterministic once the file is in Smalto's layout.
        const auto once = formats::writeAseprite(*copy);
        CHECK(formats::writeAseprite(*reload(*copy)) == once);
    }
}

TEST_CASE("clone document: an exact copy, nothing shared")
{
    for (const char* name : kCorpusFiles) {
        const std::string file = name;
        CAPTURE(file);
        auto original = load(name);
        auto copy = cloneDocument(*original);
        const auto bytes = formats::writeAseprite(*original);
        CHECK(formats::writeAseprite(*copy) == bytes);
        // Changing the copy's pixels leaves the original alone.
        for (const auto& image : celImages(*copy))
            image->clear(0);
        CHECK(formats::writeAseprite(*original) == bytes);
    }
}

TEST_CASE("aseprite: new document round trip")
{
    Document doc(10, 6);
    doc.palette = Palette::defaultPalette();
    doc.palette.entries[1].name = "navy";
    Layer* a = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    Layer* g = doc.insertLayer(doc.root(), 1, doc.makeGroupLayer("g"));
    Layer* b = doc.insertLayer(*g, 0, doc.makeImageLayer("b"));
    doc.insertFrame(1, Frame{40});
    doc.tags.push_back({"t", 0, 1, AniDirection::PingPongReverse, 3, Rgba{1, 2, 3, 255}, {}});

    auto full = std::make_shared<CelData>();
    full->image = std::make_shared<Image>(10, 6);
    full->image->setPixel(4, 3, pack({9, 8, 7, 255}));
    a->cels[0] = full;
    a->cels[1] = full; // linked
    auto empty = std::make_shared<CelData>();
    empty->image = std::make_shared<Image>(10, 6);
    b->cels[0] = empty; // fully transparent: not stored
    b->opacity = 99;
    g->blend = BlendMode::Difference;

    auto copy = reload(doc);
    CHECK(copy->palette.entries[1].name == "navy");
    REQUIRE(copy->tags.size() == 1);
    CHECK(copy->tags[0].direction == AniDirection::PingPongReverse);
    CHECK(copy->tags[0].color == Rgba{1, 2, 3, 255});
    const Layer& ca = *copy->root().children[0];
    REQUIRE(ca.cel(0));
    CHECK(ca.cel(1) == ca.cel(0));
    // Stored trimmed to its opaque pixels.
    CHECK(ca.cel(0)->image->width() == 1);
    CHECK(ca.cel(0)->position == Point{4, 3});
    const Layer& cb = *copy->root().children[1]->children[0];
    CHECK_FALSE(cb.cel(0));
    CHECK(cb.opacity == 99);
    CHECK(copy->root().children[1]->blend == BlendMode::Difference);
    for (int f = 0; f < 2; ++f)
        CHECK(render(*copy, f) == render(doc, f));
}

TEST_CASE("aseprite: Aseprite reads what we write")
{
    // The fixture written here is checked by opening it in Aseprite (see
    // generate.lua); this only guards the header and frame framing.
    auto doc = load("rgba_features");
    const auto data = formats::writeAseprite(*doc);
    REQUIRE(data.size() > 128);
    CHECK((data[0] | data[1] << 8 | data[2] << 16 | uint32_t(data[3]) << 24) == data.size());
    CHECK((data[4] | data[5] << 8) == 0xA5E0);
    CHECK((data[128 + 4] | data[128 + 5] << 8) == 0xF1FA);
}

TEST_CASE("aseprite: damaged files fail cleanly")
{
    const auto data = readFile(kCorpus / "rgba_features.aseprite");
    for (size_t n = 0; n < data.size(); n += 7) {
        std::string error;
        auto doc = formats::readAseprite(std::span(data.data(), n), &error);
        if (!doc)
            CHECK_FALSE(error.empty());
    }
    uint32_t seed = 1;
    for (int i = 0; i < 300; ++i) {
        auto copy = data;
        for (int k = 0; k < 4; ++k) {
            seed = seed * 1664525 + 1013904223;
            copy[seed % copy.size()] ^= uint8_t(seed >> 24);
        }
        formats::readAseprite(copy); // must not crash
    }
    CHECK_FALSE(formats::readAseprite(std::vector<uint8_t>{1, 2, 3}));
}


TEST_CASE("aseprite: an edited tag color wins over the stored user data")
{
    auto doc = load("rgba_features");
    REQUIRE_FALSE(doc->tags[0].userData.empty()); // has text + color
    doc->tags[0].color = Rgba{10, 200, 30, 255};
    auto copy = reload(*doc);
    CHECK(copy->tags[0].color == Rgba{10, 200, 30, 255});
    // The color inside the user data chunk follows too.
    const Bytes& ud = copy->tags[0].userData;
    const size_t textLength = size_t(ud[4] | ud[5] << 8);
    const size_t at = 4 + 2 + textLength;
    REQUIRE(ud.size() >= at + 4);
    CHECK(ud[at] == 10);
    CHECK(ud[at + 1] == 200);
    CHECK(ud[at + 2] == 30);
}

TEST_CASE("the symmetry crossing point is kept in a chunk of Smalto's own")
{
    Document doc(8, 8);
    CHECK_FALSE(reload(doc)->symmetryCenter2);
    doc.symmetryCenter2 = Point{7, 9};
    const auto back = reload(doc);
    REQUIRE(back->symmetryCenter2);
    CHECK(*back->symmetryCenter2 == Point{7, 9});
    CHECK(formats::writeAseprite(*reload(*back)) == formats::writeAseprite(*back));
}
