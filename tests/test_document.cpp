#include <doctest/doctest.h>

#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Document.h"

using namespace core;

namespace {

CelRef solidCel(int w, int h, Rgba color, Point pos = {})
{
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(w, h);
    cel->image->clear(pack(color));
    cel->position = pos;
    return cel;
}

} // namespace

TEST_CASE("layers and frames stay in sync")
{
    Document doc(8, 8);
    Layer* a = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    Layer* g = doc.insertLayer(doc.root(), 1, doc.makeGroupLayer("g"));
    Layer* b = doc.insertLayer(*g, 0, doc.makeImageLayer("b"));

    CHECK(a->cels.size() == 1);
    doc.insertFrame(1, Frame{50});
    CHECK(doc.frameCount() == 2);
    CHECK(a->cels.size() == 2);
    CHECK(b->cels.size() == 2);
    CHECK(doc.frame(1).durationMs == 50);

    a->cels[1] = solidCel(2, 2, {1, 2, 3, 255});
    auto [frame, cels] = doc.removeFrame(1);
    CHECK(frame.durationMs == 50);
    REQUIRE(cels.size() == 2);
    CHECK(cels[0].get() != nullptr);
    doc.insertFrame(1, frame, cels);
    CHECK(a->cel(1).get() == cels[0].get());
}

TEST_CASE("compositor stacks layers and honours visibility")
{
    Document doc(4, 4);
    Layer* bottom = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("bottom"));
    Layer* top = doc.insertLayer(doc.root(), 1, doc.makeImageLayer("top"));
    bottom->cels[0] = solidCel(4, 4, {255, 0, 0, 255});
    top->cels[0] = solidCel(2, 2, {0, 0, 255, 255}, {1, 1});

    Image out(4, 4);
    renderFrame(doc, 0, out, doc.bounds());
    CHECK(unpack(out.pixel(0, 0)) == Rgba{255, 0, 0, 255});
    CHECK(unpack(out.pixel(1, 1)) == Rgba{0, 0, 255, 255});
    CHECK(unpack(out.pixel(3, 3)) == Rgba{255, 0, 0, 255});

    top->visible = false;
    renderFrame(doc, 0, out, doc.bounds());
    CHECK(unpack(out.pixel(1, 1)) == Rgba{255, 0, 0, 255});
}

TEST_CASE("compositor reads indexed cels through the palette")
{
    Document doc(2, 1, ColorMode::Indexed);
    doc.palette.entries = {{{0, 0, 0, 0}, {}}, {{9, 8, 7, 255}, {}}};
    Layer* l = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(2, 1, PixelFormat::Indexed8);
    cel->image->setPixel(1, 0, 1);
    l->cels[0] = cel;

    Image out(2, 1);
    renderFrame(doc, 0, out, doc.bounds());
    CHECK(out.pixel(0, 0) == 0);
    CHECK(unpack(out.pixel(1, 0)) == Rgba{9, 8, 7, 255});
}

TEST_CASE("isolated group opacity")
{
    Document doc(1, 1);
    Layer* g = doc.insertLayer(doc.root(), 0, doc.makeGroupLayer("g"));
    g->opacity = 0;
    Layer* l = doc.insertLayer(*g, 0, doc.makeImageLayer("l"));
    l->cels[0] = solidCel(1, 1, {255, 255, 255, 255});

    Image out(1, 1);
    renderFrame(doc, 0, out, doc.bounds());
    CHECK(out.pixel(0, 0) == 0);
}

TEST_CASE("undo stack with layer and frame commands")
{
    Document doc(4, 4);
    UndoStack undo;
    undo.execute(doc, std::make_unique<InsertLayerCommand>(doc.root(), 0, doc.makeImageLayer("a")));
    CHECK(doc.root().children.size() == 1);
    undo.execute(doc, std::make_unique<InsertFrameCommand>(1, Frame{}));
    CHECK(doc.frameCount() == 2);
    CHECK(doc.root().children[0]->cels.size() == 2);

    undo.undo(doc);
    CHECK(doc.frameCount() == 1);
    undo.undo(doc);
    CHECK(doc.root().children.empty());
    CHECK(!undo.canUndo());

    undo.redo(doc);
    undo.redo(doc);
    CHECK(doc.root().children.size() == 1);
    CHECK(doc.frameCount() == 2);

    undo.execute(doc, std::make_unique<RemoveLayerCommand>(doc.root(), 0));
    CHECK(doc.root().children.empty());
    undo.undo(doc);
    CHECK(doc.root().children[0]->name == "a");
}

TEST_CASE("image patch command")
{
    Document doc(4, 4);
    auto img = std::make_shared<Image>(4, 4);
    Image before = img->copy({1, 1, 2, 2});
    img->fillRect({1, 1, 2, 2}, 9);
    Image after = img->copy({1, 1, 2, 2});

    UndoStack undo;
    undo.push(std::make_unique<ImagePatchCommand>(img, Point{1, 1}, before, after, "Pencil"));
    CHECK(undo.undoLabel() == "Pencil");
    undo.undo(doc);
    CHECK(img->pixel(1, 1) == 0);
    undo.redo(doc);
    CHECK(img->pixel(2, 2) == 9);
}

TEST_CASE("set cel and command groups")
{
    Document doc(4, 4);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    auto cel = solidCel(2, 2, {1, 2, 3, 255});
    UndoStack undo;
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<SetCelCommand>(*layer, 0, nullptr, cel));
    parts.push_back(std::make_unique<ImagePatchCommand>(cel->image, Point{0, 0}, Image(1, 1), cel->image->copy({0, 0, 1, 1}), "p"));
    undo.execute(doc, std::make_unique<CommandGroup>("Pencil", std::move(parts)));
    CHECK(layer->cel(0) == cel);
    CHECK(undo.undoLabel() == "Pencil");
    undo.undo(doc);
    CHECK_FALSE(layer->cel(0));
    CHECK(cel->image->pixel(0, 0) == 0); // patch undone too
    undo.redo(doc);
    CHECK(layer->cel(0) == cel);
    CHECK(cel->image->pixel(0, 0) == pack({1, 2, 3, 255}));
}

TEST_CASE("undo limit drops oldest")
{
    Document doc(1, 1);
    UndoStack undo;
    undo.setLimit(2);
    for (int i = 0; i < 3; ++i)
        undo.execute(doc, std::make_unique<InsertFrameCommand>(0, Frame{}));
    undo.undo(doc);
    undo.undo(doc);
    CHECK(!undo.canUndo());
    CHECK(doc.frameCount() == 2);
}

TEST_CASE("undo memory limit drops oldest, keeps the newest")
{
    Document doc(1, 1);
    auto img = std::make_shared<Image>(100, 100); // 40 000 bytes
    UndoStack undo;
    undo.setByteLimit(100000);
    const auto patch = [&] {
        return std::make_unique<ImagePatchCommand>(img, Point{0, 0}, Image(100, 100), Image(100, 100), "p"); // ~80 KB
    };
    for (int i = 0; i < 3; ++i)
        undo.execute(doc, patch());
    CHECK(undo.bytes() <= 100000 + 256);
    CHECK(undo.canUndo()); // the newest stays even over the limit
    undo.undo(doc);
    CHECK(!undo.canUndo());
    undo.setByteLimit(0);
    for (int i = 0; i < 3; ++i)
        undo.execute(doc, patch());
    CHECK(undo.bytes() > 200000); // unlimited
}

TEST_CASE("undo clean state")
{
    Document doc(1, 1);
    UndoStack undo;
    CHECK(undo.isClean());
    undo.execute(doc, std::make_unique<InsertFrameCommand>(0, Frame{}));
    CHECK_FALSE(undo.isClean());
    undo.setClean();
    CHECK(undo.isClean());
    undo.undo(doc);
    CHECK_FALSE(undo.isClean());
    undo.redo(doc);
    CHECK(undo.isClean());

    // A new command after undoing past the clean point makes it unreachable.
    undo.undo(doc);
    undo.execute(doc, std::make_unique<InsertFrameCommand>(0, Frame{}));
    CHECK_FALSE(undo.isClean());
    undo.undo(doc);
    CHECK_FALSE(undo.isClean());

    // Dropping the clean point through the limit does the same.
    undo.clear();
    CHECK(undo.isClean());
    undo.setLimit(1);
    undo.execute(doc, std::make_unique<InsertFrameCommand>(0, Frame{}));
    undo.execute(doc, std::make_unique<InsertFrameCommand>(0, Frame{}));
    undo.undo(doc);
    CHECK_FALSE(undo.isClean());
}

TEST_CASE("colors convert into each document format")
{
    Document rgba(1, 1);
    CHECK(fromRgba(rgba, {1, 2, 3, 4}) == pack({1, 2, 3, 4}));
    CHECK(fromRgba(rgba, {1, 2, 3, 0}) == 0);

    Document gray(1, 1, ColorMode::Grayscale);
    CHECK(fromRgba(gray, {255, 255, 255, 200}) == (255u | 200u << 8));
    CHECK(fromRgba(gray, {0, 255, 0, 0}) == 0);

    Document indexed(2, 1, ColorMode::Indexed);
    indexed.palette.entries = {{{0, 0, 0, 255}, {}}, {{250, 0, 0, 255}, {}}, {{0, 0, 250, 255}, {}}};
    indexed.transparentIndex = 0;
    CHECK(fromRgba(indexed, {255, 10, 10, 255}) == 1);
    CHECK(fromRgba(indexed, {5, 5, 5, 255}) != 0); // black-ish, but 0 is transparent
    CHECK(fromRgba(indexed, {5, 5, 5, 0}) == 0);

    Image src(2, 1);
    src.setPixel(0, 0, pack({0, 0, 255, 255}));
    const Image converted = convertToDocument(indexed, src);
    CHECK(converted.format() == PixelFormat::Indexed8);
    CHECK(converted.pixel(0, 0) == 2);
    CHECK(converted.pixel(1, 0) == 0);
}

namespace {

// Frame order as the durations of each frame, for easy checks.
std::vector<int> durations(const Document& doc)
{
    std::vector<int> out;
    for (int f = 0; f < doc.frameCount(); ++f)
        out.push_back(doc.frame(f).durationMs);
    return out;
}

Document numberedFrames(int count, Layer*& layer)
{
    Document doc(2, 2);
    layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    doc.frame(0).durationMs = 1;
    for (int f = 1; f < count; ++f)
        doc.insertFrame(f, Frame{f + 1});
    for (int f = 0; f < count; ++f)
        layer->cels[size_t(f)] = solidCel(1, 1, {uint8_t(f + 1), 0, 0, 255});
    return doc;
}

} // namespace

TEST_CASE("frames move and reverse with their cels")
{
    Layer* layer = nullptr;
    Document doc = numberedFrames(5, layer);
    const CelRef third = layer->cel(2);
    doc.moveFrames(2, 2, 0); // [3 4] to the front
    CHECK(durations(doc) == std::vector{3, 4, 1, 2, 5});
    CHECK(layer->cel(0) == third);
    doc.moveFrames(0, 2, 3); // and back to the end
    CHECK(durations(doc) == std::vector{1, 2, 5, 3, 4});
    doc.reverseFrames(1, 3);
    CHECK(durations(doc) == std::vector{1, 3, 5, 2, 4});

    UndoStack undo;
    undo.execute(doc, std::make_unique<MoveFramesCommand>(0, 1, 4));
    CHECK(durations(doc) == std::vector{3, 5, 2, 4, 1});
    undo.execute(doc, std::make_unique<ReverseFramesCommand>(0, 4));
    CHECK(durations(doc) == std::vector{1, 4, 2, 5, 3});
    undo.undo(doc);
    undo.undo(doc);
    CHECK(durations(doc) == std::vector{1, 3, 5, 2, 4});
}

TEST_CASE("tags follow inserted and removed frames, and undo restores them")
{
    Layer* layer = nullptr;
    Document doc = numberedFrames(6, layer);
    doc.tags = {{"a", 1, 2, {}, 0, {}, {}}, {"b", 4, 4, {}, 0, {}, {}}};
    UndoStack undo;
    undo.execute(doc, std::make_unique<InsertFrameCommand>(2, Frame{}));
    CHECK(doc.tags[0].from == 1);
    CHECK(doc.tags[0].to == 3); // inserted inside: grows
    CHECK(doc.tags[1].from == 5);

    undo.execute(doc, std::make_unique<RemoveFrameCommand>(5)); // b's only frame
    REQUIRE(doc.tags.size() == 1);
    undo.undo(doc);
    REQUIRE(doc.tags.size() == 2);
    CHECK(doc.tags[1].from == 5);
    undo.undo(doc);
    CHECK(doc.tags[0].to == 2);
    CHECK(doc.tags[1].from == 4);

    undo.execute(doc, std::make_unique<SetTagsCommand>(doc.tags, std::vector<Tag>{}, "Delete Tag"));
    CHECK(doc.tags.empty());
    undo.undo(doc);
    CHECK(doc.tags.size() == 2);
}

TEST_CASE("onion skin shows neighbouring frames, fading with distance")
{
    Document doc(4, 1);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    for (int f = 1; f < 4; ++f)
        doc.insertFrame(f, Frame{});
    // Frame f paints pixel (f, 0) white.
    for (int f = 0; f < 4; ++f) {
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(4, 1);
        cel->image->setPixel(f, 0, pack({255, 255, 255, 255}));
        layer->cels[size_t(f)] = cel;
    }
    Image onion(4, 1), scratch(4, 1);
    OnionSkin options;
    options.before = 2;
    options.after = 1;
    options.opacity = 200;
    options.tint = false;
    renderOnionSkin(doc, 2, options, onion, doc.bounds(), scratch);
    CHECK(unpack(onion.pixel(2, 0)).a == 0);   // the current frame is not included
    CHECK(unpack(onion.pixel(1, 0)).a == 200); // nearest before
    CHECK(unpack(onion.pixel(0, 0)).a == 100); // two before: half
    CHECK(unpack(onion.pixel(3, 0)).a == 200); // nearest after
    CHECK(unpack(onion.pixel(1, 0)).r == 255);

    options.tint = true;
    renderOnionSkin(doc, 2, options, onion, doc.bounds(), scratch);
    const Rgba before = unpack(onion.pixel(1, 0));
    const Rgba after = unpack(onion.pixel(3, 0));
    CHECK(before.r > before.b); // reddish
    CHECK(after.b > after.r);   // bluish

    // At the first frame there's nothing before; only `area` is touched.
    onion.clear(pack({1, 1, 1, 1}));
    renderOnionSkin(doc, 0, options, onion, {1, 0, 1, 1}, scratch);
    CHECK(unpack(onion.pixel(1, 0)).a == 200);
    CHECK(onion.pixel(0, 0) == pack({1, 1, 1, 1}));
}

TEST_CASE("onion skin can loop within a tag")
{
    Document doc(5, 1);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    for (int f = 1; f < 5; ++f)
        doc.insertFrame(f, Frame{});
    for (int f = 0; f < 5; ++f) {
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(5, 1);
        cel->image->setPixel(f, 0, pack({255, 255, 255, 255}));
        layer->cels[size_t(f)] = cel;
    }
    Image onion(5, 1), scratch(5, 1);
    OnionSkin options;
    options.before = 1;
    options.after = 1;
    options.opacity = 200;
    options.tint = false;

    // Without a loop, the first frame of a tag (frames 1..3) still has frame 0 before it.
    renderOnionSkin(doc, 1, options, onion, doc.bounds(), scratch);
    CHECK(unpack(onion.pixel(0, 0)).a == 200);
    CHECK(unpack(onion.pixel(3, 0)).a == 0);

    // With the tag, the frame before frame 1 is the tag's last one (3), frame 0 is left out;
    // after frame 3 comes frame 1.
    options.loopFrom = 1;
    options.loopTo = 3;
    renderOnionSkin(doc, 1, options, onion, doc.bounds(), scratch);
    CHECK(unpack(onion.pixel(0, 0)).a == 0);
    CHECK(unpack(onion.pixel(3, 0)).a == 200);
    CHECK(unpack(onion.pixel(2, 0)).a == 200); // and the one after
    renderOnionSkin(doc, 3, options, onion, doc.bounds(), scratch);
    CHECK(unpack(onion.pixel(1, 0)).a == 200);
    CHECK(unpack(onion.pixel(4, 0)).a == 0);

    // A frame outside the tag isn't affected by it.
    renderOnionSkin(doc, 4, options, onion, doc.bounds(), scratch);
    CHECK(unpack(onion.pixel(3, 0)).a == 200);
    CHECK(unpack(onion.pixel(1, 0)).a == 0);

    // A tag as short as the distance never draws the frame itself.
    options.before = 3;
    options.after = 0;
    options.loopFrom = 1;
    options.loopTo = 2;
    renderOnionSkin(doc, 1, options, onion, doc.bounds(), scratch);
    CHECK(unpack(onion.pixel(2, 0)).a > 0);
    CHECK(unpack(onion.pixel(1, 0)).a == 0);
}

TEST_CASE("layers move between groups, and clone deeply")
{
    Document doc(2, 2);
    Layer* a = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("a"));
    Layer* g = doc.insertLayer(doc.root(), 1, doc.makeGroupLayer("g"));
    Layer* b = doc.insertLayer(doc.root(), 2, doc.makeImageLayer("b"));
    doc.insertFrame(1, Frame{});
    a->cels[0] = solidCel(1, 1, {1, 0, 0, 255});
    a->cels[1] = a->cels[0]; // linked

    UndoStack undo;
    undo.execute(doc, std::make_unique<MoveLayerCommand>(doc.root(), 0, *g, 0)); // a into g
    REQUIRE(doc.root().children.size() == 2);
    CHECK(g->children.front().get() == a);
    CHECK(a->parent == g);
    undo.execute(doc, std::make_unique<MoveLayerCommand>(doc.root(), 1, doc.root(), 0)); // b to the bottom
    CHECK(doc.root().children.front().get() == b);
    undo.undo(doc);
    undo.undo(doc);
    CHECK(doc.root().children[0].get() == a);
    CHECK(doc.root().children[2].get() == b);
    CHECK(a->parent == &doc.root());

    CHECK(isWithin(*a, doc.root()));
    CHECK_FALSE(isWithin(*a, *g));
    CHECK(isWithin(*g, *g));

    auto copy = cloneLayer(*a);
    CHECK(copy->name == "a");
    REQUIRE(copy->cels.size() == 2);
    CHECK(copy->cels[0] != a->cels[0]);
    CHECK(copy->cels[0] == copy->cels[1]); // the link is kept in the copy
    CHECK(*copy->cels[0]->image == *a->cels[0]->image);
}

TEST_CASE("property commands, and merging continuous edits")
{
    Document doc(1, 1);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    UndoStack undo;
    auto opacity = [&](int value) {
        LayerProps after = LayerProps::of(*layer);
        after.opacity = value;
        return std::make_unique<SetLayerPropsCommand>(*layer, LayerProps::of(*layer), after, "Layer Opacity");
    };
    undo.execute(doc, opacity(200), true); // nothing to merge into
    undo.execute(doc, opacity(150), true);
    undo.execute(doc, opacity(100), true);
    CHECK(layer->opacity == 100);
    undo.undo(doc); // one step for the whole scrub
    CHECK(layer->opacity == 255);
    CHECK_FALSE(undo.canUndo());

    // Not merged: without the flag, a different kind, or into the saved state.
    undo.redo(doc);
    undo.setClean();
    undo.execute(doc, opacity(50), true);
    CHECK(layer->opacity == 50);
    undo.undo(doc);
    CHECK(layer->opacity == 100); // the clean state survived
    CHECK(undo.isClean());

    LayerProps hidden = LayerProps::of(*layer);
    hidden.visible = false;
    undo.execute(doc, std::make_unique<SetLayerPropsCommand>(*layer, LayerProps::of(*layer), hidden, "Hide Layer"), true);
    undo.execute(doc, opacity(10), true); // different kind: separate
    undo.undo(doc);
    CHECK(layer->opacity == 100);
    CHECK_FALSE(layer->visible);

    // Frame durations.
    doc.insertFrame(1, Frame{100});
    using D = SetFrameDurationsCommand::Durations;
    undo.execute(doc, std::make_unique<SetFrameDurationsCommand>(D{{0, 100}, {1, 100}}, D{{0, 120}, {1, 120}}), true);
    undo.execute(doc, std::make_unique<SetFrameDurationsCommand>(D{{0, 120}, {1, 120}}, D{{0, 140}, {1, 140}}), true);
    CHECK(doc.frame(1).durationMs == 140);
    undo.undo(doc);
    CHECK(doc.frame(0).durationMs == 100);
    CHECK(doc.frame(1).durationMs == 100);
}

TEST_CASE("scale sprite: cels scale with the canvas, linked cels once, undo restores")
{
    Document doc(4, 2);
    doc.insertFrame(1, Frame{});
    Layer* bg = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("bg"));
    Layer* top = doc.insertLayer(doc.root(), 1, doc.makeImageLayer("top"));
    bg->cels[0] = solidCel(4, 2, {255, 0, 0, 255});
    bg->cels[1] = bg->cels[0]; // linked
    auto small = solidCel(2, 1, {0, 0, 255, 255}, {1, 1});
    small->image->setPixel(1, 0, pack({0, 255, 0, 255}));
    top->cels[0] = small;
    const auto oldImage = small->image;

    UndoStack undo;
    auto command = scaleSpriteCommand(doc, 8, 6);
    undo.execute(doc, std::move(command));
    CHECK(doc.width() == 8);
    CHECK(doc.height() == 6);
    CHECK(bg->cels[0]->image->width() == 8); // covers the new canvas
    CHECK(bg->cels[0]->image->height() == 6);
    CHECK(bg->cels[1] == bg->cels[0]);
    CHECK(small->position == Point{2, 3});
    REQUIRE(small->image->width() == 4);
    REQUIRE(small->image->height() == 3);
    CHECK(unpack(small->image->pixel(1, 2)) == Rgba{0, 0, 255, 255});
    CHECK(unpack(small->image->pixel(2, 0)) == Rgba{0, 255, 0, 255});

    Image out(8, 6);
    renderFrame(doc, 0, out, doc.bounds());
    CHECK(unpack(out.pixel(0, 0)) == Rgba{255, 0, 0, 255});
    CHECK(unpack(out.pixel(5, 5)) == Rgba{0, 255, 0, 255});

    undo.undo(doc);
    CHECK(doc.width() == 4);
    CHECK(doc.height() == 2);
    CHECK(small->image == oldImage); // the same object, for earlier patches
    CHECK(small->position == Point{1, 1});
    undo.redo(doc);
    CHECK(doc.width() == 8);
    CHECK(small->image->width() == 4);
}

TEST_CASE("scale sprite down: cels keep at least one pixel")
{
    Document doc(10, 10);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    layer->cels[0] = solidCel(1, 1, {1, 2, 3, 255}, {3, 3});
    auto command = scaleSpriteCommand(doc, 2, 2);
    command->redo(doc);
    CHECK(layer->cels[0]->image->width() == 1);
    CHECK(layer->cels[0]->image->height() == 1);
    CHECK(layer->cels[0]->position == Point{1, 1});
}

TEST_CASE("canvas size: grow moves cels, background covers the new area")
{
    Document doc(4, 4);
    Layer* bg = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("bg"));
    bg->extraFlags |= Layer::kBackgroundFlag;
    Layer* top = doc.insertLayer(doc.root(), 1, doc.makeImageLayer("top"));
    bg->cels[0] = solidCel(4, 4, {255, 0, 0, 255});
    top->cels[0] = solidCel(2, 2, {0, 0, 255, 255}, {1, 1});
    const auto topImage = top->cels[0]->image;

    UndoStack undo;
    undo.execute(doc, canvasSizeCommand(doc, {-2, -1, 8, 6}, pack({255, 255, 255, 255})));
    CHECK(doc.width() == 8);
    CHECK(doc.height() == 6);
    CHECK(bg->cels[0]->position == Point{0, 0});
    REQUIRE(bg->cels[0]->image->width() == 8);
    CHECK(unpack(bg->cels[0]->image->pixel(0, 0)) == Rgba{255, 255, 255, 255}); // new area
    CHECK(unpack(bg->cels[0]->image->pixel(2, 1)) == Rgba{255, 0, 0, 255});     // old (0, 0)
    CHECK(top->cels[0]->position == Point{3, 2});
    CHECK(top->cels[0]->image == topImage); // inside the canvas: untouched

    undo.undo(doc);
    CHECK(doc.width() == 4);
    CHECK(bg->cels[0]->image->width() == 4);
    CHECK(top->cels[0]->position == Point{1, 1});
}

TEST_CASE("canvas size: crop cuts cels to the new canvas")
{
    Document doc(6, 6);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    Layer* other = doc.insertLayer(doc.root(), 1, doc.makeImageLayer("o"));
    layer->cels[0] = solidCel(4, 4, {0, 255, 0, 255}, {1, 1});
    layer->cels[0]->image->setPixel(3, 3, pack({9, 9, 9, 255}));
    other->cels[0] = solidCel(1, 1, {1, 1, 1, 255}, {5, 5}); // entirely cropped away

    canvasSizeCommand(doc, {2, 2, 3, 3}, 0)->redo(doc);
    CHECK(doc.width() == 3);
    const CelRef& cel = layer->cels[0];
    CHECK(cel->position == Point{0, 0});
    REQUIRE(cel->image->width() == 3);
    REQUIRE(cel->image->height() == 3);
    CHECK(unpack(cel->image->pixel(2, 2)) == Rgba{9, 9, 9, 255}); // old (4, 4)
    CHECK(other->cels[0]->image->width() == 1);
    CHECK(other->cels[0]->image->pixel(0, 0) == 0);
}

TEST_CASE("set selection: undo, redo and merging nudges")
{
    Document doc(8, 8);
    UndoStack undo;
    const Mask a(Rect{1, 1, 2, 2}), b(Rect{2, 1, 2, 2}), c(Rect{3, 1, 2, 2});
    undo.execute(doc, std::make_unique<SetSelectionCommand>(Mask{}, a, "Select"));
    undo.execute(doc, std::make_unique<SetSelectionCommand>(a, b, "Nudge"));
    undo.execute(doc, std::make_unique<SetSelectionCommand>(b, c, "Nudge"), true);
    CHECK(doc.selection == c);
    undo.undo(doc); // both nudges
    CHECK(doc.selection == a);
    undo.undo(doc);
    CHECK(doc.selection.isEmpty());
    undo.redo(doc);
    CHECK(doc.selection == a);
}

TEST_CASE("mask: union, difference, tight bounds, equality")
{
    const Mask a(Rect{0, 0, 4, 4});
    CHECK(a.isRect());
    CHECK(a.contains(3, 3));
    CHECK_FALSE(a.contains(4, 0));

    const Mask both = a.united(Mask(Rect{6, 0, 2, 2}));
    CHECK(both.bounds() == Rect{0, 0, 8, 4});
    CHECK_FALSE(both.isRect());
    CHECK_FALSE(both.contains(5, 0));
    CHECK(both.contains(7, 1));

    // A hole in the middle; the bounds stay.
    const Mask ring = a.subtracted(Mask(Rect{1, 1, 2, 2}));
    CHECK(ring.bounds() == Rect{0, 0, 4, 4});
    CHECK_FALSE(ring.contains(1, 1));
    CHECK(ring.contains(0, 1));

    // Taking a side off shrinks the bounds; the same pixels compare equal.
    const Mask cut = a.subtracted(Mask(Rect{0, 0, 4, 1}));
    CHECK(cut.bounds() == Rect{0, 1, 4, 3});
    CHECK(cut == Mask(Rect{0, 1, 4, 3}));
    CHECK(a.subtracted(a).isEmpty());
    CHECK(a.subtracted(a) == Mask{});

    CHECK(a.intersected(Rect{2, 2, 10, 10}) == Mask(Rect{2, 2, 2, 2}));
    CHECK(a.translated(-1, 5).bounds() == Rect{-1, 5, 4, 4});
    CHECK(ring.intersected(Mask(Rect{0, 0, 2, 2})) == Mask(Rect{0, 0, 2, 2}).subtracted(Mask(Rect{1, 1, 1, 1})));
    CHECK(a.intersected(Mask(Rect{6, 0, 2, 2})).isEmpty());
    CHECK(ring.inverted(Rect{0, 0, 4, 4}) == Mask(Rect{1, 1, 2, 2}));
    CHECK(Mask{}.inverted(Rect{0, 0, 3, 3}) == Mask(Rect{0, 0, 3, 3}));

    const Image image = ring.toImage();
    CHECK(image.pixel(1, 1) == 0);
    CHECK(image.pixel(0, 0) == 1);
    CHECK(Mask::fromImage(image, {0, 0}) == ring);
}

TEST_CASE("undo clean state: selection-only steps don't count as changes")
{
    Document doc(4, 4);
    UndoStack undo;
    const Mask a(Rect{0, 0, 2, 2});
    undo.execute(doc, std::make_unique<SetSelectionCommand>(Mask{}, a, "Select"));
    CHECK(undo.isClean());
    undo.undo(doc);
    CHECK(undo.isClean());
    undo.redo(doc);

    undo.execute(doc, std::make_unique<InsertFrameCommand>(0, Frame{}));
    CHECK_FALSE(undo.isClean());
    undo.setClean();
    undo.undo(doc); // past the frame: content differs again
    CHECK_FALSE(undo.isClean());
    undo.redo(doc);

    // A group with a selection and a real change is a change.
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<SetSelectionCommand>(a, Mask{}, "x"));
    parts.push_back(std::make_unique<InsertFrameCommand>(0, Frame{}));
    CommandGroup group("x", std::move(parts));
    CHECK(group.changesContent());

    // Saved, then a selection step undone and replaced: still the saved content.
    undo.execute(doc, std::make_unique<SetSelectionCommand>(Mask{}, a, "Select"));
    undo.setClean();
    undo.undo(doc);
    CHECK(undo.isClean());
    undo.execute(doc, std::make_unique<SetSelectionCommand>(Mask{}, Mask(Rect{1, 1, 1, 1}), "Select"));
    CHECK(undo.isClean());

    // Dropped through the limit past selection-only steps: still clean.
    undo.setLimit(1);
    CHECK(undo.isClean());
}

TEST_CASE("resample: smooth adds no colors, bilinear blends and averages, nearest copies")
{
    const uint32_t black = pack({0, 0, 0, 255}), white = pack({255, 255, 255, 255});
    Image two(2, 1);
    two.setPixel(0, 0, black);
    two.setPixel(1, 0, white);

    const Image nearest = scaled(two, 4, 1, Resample::Nearest);
    CHECK(nearest.pixel(0, 0) == black);
    CHECK(nearest.pixel(3, 0) == white);

    const Image smooth = scaled(two, 6, 3, Resample::Smooth);
    for (int y = 0; y < smooth.height(); ++y)
        for (int x = 0; x < smooth.width(); ++x)
            CHECK((smooth.pixel(x, y) == black || smooth.pixel(x, y) == white));

    const Image bilinear = scaled(two, 4, 1, Resample::Bilinear);
    CHECK(bilinear.pixel(0, 0) == black); // the ends stay
    CHECK(bilinear.pixel(3, 0) == white);
    const Rgba mid = unpack(bilinear.pixel(1, 0));
    CHECK(mid.r > 0);
    CHECK(mid.r < 255); // a blend of the two

    const Image averaged = scaled(two, 1, 1, Resample::Bilinear); // shrinking averages
    const Rgba avg = unpack(averaged.pixel(0, 0));
    CHECK(avg.r >= 127);
    CHECK(avg.r <= 128);
    CHECK(avg.a == 255);

    // Transparent pixels don't tint what they're blended with.
    Image edge(2, 1);
    edge.setPixel(0, 0, pack({255, 0, 0, 255}));
    edge.setPixel(1, 0, 0);
    const Rgba red = unpack(scaled(edge, 4, 1, Resample::Bilinear).pixel(1, 0));
    CHECK(red.r == 255);
    CHECK(red.g == 0);
    CHECK(red.a > 0);
    CHECK(red.a < 255);
}

#include "core/Tilemap.h"

namespace {

Tileset makeTiles(std::initializer_list<uint32_t> colors)
{
    Tileset set;
    set.tileWidth = set.tileHeight = 2;
    set.tiles.push_back(std::make_shared<Image>(2, 2)); // the empty tile
    for (uint32_t c : colors) {
        auto tile = std::make_shared<Image>(2, 2);
        tile->setPixel(0, 0, c);
        set.tiles.push_back(tile);
    }
    return set;
}

Image mapOf(std::initializer_list<uint32_t> ids)
{
    Image map(int(ids.size()), 1, PixelFormat::Tilemap32);
    int x = 0;
    for (uint32_t id : ids)
        map.setPixel(x++, 0, id);
    return map;
}

} // namespace

TEST_CASE("tile management: delete, move, remove unused, merge duplicates")
{
    const Tileset set = makeTiles({0xff0000ffu, 0xff00ff00u, 0xff0000ffu}); // 1 and 3 are alike
    const Image map = mapOf({1, 2, 3, 2 | kTileFlipX, 0});

    SUBCASE("delete sends its cells to empty and shifts the later tiles down")
    {
        const TileEdit e = deleteTile(set, 2);
        REQUIRE(e.changes);
        CHECK(e.after.tiles.size() == 3);
        const Image out = remapTilemap(map, e.map);
        CHECK(out.pixel(0, 0) == 1);
        CHECK(out.pixel(1, 0) == 0);
        CHECK(out.pixel(2, 0) == 2);
        CHECK(out.pixel(3, 0) == 0);
    }
    SUBCASE("move keeps every cell showing its tile, flips included")
    {
        const TileEdit e = moveTile(set, 3, 1);
        REQUIRE(e.changes);
        const Image out = remapTilemap(map, e.map);
        CHECK(out.pixel(0, 0) == 2);
        CHECK(out.pixel(1, 0) == 3);
        CHECK(out.pixel(2, 0) == 1);
        CHECK(out.pixel(3, 0) == (3 | kTileFlipX));
        CHECK(*e.after.tiles[1] == *set.tiles[3]);
        CHECK_FALSE(moveTile(set, 2, 2).changes);
    }
    SUBCASE("unused tiles go")
    {
        const std::vector<bool> used = {true, true, false, false};
        CHECK(countUnusedTiles(set, used) == 2);
        const TileEdit e = removeUnusedTiles(set, used);
        REQUIRE(e.changes);
        CHECK(e.after.tiles.size() == 2);
    }
    SUBCASE("duplicates merge into the first")
    {
        CHECK(countDuplicateTiles(set) == 1);
        const TileEdit e = mergeDuplicateTiles(set);
        REQUIRE(e.changes);
        CHECK(e.after.tiles.size() == 3);
        const Image out = remapTilemap(map, e.map);
        CHECK(out.pixel(0, 0) == 1);
        CHECK(out.pixel(2, 0) == 1);
        CHECK(out.pixel(1, 0) == 2);
        CHECK_FALSE(mergeDuplicateTiles(e.after).changes);
    }
}

TEST_CASE("tileset images: the grid and the slices honour margin and spacing")
{
    CHECK(sheetGrid(32, 16, 16, 16, 0, 0).columns == 2);
    CHECK(sheetGrid(32, 16, 16, 16, 0, 0).rows == 1);
    // 1 px margin, 2 px spacing: 1 + 8 + 2 + 8 + 2 + 8 + 1 = 30
    CHECK(sheetGrid(30, 10, 8, 8, 1, 2).columns == 3);
    CHECK(sheetGrid(30, 10, 8, 8, 1, 2).rows == 1);
    CHECK(sheetGrid(8, 8, 16, 16, 0, 0).count() == 0);
    CHECK(sheetGrid(16, 16, 0, 4, 0, 0).count() == 0);

    Image sheet(30, 10);
    sheet.setPixel(1, 1, 0xff0000ffu);   // first tile's corner
    sheet.setPixel(11, 1, 0xff00ff00u);  // second: 1 + 8 + 2
    const auto tiles = sliceSheet(sheet, 8, 8, 1, 2);
    REQUIRE(tiles.size() == 3);
    CHECK(tiles[0].pixel(0, 0) == 0xff0000ffu);
    CHECK(tiles[1].pixel(0, 0) == 0xff00ff00u);
    CHECK(tiles[2].pixel(0, 0) == 0);
}

#include "core/Gradient.h"

TEST_CASE("gradient: where a pixel is along each shape, and between which ramp colors")
{
    using core::GradientShape;
    const Point a{0, 0}, b{10, 0};
    const auto at = [&](GradientShape shape, int x, int y) { return gradientPosition(shape, a, b, x, y); };

    CHECK(at(GradientShape::Linear, 5, 3) == doctest::Approx(0.5)); // constant across
    CHECK(at(GradientShape::Linear, -5, 0) == doctest::Approx(0.0)); // flat beyond both ends
    CHECK(at(GradientShape::Linear, 20, 0) == doctest::Approx(1.0));
    CHECK(at(GradientShape::Radial, 0, 5) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Radial, -5, 0) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Radial, 30, 0) == doctest::Approx(1.0));
    // Mirrored on both sides of the start.
    CHECK(at(GradientShape::Reflected, -5, 0) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Reflected, 5, 0) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Reflected, 10, 0) == doctest::Approx(1.0));
    // A diamond turned with the drag: along plus across.
    CHECK(at(GradientShape::Diamond, 5, 0) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Diamond, 0, 5) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Diamond, 3, 3) == doctest::Approx(0.6));
    CHECK(at(GradientShape::Diamond, 6, -6) == doctest::Approx(1.0)); // clamped
    // Once around, starting in the drag's direction.
    CHECK(at(GradientShape::Conical, 10, 0) == doctest::Approx(0.0));
    CHECK(at(GradientShape::Conical, 0, 10) == doctest::Approx(0.25)); // a quarter turn
    CHECK(at(GradientShape::Conical, -10, 0) == doctest::Approx(0.5));
    CHECK(at(GradientShape::Conical, 0, -10) == doctest::Approx(0.75));
    // A drag of no length has no position.
    CHECK(gradientPosition(GradientShape::Linear, a, a, 4, 4) == 0.0);

    // Over three colors the halves are the two intervals.
    CHECK(core::rampStep(0.0, 3).lower == 0);
    CHECK(core::rampStep(0.25, 3).lower == 0);
    CHECK(core::rampStep(0.25, 3).frac == doctest::Approx(0.5));
    CHECK(core::rampStep(0.5, 3).lower == 1);
    CHECK(core::rampStep(0.5, 3).frac == doctest::Approx(0.0));
    CHECK(core::rampStep(1.0, 3).lower == 1); // the last color is the end of the last interval
    CHECK(core::rampStep(1.0, 3).frac == doctest::Approx(1.0));
    CHECK(core::rampStep(0.3, 2).frac == doctest::Approx(0.3));
}

TEST_CASE("gradient: repeat and mirror fold the position, stops can sit anywhere")
{
    using core::GradientRepeat;
    using core::repeatPosition;
    CHECK(repeatPosition(1.5, GradientRepeat::Off) == 1.0);
    CHECK(repeatPosition(-0.5, GradientRepeat::Off) == 0.0);
    CHECK(repeatPosition(1.25, GradientRepeat::Repeat) == doctest::Approx(0.25));
    CHECK(repeatPosition(-0.25, GradientRepeat::Repeat) == doctest::Approx(0.75));
    CHECK(repeatPosition(1.25, GradientRepeat::Mirror) == doctest::Approx(0.75));
    CHECK(repeatPosition(2.25, GradientRepeat::Mirror) == doctest::Approx(0.25));
    CHECK(repeatPosition(-0.25, GradientRepeat::Mirror) == doctest::Approx(0.25));
    // A linear gradient from x 0 to 4: pixel 6 is at 1.5, which runs back to 0.5.
    const Point a{0, 0}, b{4, 0};
    CHECK(gradientPosition(GradientShape::Linear, a, b, 6, 0) == 1.0);
    CHECK(gradientPosition(GradientShape::Linear, a, b, 6, 0, GradientRepeat::Mirror) == doctest::Approx(0.5));
    CHECK(gradientPosition(GradientShape::Linear, a, b, 6, 0, GradientRepeat::Repeat) == doctest::Approx(0.5));
    CHECK(gradientPosition(GradientShape::Linear, a, b, 5, 0, GradientRepeat::Mirror) == doctest::Approx(0.75));

    const std::vector<double> at{0.0, 0.2, 1.0};
    CHECK(core::rampStepAt(0.0, at).lower == 0);
    CHECK(core::rampStepAt(0.1, at).frac == doctest::Approx(0.5));
    CHECK(core::rampStepAt(0.2, at).lower == 1);
    CHECK(core::rampStepAt(0.6, at).frac == doctest::Approx(0.5));
    CHECK(core::rampStepAt(1.0, at).lower == 1);
    CHECK(core::rampStepAt(1.0, at).frac == doctest::Approx(1.0));
    const std::vector<double> inner{0.3, 0.7}; // before the first stop and after the last stay put
    CHECK(core::rampStepAt(0.1, inner).frac == 0.0);
    CHECK(core::rampStepAt(0.9, inner).frac == 1.0);
}

#include "formats/TilesetExport.h"

TEST_CASE("tileset export: a layer's tile numbers move to a tileset's first tile, flips stay")
{
    using namespace formats;
    MapLayer layer;
    layer.width = 3;
    layer.height = 1;
    layer.gids = {0, 2, 0x80000000u | 5};
    offsetGids(layer, 1); // the first tileset: nothing moves
    CHECK(layer.gids[1] == 2);
    offsetGids(layer, 11); // tiles of a tileset that starts at 11
    CHECK(layer.gids[0] == 0); // empty stays empty
    CHECK(layer.gids[1] == 12);
    CHECK(layer.gids[2] == (0x80000000u | 15));

    // A map with two tilesets lists both, with their first tiles.
    Layer l(LayerKind::Tilemap, "L");
    const std::string xml = tiledMapXml(8, 8, {{"a.tsx", 1}, {"b.tsx", 11}}, {layer});
    CHECK(xml.find("<tileset firstgid=\"1\" source=\"a.tsx\"/>") != std::string::npos);
    CHECK(xml.find("<tileset firstgid=\"11\" source=\"b.tsx\"/>") != std::string::npos);
    const std::string json = tiledMapJson(8, 8, {{"a.tsj", 1}, {"b.tsj", 11}}, {layer});
    CHECK(json.find("{\"firstgid\": 11, \"source\": \"b.tsj\"}") != std::string::npos);
}

TEST_CASE("tileset export: a Godot 4 TileSet with an atlas source per tileset")
{
    using namespace formats;
    Tileset ground;
    ground.name = "Ground";
    ground.tileWidth = ground.tileHeight = 16;
    for (int i = 0; i < 4; ++i) // the empty tile and three
        ground.tiles.push_back(std::make_shared<Image>(16, 16));
    Tileset props = ground;
    props.name = "Props";
    props.tiles.resize(2); // the empty tile and one

    const TilesetSheet sheetA = tilesetSheetLayout(3, 16, 16, 2, 1, 2);
    const TilesetSheet sheetB = tilesetSheetLayout(1, 16, 16, 2, 1, 2);
    const std::string tres = godotTileSet({{&ground, "ground.png", sheetA, 1, 2}, {&props, "props.png", sheetB, 1, 2}}, 16, 16,
                                          "art/tiles");
    const auto has = [&](const char* text) { return tres.find(text) != std::string::npos; };
    CHECK(tres.rfind("[gd_resource type=\"TileSet\" load_steps=5 format=3]", 0) == 0);
    CHECK(has("[ext_resource type=\"Texture2D\" path=\"res://art/tiles/ground.png\" id=\"1_ground\"]"));
    CHECK(has("path=\"res://art/tiles/props.png\" id=\"2_props\""));
    CHECK(has("[sub_resource type=\"TileSetAtlasSource\" id=\"TileSetAtlasSource_ground\"]"));
    CHECK(has("margins = Vector2i(1, 1)"));
    CHECK(has("separation = Vector2i(2, 2)"));
    CHECK(has("texture_region_size = Vector2i(16, 16)"));
    // Three tiles in two columns: (0,0) (1,0) (0,1); the other atlas has one.
    CHECK(has("0:0/0 = 0\n1:0/0 = 0\n0:1/0 = 0\n"));
    CHECK_FALSE(has("1:1/0 = 0"));
    CHECK(has("[resource]\ntile_size = Vector2i(16, 16)\n"));
    CHECK(has("sources/0 = SubResource(\"TileSetAtlasSource_ground\")"));
    CHECK(has("sources/1 = SubResource(\"TileSetAtlasSource_props_2\")"));

    // No margin or spacing: those lines are left out; no folder: res:// itself.
    const std::string plain = godotTileSet({{&ground, "g.png", tilesetSheetLayout(3, 16, 16, 3, 0, 0), 0, 0}}, 16, 16, "");
    CHECK(plain.find("margins") == std::string::npos);
    CHECK(plain.find("separation") == std::string::npos);
    CHECK(plain.find("path=\"res://g.png\"") != std::string::npos);
    CHECK(plain.find("load_steps=3") != std::string::npos);
}

TEST_CASE("tileset images: the tile size is guessed from where the picture changes")
{
    // 4 x 2 tiles of 16 x 16, each a flat color of its own.
    Image sheet(64, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) {
            const uint32_t n = uint32_t(x / 16 + 4 * (y / 16));
            sheet.setPixel(x, y, 0xff000000u | ((40 + n * 25) << 16) | ((200 - n * 20) << 8) | (n * 30));
        }
    TileSizeGuess g = detectTileSize(sheet);
    CHECK(g.found());
    CHECK(g.width == 16);
    CHECK(g.height == 16);

    // Tiles with detail inside (a diagonal stripe) still find their grid.
    for (int t = 0; t < 8; ++t)
        for (int i = 0; i < 16; ++i)
            sheet.setPixel(t % 4 * 16 + i, t / 4 * 16 + i, 0xffffffffu);
    g = detectTileSize(sheet);
    CHECK(g.width == 16);
    CHECK(g.height == 16);

    // Not square: 8 wide, 16 high.
    Image wide(32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            wide.setPixel(x, y, 0xff000000u | uint32_t((x / 8 * 60 + y / 16 * 110) % 256) * 0x010101u);
    g = detectTileSize(wide);
    CHECK(g.width == 8);
    CHECK(g.height == 16);

    // One row of tiles: the height comes from the width (the sheet is as tall as a tile).
    Image row(48, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 48; ++x)
            row.setPixel(x, y, x / 16 == 1 ? 0xffff0000u : 0xff0000ffu);
    g = detectTileSize(row);
    CHECK(g.width == 16);
    CHECK(g.height == 16);

    // Nothing stands out: a flat image, or a seamless gradient.
    CHECK_FALSE(detectTileSize(Image(32, 32)).found());
    Image smooth(32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            smooth.setPixel(x, y, 0xff000000u | uint32_t(x * 4 + y * 2) * 0x010101u);
    CHECK_FALSE(detectTileSize(smooth).found());
}

TEST_CASE("tileset images: importing a sheet lays the tiles out and can merge identical ones")
{
    const uint32_t a = 0xff0000ffu, b = 0xff00ff00u;
    const auto tile = [](uint32_t color) {
        Image t(2, 2);
        t.setPixel(0, 0, color);
        return t;
    };
    // A 3 x 2 sheet: a, b, blank / a, blank, b.
    const std::vector<Image> slices{tile(a), tile(b), tile(0), tile(a), tile(0), tile(b)};
    Tileset set;
    set.tileWidth = set.tileHeight = 2;
    set.tiles.push_back(std::make_shared<Image>(2, 2)); // the empty tile

    // As it is: every tile becomes a tile, in order, and the map follows the sheet.
    SheetImport plain = importSheetTiles(set, slices, 3, 2, 0, false, false);
    CHECK(plain.added.size() == 6);
    CHECK(plain.map.width() == 3);
    CHECK(plain.map.height() == 2);
    CHECK(plain.map.pixel(0, 0) == 1);
    CHECK(plain.map.pixel(2, 1) == 6);

    // Skip empty: blank cells are tile 0 and the rest move up, the layout stays.
    SheetImport skipped = importSheetTiles(set, slices, 3, 2, 0, true, false);
    CHECK(skipped.added.size() == 4);
    CHECK(skipped.map.pixel(2, 0) == 0);
    CHECK(skipped.map.pixel(1, 1) == 0);
    CHECK(skipped.map.pixel(2, 1) == 4);

    // Merge: two tiles (a and b), the repeats and the blanks point at what exists.
    SheetImport merged = importSheetTiles(set, slices, 3, 2, 0, false, true);
    REQUIRE(merged.added.size() == 2);
    CHECK(merged.map.pixel(0, 0) == 1);
    CHECK(merged.map.pixel(1, 0) == 2);
    CHECK(merged.map.pixel(2, 0) == 0);
    CHECK(merged.map.pixel(0, 1) == 1);
    CHECK(merged.map.pixel(2, 1) == 2);

    // Into a tileset that already has tile "a": the new ones go after it, and a reuses it.
    Tileset existing = set;
    existing.tiles.push_back(std::make_shared<Image>(tile(a)));
    SheetImport appended = importSheetTiles(existing, slices, 3, 2, 0, false, true);
    REQUIRE(appended.added.size() == 1); // only b is new
    CHECK(appended.map.pixel(0, 0) == 1);
    CHECK(appended.map.pixel(1, 0) == 2); // b: right after the tileset's own tiles
    // Without merging, they are appended as they are.
    SheetImport appendedPlain = importSheetTiles(existing, slices, 3, 2, 0, false, false);
    CHECK(appendedPlain.added.size() == 6);
    CHECK(appendedPlain.map.pixel(0, 0) == 2);
}

#include "formats/TilesetExport.h"

TEST_CASE("tileset export: sheet layout, GIDs with flags, Tiled XML")
{
    using namespace formats;
    // 5 tiles of 8x8 in 3 columns with 1 px margin and 2 px spacing.
    const TilesetSheet s = tilesetSheetLayout(5, 8, 8, 3, 1, 2);
    CHECK(s.columns == 3);
    CHECK(s.rows == 2);
    CHECK(s.width == 2 + 24 + 4);
    CHECK(s.height == 2 + 16 + 2);
    CHECK(tilesetSheetLayout(5, 8, 8, 99, 0, 0).columns == 5);
    CHECK(tilesetSheetLayout(0, 8, 8, 4, 0, 0).width == 0);

    Document doc(8, 8);
    Tileset set = makeTiles({0xff0000ffu, 0xff00ff00u});
    set.name = "Ground <1>";
    set.tileWidth = set.tileHeight = 2;
    const Image sheet = tilesetSheetImage(doc, set, 2, 0, 1);
    CHECK(sheet.width() == 5);
    CHECK(sheet.pixel(0, 0) == 0xff0000ffu); // tile 1 first
    CHECK(sheet.pixel(3, 0) == 0xff00ff00u); // tile 2 after the spacing

    Layer layer(LayerKind::Tilemap, "Walls & floor");
    CelData cel;
    cel.image = std::make_shared<Image>(mapOf({1, 0, 2 | kTileFlipX | kTileFlipDiagonal}));
    cel.position = {-4, 2}; // two tiles left, one down
    const MapLayer m = mapLayerFromCel(layer, cel, 2, 2);
    CHECK(m.x == -2);
    CHECK(m.y == 1);
    REQUIRE(m.gids.size() == 3);
    CHECK(m.gids[0] == 1);
    CHECK(m.gids[2] == (2u | 0x80000000u | 0x20000000u)); // Tiled's flags
    CHECK(mapLayerCsv(m) == "1,0,2684354562\n");

    const std::string tsx = tiledTilesetXml(set, tilesetSheetLayout(2, 2, 2, 2, 0, 1), 0, 1, "g.png");
    CHECK(tsx.find("name=\"Ground &lt;1&gt;\"") != std::string::npos);
    CHECK(tsx.find("tilecount=\"2\" columns=\"2\" spacing=\"1\"") != std::string::npos);
    CHECK(tsx.find("<image source=\"g.png\" width=\"5\" height=\"2\"/>") != std::string::npos);

    MapLayer other = m;
    other.name = "b";
    other.x = 0;
    other.y = 0;
    other.width = 1;
    other.height = 1;
    other.gids = {7};
    const std::string tmx = tiledMapXml(set, "g.tsx", {m, other});
    CHECK(tmx.find("width=\"3\" height=\"2\" tilewidth=\"2\"") != std::string::npos); // x -2..1, y 0..2
    CHECK(tmx.find("<tileset firstgid=\"1\" source=\"g.tsx\"/>") != std::string::npos);
    CHECK(tmx.find("name=\"Walls &amp; floor\"") != std::string::npos);
}
