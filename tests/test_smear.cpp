#include <doctest/doctest.h>
#include <set>

#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Smear.h"
#include "formats/Aseprite.h"

using namespace core;

namespace {

const uint32_t kWhite = pack(Rgba{255, 255, 255, 255});
const uint32_t kBlue = pack(Rgba{40, 80, 200, 255});
const uint32_t kRed = pack(Rgba{220, 30, 30, 255});

Image canvas(int w = 32, int h = 32)
{
    Image image(w, h);
    image.clear(0);
    return image;
}

bool lit(const Image& image, int x, int y) { return (image.pixel(x, y) >> 24) != 0; }

int countLit(const Image& image)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            n += lit(image, x, y);
    return n;
}

SmearKey key(int frame, SmearPose pose, SmearKey::Ease ease = SmearKey::Ease::Linear)
{
    SmearKey k;
    k.frame = frame;
    k.pose = pose;
    k.ease = ease;
    return k;
}

SmearPose moved(double x, double y = 0)
{
    SmearPose p;
    p.x = x;
    p.y = y;
    return p;
}

SmearPose turned(double degrees)
{
    SmearPose p;
    p.rotation = degrees;
    return p;
}

} // namespace

TEST_CASE("smear: a pose between keys is tweened by the easing, and held before the first and after the last")
{
    SmearRecipe r;
    CHECK(smearPoseAt(r, 3) == SmearPose{}); // no key: as drawn
    r.keys = {key(2, moved(0)), key(6, moved(8), SmearKey::Ease::In), key(10, moved(20))};
    CHECK(smearPoseAt(r, 0).x == 0);
    CHECK(smearPoseAt(r, 4).x == doctest::Approx(4));  // halfway, linear
    CHECK(smearPoseAt(r, 8).x == doctest::Approx(11)); // halfway eased in: a quarter of the way
    CHECK(smearPoseAt(r, 12).x == 20);
}

TEST_CASE("smear: the art moves and turns about the pivot exactly when it lands on whole pixels")
{
    Image art = canvas();
    art.setPixel(2, 2, kWhite);
    art.setPixel(3, 2, kBlue);

    SmearRecipe r;
    r.keys = {key(0, moved(5, 1))};
    const Image shifted = renderSmear(r, art, 0);
    CHECK(shifted.pixel(7, 3) == kWhite);
    CHECK(shifted.pixel(8, 3) == kBlue);
    CHECK(countLit(shifted) == 2);

    // A half turn about the middle of the two pixels swaps them.
    r.pivotX = 3;
    r.pivotY = 2.5;
    r.keys = {key(0, turned(180))};
    const Image swapped = renderSmear(r, art, 0);
    CHECK(swapped.pixel(2, 2) == kBlue);
    CHECK(swapped.pixel(3, 2) == kWhite);
    CHECK(countLit(swapped) == 2);
}

TEST_CASE("smear: a streak joins where the art was a frame ago to where it is, as far back as the trail")
{
    Image art = canvas();
    art.setPixel(2, 5, kWhite);
    SmearRecipe r;
    r.keys = {key(0, moved(0)), key(1, moved(10))};

    const Image full = renderSmear(r, art, 1);
    for (int x = 2; x <= 12; ++x)
        CHECK(lit(full, x, 5));
    CHECK(countLit(full) == 11);

    r.trail = 0.5;
    const Image half = renderSmear(r, art, 1);
    CHECK(lit(half, 12, 5));
    CHECK(lit(half, 8, 5));
    CHECK_FALSE(lit(half, 4, 5));

    r.trail = 0;
    CHECK(countLit(renderSmear(r, art, 1)) == 1);
    r.trail = 1;
    CHECK(countLit(renderSmear(r, art, 0)) == 1); // at the first key it hasn't moved yet: no smear
}

TEST_CASE("smear: a flat smear is one color, under the art or over it")
{
    Image art = canvas();
    for (int x = 2; x < 6; ++x)
        art.setPixel(x, 5, kWhite);
    SmearRecipe r;
    r.style = SmearRecipe::Style::Flat;
    r.color = unpack(kRed);
    r.keys = {key(0, moved(0)), key(1, moved(2))};

    const Image behind = renderSmear(r, art, 1);
    CHECK(behind.pixel(2, 5) == kRed);   // only the smear was here
    CHECK(behind.pixel(5, 5) == kWhite); // the art covers the smear

    r.behind = false;
    const Image front = renderSmear(r, art, 1);
    CHECK(front.pixel(5, 5) == kRed); // the smear covers the art
    CHECK(front.pixel(7, 5) == kWhite); // the art's lead, where no in-between reached
}

TEST_CASE("smear: taper narrows it toward the tail, fade thins it, and an outline goes around it")
{
    // A blade 1 x 8 pixels hanging from its hilt (the pivot), swung a quarter turn in one frame.
    Image art = canvas(40, 40);
    for (int y = 20; y < 28; ++y)
        art.setPixel(20, y, kWhite);
    SmearRecipe r;
    r.pivotX = 20.5;
    r.pivotY = 20;
    r.keys = {key(0, turned(0)), key(1, turned(90))};
    const int plain = countLit(renderSmear(r, art, 1));
    CHECK(plain > 30); // a quarter disc of smear

    r.taper = 1;
    const int tapered = countLit(renderSmear(r, art, 1));
    CHECK(tapered < plain);

    r.taper = 0;
    r.fade = 1;
    r.fadePattern = 2;
    const int faded = countLit(renderSmear(r, art, 1));
    CHECK(faded < plain);

    r.fade = 0;
    r.style = SmearRecipe::Style::Flat;
    r.color = unpack(kRed);
    r.outline = true;
    r.outlineColor = unpack(kBlue);
    const Image outlined = renderSmear(r, art, 1);
    bool edge = false;
    for (int y = 0; y < 40 && !edge; ++y)
        for (int x = 0; x < 40 && !edge; ++x)
            edge = outlined.pixel(x, y) == kBlue;
    CHECK(edge);
    CHECK(countLit(outlined) > plain);
}

TEST_CASE("smear: a free turn makes no new colors (the art is sampled, not blended)")
{
    Image art = canvas();
    for (int y = 10; y < 18; ++y)
        for (int x = 12; x < 16; ++x)
            art.setPixel(x, y, (x + y) % 3 ? kWhite : kBlue);
    SmearRecipe r;
    r.pivotX = 14;
    r.pivotY = 14;
    r.keys = {key(0, turned(0)), key(1, turned(33))};
    const Image out = renderSmear(r, art, 1);
    std::set<uint32_t> colors;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (lit(out, x, y))
                colors.insert(out.pixel(x, y));
    CHECK(colors.size() <= 2);
    CHECK(colors.count(kWhite) == 1);
}

namespace {

// A sprite with a Smear group holding one layer with a two-pixel mark, turned a half turn at frame 2.
std::unique_ptr<Document> smearDoc()
{
    auto doc = std::make_unique<Document>(16, 16);
    for (int i = 1; i < 4; ++i)
        doc->insertFrame(i, Frame{});
    Layer* group = doc->insertLayer(doc->root(), 0, doc->makeGroupLayer("Smear 1"));
    Layer* art = doc->insertLayer(*group, 0, doc->makeImageLayer("Sword"));
    auto image = std::make_shared<Image>(16, 16);
    image->clear(0);
    image->setPixel(4, 4, kWhite);
    image->setPixel(5, 4, kBlue);
    for (int f = 0; f < 4; ++f) {
        auto cel = std::make_shared<CelData>();
        cel->image = image;
        art->cels[size_t(f)] = cel;
    }
    SmearRecipe r;
    r.pivotX = 5;
    r.pivotY = 4.5;
    r.trail = 0; // just the poses
    r.keys = {key(0, turned(0)), key(2, turned(180))};
    group->smear = r;
    return doc;
}

Image frameOf(const Document& doc, int frame)
{
    Image out(doc.width(), doc.height());
    renderFrame(doc, frame, out, doc.bounds());
    return out;
}

} // namespace

TEST_CASE("smear group: the sprite shows its art posed, or as drawn while it is being painted")
{
    auto doc = smearDoc();
    Layer* group = doc->root().children[0].get();
    REQUIRE(group->isSmear());
    CHECK(frameOf(*doc, 0).pixel(4, 4) == kWhite);
    const Image turnedFrame = frameOf(*doc, 3); // held after the last key
    CHECK(turnedFrame.pixel(4, 4) == kBlue);
    CHECK(turnedFrame.pixel(5, 4) == kWhite);

    // While its art is painted the canvas (`asEdited`) shows it as drawn; exports and previews still posed.
    group->smearAsDrawn = true;
    CHECK(frameOf(*doc, 3).pixel(4, 4) == kBlue);
    Image edited(doc->width(), doc->height());
    renderFrame(*doc, 3, edited, doc->bounds(), true);
    CHECK(edited.pixel(4, 4) == kWhite);
    group->smearAsDrawn = false;

    // The group's opacity applies to what it shows.
    group->opacity = 128;
    CHECK((frameOf(*doc, 3).pixel(4, 4) >> 24) == 128);
}

TEST_CASE("smear group: it is saved with its recipe and its art, and moves with the sprite")
{
    auto doc = smearDoc();
    SmearRecipe& r = *doc->root().children[0]->smear;
    r.style = SmearRecipe::Style::Flat;
    r.color = unpack(kRed);
    r.taper = 0.4;
    r.fade = 0.25;
    r.fadePattern = 3;
    r.outline = true;
    r.behind = false;
    r.keys[0].ease = SmearKey::Ease::Custom;
    r.keys[0].curve = {0.1, 1.3, 0.4, 0.95};
    r.keys[1].ease = SmearKey::Ease::InOut;
    r.keys[1].pose.scaleX = 1.5;
    r.keys[1].pose.skewY = 12;
    r.keys[1].pose.x = -3;
    r.smoothPath = true;
    r.facePath = true;
    std::string error;
    const auto loaded = formats::readAseprite(formats::writeAseprite(*doc), &error);
    INFO(error);
    REQUIRE(loaded);
    const Layer& group = *loaded->root().children[0];
    REQUIRE(group.isSmear());
    CHECK(*group.smear == r);
    REQUIRE(group.children.size() == 1);
    for (int f = 0; f < 4; ++f)
        CHECK(frameOf(*loaded, f) == frameOf(*doc, f));

    auto shift = canvasSizeCommand(*doc, Rect{2, 1, 20, 20}, 0);
    shift->redo(*doc);
    CHECK(doc->root().children[0]->smear->pivotX == doctest::Approx(3));
    CHECK(doc->root().children[0]->smear->keys[1].pose.x == doctest::Approx(-3));
    shift->undo(*doc);
    auto scale = scaleSpriteCommand(*doc, 32, 32, Resample::Nearest);
    scale->redo(*doc);
    CHECK(doc->root().children[0]->smear->pivotX == doctest::Approx(10));
    CHECK(doc->root().children[0]->smear->keys[1].pose.x == doctest::Approx(-6));
    scale->undo(*doc);
    CHECK(*doc->root().children[0]->smear == r);
}

TEST_CASE("smear group: baked, it is an image layer showing what the group showed")
{
    auto doc = smearDoc();
    const Layer& group = *doc->root().children[0];
    auto baked = bakeSmearGroup(*doc, group);
    REQUIRE(baked->kind == LayerKind::Image);
    CHECK(baked->name == group.name);
    REQUIRE(int(baked->cels.size()) == doc->frameCount());
    CHECK(baked->cels[2] == baked->cels[3]); // held after the last key: the same picture, one cel
    std::vector<Image> before;
    for (int f = 0; f < doc->frameCount(); ++f)
        before.push_back(frameOf(*doc, f));
    Layer* inserted = doc->insertLayer(doc->root(), 1, std::move(baked));
    doc->root().children[0]->visible = false;
    for (int f = 0; f < doc->frameCount(); ++f)
        CHECK(frameOf(*doc, f) == before[size_t(f)]);
    CHECK(inserted->visible);
}

TEST_CASE("smear presets: each starts on the frame asked, lasts its length, and is made for the art")
{
    const Rect art{10, 20, 4, 12};
    SmearRecipe base;
    base.color = unpack(kRed);
    REQUIRE(smearPresets().size() >= 4);
    for (const SmearPreset& p : smearPresets()) {
        CAPTURE(p.id);
        const SmearRecipe r = smearPresetRecipe(p.id, art, 5, base);
        REQUIRE(r.keys.size() >= 2);
        CHECK(r.keys.front().frame == 5);
        CHECK(r.keys.back().frame == 5 + p.frames - 1);
        CHECK(r.color == base.color); // the group's colors stay
        CHECK(r.pivotX == doctest::Approx(12));
        for (size_t i = 1; i < r.keys.size(); ++i)
            CHECK(r.keys[i].frame > r.keys[i - 1].frame);
    }
    const SmearRecipe swing = smearPresetRecipe("swing", art, 0, base);
    CHECK(swing.pivotY == doctest::Approx(32)); // the bottom of the art: the grip
    const SmearRecipe dash = smearPresetRecipe("dash", art, 0, base);
    CHECK(dash.keys.back().pose.x == doctest::Approx(18)); // one and a half times its longer side
    CHECK(smearPresetRecipe("nonsense", art, 0, base) == base);
}

TEST_CASE("smear: a smooth path goes through every key on a curve, and the art can face the way it goes")
{
    SmearRecipe r;
    // An arc: right and down, then right and up.
    r.keys = {key(0, moved(0, 0)), key(4, moved(20, 20)), key(8, moved(40, 0))};
    const SmearPose straight = smearPoseAt(r, 2);
    CHECK(straight.x == doctest::Approx(10));
    CHECK(straight.y == doctest::Approx(10));

    r.smoothPath = true;
    for (const SmearKey& k : r.keys) { // through every key
        CHECK(smearPoseAt(r, k.frame).x == doctest::Approx(k.pose.x));
        CHECK(smearPoseAt(r, k.frame).y == doctest::Approx(k.pose.y));
    }
    const SmearPose curved = smearPoseAt(r, 2);
    CHECK(std::abs(curved.y - 10) > 1); // bent toward the arc, not on the straight line
    CHECK(curved.rotation == 0);

    // Facing the path: going right at the bottom of the arc, right and up at the end (held after it).
    r.facePath = true;
    CHECK(smearPoseAt(r, 4).rotation == doctest::Approx(0).epsilon(0.05));
    const double end = smearPoseAt(r, 8).rotation;
    CHECK(end < -10);
    CHECK(end > -80);
    CHECK(smearPoseAt(r, 12).rotation == doctest::Approx(end));
    // On top of a key's own turn.
    r.keys[1].pose.rotation = 30;
    CHECK(smearPoseAt(r, 4).rotation == doctest::Approx(30).epsilon(0.05));

    // Standing still, nothing to face.
    SmearRecipe still;
    still.facePath = true;
    still.keys = {key(0, turned(15)), key(4, turned(15))};
    CHECK(smearPoseAt(still, 2).rotation == 15);
}

TEST_CASE("easing: the named easings are curves, and a custom curve can overshoot")
{
    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        CHECK(evalEasing(namedEasing(0), t) == doctest::Approx(t));
        CHECK(evalEasing(namedEasing(1), t) == doctest::Approx(t * t));
        CHECK(evalEasing(namedEasing(2), t) == doctest::Approx(1 - (1 - t) * (1 - t)));
        CHECK(evalEasing(namedEasing(3), t) == doctest::Approx(t * t * (3 - 2 * t)));
    }
    const EasingCurve back{0.3, 1.6, 0.6, 1.0}; // past the end, then back
    double most = 0;
    for (int i = 0; i <= 20; ++i)
        most = std::max(most, evalEasing(back, i / 20.0));
    CHECK(most > 1.05);
    CHECK(evalEasing(back, 0) == doctest::Approx(0));
    CHECK(evalEasing(back, 1) == doctest::Approx(1));
    // A handle's x past 0..1 is taken as its edge: time never runs back.
    const EasingCurve wild{-2, 0, 3, 1};
    double last = -1;
    for (int i = 0; i <= 20; ++i) {
        const double v = evalEasing(wild, i / 20.0);
        CHECK(v >= last - 1e-9);
        last = v;
    }

    // A Smear key eased by a custom curve.
    SmearRecipe r;
    r.keys = {key(0, moved(0), SmearKey::Ease::Custom), key(10, moved(100))};
    r.keys[0].curve = back;
    double far = 0;
    for (int f = 0; f <= 10; ++f)
        far = std::max(far, smearPoseAt(r, f).x);
    CHECK(far > 105); // past the second key's place, then back to it
    CHECK(smearPoseAt(r, 10).x == doctest::Approx(100));
}

TEST_CASE("smear presets of your own: made independent of the art, placed on any art")
{
    SmearRecipe r;
    r.pivotX = 12;
    r.pivotY = 32;
    r.keys = {key(3, moved(4, -2)), key(7, turned(90))};
    r.trail = 0.6;
    const Rect art{10, 20, 4, 12}; // the longer side: 12
    const SmearRecipe relative = relativeSmear(r, art);
    CHECK(relative.keys[0].frame == 0);
    CHECK(relative.keys[1].frame == 4);
    CHECK(relative.pivotX == doctest::Approx(0.5));
    CHECK(relative.pivotY == doctest::Approx(1));
    CHECK(placedSmear(relative, art, 3, r) == r); // back where it was

    // On art twice the size, later: moves twice as far, the pivot on the same place of the art.
    SmearRecipe base;
    base.color = Rgba{1, 2, 3, 255};
    const SmearRecipe placed = placedSmear(relative, Rect{0, 0, 8, 24}, 10, base);
    CHECK(placed.keys[0].frame == 10);
    CHECK(placed.keys[1].frame == 14);
    CHECK(placed.keys[0].pose.x == doctest::Approx(8));
    CHECK(placed.keys[0].pose.y == doctest::Approx(-4));
    CHECK(placed.keys[1].pose.rotation == 90);
    CHECK(placed.pivotX == doctest::Approx(4));
    CHECK(placed.pivotY == doctest::Approx(24));
    CHECK(placed.trail == doctest::Approx(0.6));
    CHECK(placed.color == base.color);
}
