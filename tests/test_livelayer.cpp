#include <doctest/doctest.h>

#include "core/ColorConvert.h"
#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "formats/Aseprite.h"
#include "formats/LiveLayerData.h"

using namespace core;

namespace {

ParticleRecipe recipe()
{
    ParticleRecipe r;
    r.count = 12;
    r.startFrame = -7;
    r.endFrame = 5;
    r.perFrame = true;
    r.rate = 3.5;
    r.untilEnd = true;
    r.lifetime = 4;
    r.x = 7.5;
    r.y = 9.5;
    r.direction = -45;
    r.spread = 90;
    r.speed = 1.25;
    r.sizeStart = 3;
    r.sizeEnd = 1;
    r.sizeEase = ParticleRecipe::Ease::Out;
    r.shape = ParticleRecipe::Shape::Circle;
    r.colors = {Rgba{255, 0, 0, 255}, Rgba{255, 200, 0, 128}, Rgba{10, 20, 30, 255}};
    r.loop = true;
    r.seed = 4242;
    r.shape = ParticleRecipe::Shape::Brush;
    r.brush.width = 2;
    r.brush.height = 3;
    r.brush.pixels = {0xff0000ffu, 0, 0x800000ffu, 0xff00ff00u, 0, 0x0a000000u};
    r.brushColors = true;
    r.angle = 30;
    r.angleRandom = 15;
    r.spin = -2.5;
    r.spinRandom = 20;
    r.faceMotion = true;
    r.angleSnap = 2;
    r.source = ParticleRecipe::Source::Ellipse;
    r.sourceWidth = 30;
    r.sourceHeight = 12;
    r.sourceAngle = -20;
    r.sourceFill = false;
    r.fields = {ParticleField{ParticleField::Type::Point, 5.5, 6.5, -0.4, 20, 1}, ParticleField{ParticleField::Type::Point, 9, 3, 0.15, 0, 0},
                ParticleField{ParticleField::Type::Vortex, 4, 4, -0.3, 12, 1, 0, 0, 24},
                ParticleField{ParticleField::Type::Wind, 2, 2, 0.12, 0, 0, 135, 40, 18},
                ParticleField{ParticleField::Type::Floor, 3, 20, 0, 0, 0, 0, 0, 24, 1, 70, 15, true}};
    ParticleField roof;
    roof.type = ParticleField::Type::Collider;
    roof.x = 12.5;
    roof.y = 16;
    roof.points = {{-8, 3}, {0.5, -2.25}, {9, 4}};
    roof.mode = 0;
    roof.bounciness = 20;
    roof.friction = 10;
    r.fields.push_back(roof);
    r.preset = "fire";
    r.subTrigger = ParticleRecipe::Trigger::Impact;
    r.subInherit = 35;
    ParticleRecipe child;
    child.count = 5;
    child.lifetime = 6;
    child.speed = 0.8;
    child.gravityY = 0.2;
    child.colors = {Rgba{9, 8, 7, 255}, Rgba{1, 2, 3, 128}};
    child.fields = {ParticleField{ParticleField::Type::Wind, 1, 2, 0.04, 0, 0, 45, 30, 12}};
    r.children = {child};
    r.opacityStart = 80;
    r.opacityEnd = 10;
    r.opacityEase = ParticleRecipe::Ease::Custom;
    r.opacityCurve = {0.2, -0.3, 0.75, 1.4};
    r.sizeEase = ParticleRecipe::Ease::Custom;
    r.sizeCurve = {0.5, 0.1, 0.9, 0.6};
    r.fadePattern = 3;
    return r;
}

Document makeDoc(ColorMode mode = ColorMode::Rgba)
{
    Document doc(24, 24, mode);
    if (mode == ColorMode::Indexed) {
        doc.palette.entries = {{Rgba{0, 0, 0, 0}, {}}, {Rgba{255, 0, 0, 255}, {}}, {Rgba{255, 200, 0, 255}, {}}};
        doc.transparentIndex = 0;
    }
    for (int i = 1; i < 6; ++i)
        doc.insertFrame(i, Frame{});
    return doc;
}

Layer* liveLayer(Document& doc)
{
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("Fire"));
    layer->particles = recipe();
    renderParticleLayer(doc, *layer);
    return layer;
}

Layer* firstImageLayer(Document& doc)
{
    Layer* found = nullptr;
    doc.forEachLayer([&](Layer& l) { if (!found && l.kind == LayerKind::Image) found = &l; });
    return found;
}

} // namespace

TEST_CASE("live layer file: the recipe and its render survive a save and load")
{
    for (const ColorMode mode : {ColorMode::Rgba, ColorMode::Indexed}) {
        Document doc = makeDoc(mode);
        liveLayer(doc);
        std::string error;
        const auto loaded = formats::readAseprite(formats::writeAseprite(doc), &error);
        INFO(error);
        REQUIRE(loaded);
        const Layer* layer = firstImageLayer(*loaded);
        REQUIRE(layer);
        REQUIRE(layer->isLive());
        CHECK(*layer->particles == recipe());
        // Reading it back doesn't change the picture.
        for (int f = 0; f < doc.frameCount(); ++f) {
            Image a(doc.width(), doc.height()), b(doc.width(), doc.height());
            renderFrame(doc, f, a, a.bounds());
            renderFrame(*loaded, f, b, b.bounds());
            CHECK(a == b);
        }
    }
}

TEST_CASE("live layer file: pixels edited elsewhere leave a normal layer that keeps its recipe")
{
    Document doc = makeDoc();
    Layer* layer = liveLayer(doc);
    auto loaded = formats::readAseprite(formats::writeAseprite(doc));
    REQUIRE(loaded);
    Layer* back = firstImageLayer(*loaded);
    REQUIRE(back);
    REQUIRE(back->isLive());

    // Another program changes a pixel: it isn't live when read again.
    back->particles.reset();
    CelRef cel = back->cels[0];
    REQUIRE(cel);
    auto image = std::make_shared<Image>(*cel->image);
    image->setPixel(0, 0, 0xff00ff00u);
    back->cels[0] = std::make_shared<CelData>(*cel);
    back->cels[0]->image = image;
    const auto edited = formats::readAseprite(formats::writeAseprite(*loaded));
    REQUIRE(edited);
    const Layer* plain = firstImageLayer(*edited);
    REQUIRE(plain);
    CHECK_FALSE(plain->isLive());
    CHECK(formats::storedRecipeIn(plain->userData).has_value()); // still there, for restoring
    REQUIRE(plain->staleRecipe.has_value()); // and waiting for a decision
    CHECK(*plain->staleRecipe == recipe());
    (void)layer;
}

TEST_CASE("live layer file: other user data is kept, and the property can be removed again")
{
    // A user data chunk with just a text ("note").
    const Bytes text{1, 0, 0, 0, 4, 0, 'n', 'o', 't', 'e'};
    const Bytes with = formats::withStoredRecipe(text, recipe(), 99);
    CHECK(with != text);
    CHECK(with[0] == 5); // text and properties
    CHECK(std::equal(text.begin() + 1, text.end(), with.begin() + 1));
    const auto stored = formats::storedRecipeIn(with);
    REQUIRE(stored);
    CHECK(stored->recipe == recipe());
    CHECK(stored->hash == 99);
    CHECK(formats::withoutStoredRecipe(with) == text);

    // From nothing, and back to nothing.
    const Bytes alone = formats::withStoredRecipe({}, recipe(), 1);
    CHECK(formats::storedRecipeIn(alone).has_value());
    CHECK(formats::withoutStoredRecipe(alone).empty());

    // Setting it twice replaces it.
    ParticleRecipe other = recipe();
    other.count = 3;
    const Bytes twice = formats::withStoredRecipe(alone, other, 2);
    CHECK(formats::storedRecipeIn(twice)->recipe.count == 3);

    // Garbage is left alone.
    const Bytes junk{4, 0, 0, 0, 255, 255, 255, 255, 1, 0};
    CHECK(formats::withStoredRecipe(junk, recipe(), 1) == junk);
    CHECK_FALSE(formats::storedRecipeIn(junk).has_value());
}

namespace {

// Whether every frame's cel is what the recipe renders for the document as it is now.
bool followsRecipe(const Document& doc, const Layer& layer)
{
    for (int f = 0; f < doc.frameCount(); ++f) {
        const Image rgba = renderParticles(*layer.particles, f, doc.width(), doc.height());
        const Image expected = convertToDocument(doc, rgba);
        const CelRef& cel = layer.cels[size_t(f)];
        bool any = false;
        for (int y = 0; y < rgba.height() && !any; ++y)
            for (int x = 0; x < rgba.width(); ++x)
                any = any || (rgba.pixel(x, y) >> 24);
        if (!any) {
            if (cel)
                return false;
            continue;
        }
        if (!cel || !cel->image || *cel->image != expected)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("live layer: its cels are rendered again after a resize, a color mode change and a palette edit")
{
    Document doc = makeDoc();
    Layer* layer = liveLayer(doc);
    REQUIRE(followsRecipe(doc, *layer));

    // Scaling would scale the particles too; the live layer is drawn again at the new size.
    auto scale = scaleSpriteCommand(doc, 48, 48);
    scale->redo(doc);
    CHECK(followsRecipe(doc, *layer));
    scale->undo(doc);
    CHECK(followsRecipe(doc, *layer));

    auto canvas = canvasSizeCommand(doc, Rect{0, 0, 40, 30}, 0);
    canvas->redo(doc);
    CHECK(followsRecipe(doc, *layer));
    canvas->undo(doc);
    CHECK(followsRecipe(doc, *layer));

    // To indexed and back: in the document's pixel format each time.
    auto indexed = colorModeCommand(doc, ColorMode::Indexed, IndexedPalette::FromSprite);
    REQUIRE(indexed);
    indexed->redo(doc);
    CHECK(layer->cels[0]->image->format() == PixelFormat::Indexed8);
    CHECK(followsRecipe(doc, *layer));
    // A palette edit moves what the indices mean.
    Palette edited = doc.palette;
    for (auto& entry : edited.entries)
        entry.color = Rgba{uint8_t(255 - entry.color.r), entry.color.g, entry.color.b, entry.color.a};
    SetPaletteCommand palette(doc.palette, doc.transparentIndex, edited, doc.transparentIndex, "Palette");
    palette.redo(doc);
    CHECK(followsRecipe(doc, *layer));
    palette.undo(doc);
    indexed->undo(doc);
    CHECK(layer->cels[0]->image->format() == PixelFormat::Rgba8);
    CHECK(followsRecipe(doc, *layer));
}
