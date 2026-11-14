#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Particles.h"

using namespace core;

namespace {

int countPixels(const Image& image)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            n += (image.pixel(x, y) >> 24) != 0;
    return n;
}

bool lit(const Image& image, int x, int y) { return (image.pixel(x, y) >> 24) != 0; }

// Particles that stay where they are born, for three frames.
ParticleRecipe still()
{
    ParticleRecipe r;
    r.count = 5;
    r.emission = ParticleRecipe::Emission::Burst;
    r.startFrame = 0;
    r.endFrame = 0;
    r.lifetime = 3;
    r.lifetimeRandom = 0;
    r.x = 8.5;
    r.y = 8.5;
    r.spread = 0;
    r.speed = 0;
    r.speedRandom = 0;
    return r;
}

} // namespace

TEST_CASE("particles: a burst lives for its lifetime at the emitter")
{
    const ParticleRecipe r = still();
    for (int f = 0; f < 3; ++f) {
        const Image image = renderParticles(r, f, 16, 16);
        CHECK(countPixels(image) == 1); // all five on the same pixel
        CHECK(lit(image, 8, 8));
    }
    CHECK(countPixels(renderParticles(r, 3, 16, 16)) == 0);
    CHECK(countPixels(renderParticles(r, -1, 16, 16)) == 0);
}

TEST_CASE("particles: direction, speed, gravity and drag move them by age")
{
    ParticleRecipe r = still();
    r.speed = 2;
    r.speedRandom = 0; // straight up
    r.lifetime = 5;
    CHECK(lit(renderParticles(r, 3, 16, 16), 8, 2)); // 8.5 - 2 * 3 = 2.5

    // Gravity of 1 down: the speed up goes -2 + 1 = -1, then 0, then +1 per frame.
    r.gravityY = 1;
    const Image pulled = renderParticles(r, 3, 16, 16);
    CHECK(countPixels(pulled) == 1);
    CHECK(lit(pulled, 8, 8)); // 8.5 - 1 + 0 + 1

    // Drag slows it: 2 * (.5 + .25 + .125 + .0625) = 1.875 of 8.5 - 2 * 4 for none.
    ParticleRecipe fast = still();
    fast.speed = 2;
    fast.speedRandom = 0;
    fast.lifetime = 5;
    ParticleRecipe slow = fast;
    slow.drag = 0.5;
    CHECK(lit(renderParticles(fast, 4, 16, 16), 8, 0));
    CHECK(lit(renderParticles(slow, 4, 16, 16), 8, 6));
}

TEST_CASE("particles: a stream spreads the count over the range")
{
    ParticleRecipe r = still();
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 4;
    r.startFrame = 2;
    r.endFrame = 5;
    r.lifetime = 1;
    for (int f = 0; f < 8; ++f)
        CHECK(countPixels(renderParticles(r, f, 16, 16)) == (f >= 2 && f <= 5 ? 1 : 0));
}

TEST_CASE("particles: same recipe, same frame; the seed changes the spray")
{
    ParticleRecipe r;
    r.x = 8.5;
    r.y = 8.5;
    r.spread = 180;
    r.count = 30;
    r.lifetime = 6;
    const Image a = renderParticles(r, 4, 16, 16);
    CHECK(a == renderParticles(r, 4, 16, 16));
    r.seed = 2;
    CHECK_FALSE(a == renderParticles(r, 4, 16, 16));
}

TEST_CASE("particles: a loop repeats every period and has particles at its first frame")
{
    ParticleRecipe r;
    r.x = 8.5;
    r.y = 12.5;
    r.count = 16;
    r.startFrame = 0;
    r.endFrame = 7;
    r.lifetime = 6;
    r.lifetimeRandom = 0;
    r.spread = 0;
    r.speed = 1;
    r.speedRandom = 0;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 1); // only those born on frame 0, together
    r.loop = true;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) > 1);
    for (int f = 0; f < 8; ++f)
        CHECK(renderParticles(r, f, 16, 16) == renderParticles(r, f + 8, 16, 16));
    CHECK(renderParticles(r, 3, 16, 16) == renderParticles(r, -5, 16, 16));
}

TEST_CASE("particles: size over life, shapes and colors")
{
    ParticleRecipe r = still();
    r.sizeStart = 4;
    r.sizeEnd = 1; // 4 -> 1 at the end of life
    r.lifetime = 4;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 16);
    CHECK(countPixels(renderParticles(r, 3, 16, 16)) == 1);
    r.sizeEnd = 0; // gone at the end
    CHECK(countPixels(renderParticles(r, 3, 16, 16)) == 0);

    // Spawn at 10 and die at 3, eased: In holds the size longer, Out drops it sooner.
    r.sizeStart = 10;
    r.sizeEnd = 3;
    r.lifetime = 5; // ages 0..4, t = 0, .25, .5, .75, 1
    const auto sideAt = [&](int f) { return int(std::lround(std::sqrt(double(countPixels(renderParticles(r, f, 32, 32)))))); };
    r.sizeEase = ParticleRecipe::Ease::Linear;
    CHECK(sideAt(0) == 10);
    CHECK(sideAt(4) == 3);
    const int linearMid = sideAt(2);
    r.sizeEase = ParticleRecipe::Ease::In;
    CHECK(sideAt(2) > linearMid);
    r.sizeEase = ParticleRecipe::Ease::Out;
    CHECK(sideAt(2) < linearMid);

    // Random: every particle's whole size varies, none outside the range.
    r = still();
    r.count = 30;
    r.emission = ParticleRecipe::Emission::Burst;
    r.sizeStart = r.sizeEnd = 10;
    r.sizeRandom = 0;
    const int plain = countPixels(renderParticles(r, 0, 64, 64));
    r.sizeRandom = 50;
    const Image varied = renderParticles(r, 0, 64, 64);
    CHECK(countPixels(varied) != plain);

    r = still();
    r.sizeStart = r.sizeEnd = 3;
    r.shape = ParticleRecipe::Shape::Plus;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 5);
    r.shape = ParticleRecipe::Shape::Diamond;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 5);
    r.sizeStart = r.sizeEnd = 5;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 13);
    r.shape = ParticleRecipe::Shape::Circle;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) < 25);

    r = still();
    r.colors = {Rgba{255, 0, 0, 255}, Rgba{0, 0, 255, 255}};
    CHECK(unpack(renderParticles(r, 0, 16, 16).pixel(8, 8)) == Rgba{255, 0, 0, 255});
    CHECK(unpack(renderParticles(r, 2, 16, 16).pixel(8, 8)) == Rgba{0, 0, 255, 255});
    r.colorMode = ParticleRecipe::ColorMode::Random;
    const Rgba picked = unpack(renderParticles(r, 0, 16, 16).pixel(8, 8));
    CHECK((picked == Rgba{255, 0, 0, 255} || picked == Rgba{0, 0, 255, 255}));
}

TEST_CASE("particles: opacity over life, smooth or dithered")
{
    ParticleRecipe r = still();
    r.sizeStart = r.sizeEnd = 8;
    r.lifetime = 5;
    r.opacityStart = 100;
    r.opacityEnd = 0;
    const auto alphaAt = [&](int f) { return renderParticles(r, f, 32, 32).pixel(8, 8) >> 24; };
    CHECK(alphaAt(0) == 255);
    CHECK(alphaAt(2) == doctest::Approx(128).epsilon(0.02));
    CHECK(alphaAt(4) == 0);

    // Dithered: only whole pixels, fewer of them as it fades.
    r.fadePattern = 2;
    for (int f = 0; f < 5; ++f) {
        const Image image = renderParticles(r, f, 32, 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const uint32_t a = image.pixel(x, y) >> 24;
                CHECK((a == 0 || a == 255));
            }
    }
    CHECK(countPixels(renderParticles(r, 0, 32, 32)) == 64);
    const int half = countPixels(renderParticles(r, 2, 32, 32));
    CHECK(half > 16);
    CHECK(half < 48);
    CHECK(countPixels(renderParticles(r, 4, 32, 32)) == 0);
}

TEST_CASE("particles: a live layer's cels follow its recipe, in the document's format")
{
    for (const ColorMode mode : {ColorMode::Rgba, ColorMode::Indexed}) {
        Document doc(16, 16, mode);
        if (mode == ColorMode::Indexed) {
            doc.palette.entries = {{Rgba{0, 0, 0, 0}, {}}, {Rgba{255, 255, 255, 255}, {}}};
            doc.transparentIndex = 0;
        }
        for (int i = 1; i < 6; ++i)
            doc.insertFrame(i, Frame{});
        Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("Particles"));
        layer->particles = still();
        layer->particles->lifetime = 2;
        renderParticleLayer(doc, *layer);
        REQUIRE(layer->cels.size() == 6);
        CHECK(layer->isLive());
        CHECK(layer->cels[0]);
        CHECK(layer->cels[1] == layer->cels[0]); // the same picture twice: one cel
        CHECK_FALSE(layer->cels[2]);
        CHECK(layer->cels[0]->image->format() == doc.pixelFormat());

        // Rendered with the document, they show.
        Image frame(16, 16);
        renderFrame(doc, 0, frame, doc.bounds());
        CHECK((frame.pixel(8, 8) >> 24) == 255);
        renderFrame(doc, 4, frame, doc.bounds());
        CHECK((frame.pixel(8, 8) >> 24) == 0);

        // A copy of the layer keeps the recipe.
        const auto copy = cloneLayer(*layer);
        REQUIRE(copy->particles);
        CHECK(*copy->particles == *layer->particles);
    }
}

TEST_CASE("particles: a brush shape stamps the brush scaled to the particle's size")
{
    ParticleRecipe r = still();
    r.shape = ParticleRecipe::Shape::Brush;
    // A 3x3 brush: an L (left column and bottom row).
    r.brush.width = 3;
    r.brush.height = 3;
    r.brush.pixels = {0xff0000ffu, 0, 0,
                      0xff0000ffu, 0, 0,
                      0xff0000ffu, 0xff0000ffu, 0xff0000ffu};
    r.sizeStart = r.sizeEnd = 3;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 5);
    // Twice the size: each brush pixel is 2 x 2.
    r.sizeStart = r.sizeEnd = 6;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 20);
    // Half transparent pixels count, a faint one doesn't.
    r.sizeStart = r.sizeEnd = 3;
    r.brush.pixels[0] = 0x7f0000ffu;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 4);
    // Without a brush it is a square.
    r.brush = {};
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 9);
}

namespace {

// The box around the lit pixels (width, height); (0, 0) when there are none.
std::pair<int, int> litSize(const Image& image)
{
    int x0 = image.width(), y0 = image.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) >> 24) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    return x1 < 0 ? std::pair{0, 0} : std::pair{x1 - x0 + 1, y1 - y0 + 1};
}

ParticleRecipe bar()
{
    ParticleRecipe r = still();
    r.count = 1;
    r.shape = ParticleRecipe::Shape::Brush;
    r.brush.width = 5;
    r.brush.height = 1;
    r.brush.pixels = std::vector<uint32_t>(5, 0xff0000ffu); // a horizontal bar, red
    r.sizeStart = r.sizeEnd = 5;
    return r;
}

} // namespace

TEST_CASE("particles: angle, spin and facing the motion turn the shape")
{
    ParticleRecipe r = bar();
    CHECK(litSize(renderParticles(r, 0, 32, 32)) == std::pair{5, 1});
    r.angle = 90;
    CHECK(litSize(renderParticles(r, 0, 32, 32)) == std::pair{1, 5});
    r.angle = 180;
    CHECK(litSize(renderParticles(r, 0, 32, 32)) == std::pair{5, 1});
    r.angle = 45;
    const auto diagonal = litSize(renderParticles(r, 0, 32, 32));
    CHECK(diagonal.first > 1);
    CHECK(diagonal.second > 1);

    // Spin: a quarter turn every frame; a whole turn is back where it started.
    r = bar();
    r.lifetime = 8;
    r.spin = 90;
    CHECK(litSize(renderParticles(r, 0, 32, 32)) == std::pair{5, 1});
    CHECK(litSize(renderParticles(r, 1, 32, 32)) == std::pair{1, 5});
    CHECK(renderParticles(r, 4, 32, 32) == renderParticles(r, 0, 32, 32));

    // Facing the motion: moving up, the bar's right end leads, so it stands upright; moving right it lies.
    r = bar();
    r.lifetime = 4;
    r.speed = 1;
    r.faceMotion = true;
    r.direction = -90;
    CHECK(litSize(renderParticles(r, 1, 32, 32)) == std::pair{1, 5});
    r.direction = 0;
    CHECK(litSize(renderParticles(r, 1, 32, 32)) == std::pair{5, 1});

    // Snapped to 90 degrees, a slightly crooked angle is a straight one.
    r = bar();
    r.angle = 80;
    r.angleSnap = 3;
    CHECK(litSize(renderParticles(r, 0, 32, 32)) == std::pair{1, 5});

    // Random angles differ between particles, and the same seed gives the same picture.
    r = bar();
    r.count = 6;
    r.angleRandom = 90;
    CHECK(renderParticles(r, 0, 32, 32) == renderParticles(r, 0, 32, 32));
    ParticleRecipe fixed = r;
    fixed.angleRandom = 0;
    CHECK(renderParticles(r, 0, 32, 32) != renderParticles(fixed, 0, 32, 32));

    // A circle looks the same turned.
    r = still();
    r.shape = ParticleRecipe::Shape::Circle;
    r.sizeStart = r.sizeEnd = 5;
    const Image plain = renderParticles(r, 0, 16, 16);
    r.angle = 33;
    CHECK(renderParticles(r, 0, 16, 16) == plain);
}

TEST_CASE("particles: a brush can keep its own colors")
{
    ParticleRecipe r = still();
    r.count = 1;
    r.shape = ParticleRecipe::Shape::Brush;
    r.brush.width = 2;
    r.brush.height = 1;
    r.brush.pixels = {pack(Rgba{255, 0, 0, 255}), pack(Rgba{0, 0, 255, 255})};
    r.sizeStart = r.sizeEnd = 2;
    r.colors = {Rgba{0, 255, 0, 255}};

    // Tinted: both pixels take the particle's color.
    Image tinted = renderParticles(r, 0, 16, 16);
    int green = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            green += unpack(tinted.pixel(x, y)) == Rgba{0, 255, 0, 255};
    CHECK(green == 2);

    // Its own: red and blue, no green; and the fade scales their alpha.
    r.brushColors = true;
    const Image own = renderParticles(r, 0, 16, 16);
    int red = 0, blue = 0, other = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const Rgba c = unpack(own.pixel(x, y));
            if (c == Rgba{255, 0, 0, 255})
                ++red;
            else if (c == Rgba{0, 0, 255, 255})
                ++blue;
            else if (c.a)
                ++other;
        }
    CHECK(red == 1);
    CHECK(blue == 1);
    CHECK(other == 0);
    r.opacityStart = r.opacityEnd = 50;
    int half = 0;
    const Image faded = renderParticles(r, 0, 16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            half += unpack(faded.pixel(x, y)).a == 128;
    CHECK(half == 2);

    // Turned, the colors turn with it.
    r.opacityStart = r.opacityEnd = 100;
    r.angle = 180;
    const Image flipped = renderParticles(r, 0, 16, 16);
    int leftRed = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 15; ++x)
            leftRed += unpack(flipped.pixel(x, y)) == Rgba{0, 0, 255, 255} && unpack(flipped.pixel(x + 1, y)) == Rgba{255, 0, 0, 255};
    CHECK(leftRed == 1); // blue then red: the other way round than unturned
}

TEST_CASE("particles: a brush squeezed small samples its middle, not its corner")
{
    ParticleRecipe r = still();
    r.count = 1;
    r.shape = ParticleRecipe::Shape::Brush;
    // A diamond in a 5 x 5 brush: the corners are empty, the middle is not.
    r.brush.width = 5;
    r.brush.height = 5;
    r.brush.pixels.assign(25, 0);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            if (std::abs(x - 2) + std::abs(y - 2) <= 2)
                r.brush.pixels[size_t(y * 5 + x)] = 0xff0000ffu;
    r.sizeStart = r.sizeEnd = 1;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 1);
    r.sizeStart = r.sizeEnd = 5;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 13);
    r.sizeStart = r.sizeEnd = 10; // twice as big: each brush pixel is 2 x 2
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 52);
    r.angle = 45; // turned, small
    r.sizeStart = r.sizeEnd = 1;
    CHECK(countPixels(renderParticles(r, 0, 16, 16)) == 1);
}

namespace {

// The box around the lit pixels: x0, y0, x1, y1 (inclusive).
std::array<int, 4> litBox(const Image& image)
{
    std::array<int, 4> box{image.width(), image.height(), -1, -1};
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) >> 24) {
                box[0] = std::min(box[0], x);
                box[1] = std::min(box[1], y);
                box[2] = std::max(box[2], x);
                box[3] = std::max(box[3], y);
            }
    return box;
}

ParticleRecipe spread()
{
    ParticleRecipe r = still();
    r.count = 400; // enough to cover the source
    r.x = 32.5;
    r.y = 32.5;
    return r;
}

} // namespace

TEST_CASE("particles: a line, rectangle or ellipse source spreads where they are born")
{
    // A point: everything at the emitter.
    ParticleRecipe r = spread();
    CHECK(litBox(renderParticles(r, 0, 64, 64)) == std::array<int, 4>{32, 32, 32, 32});

    // A line 20 long: wide, one pixel high.
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 20;
    auto box = litBox(renderParticles(r, 0, 64, 64));
    CHECK(box[0] >= 22);
    CHECK(box[2] <= 42);
    CHECK(box[2] - box[0] >= 17); // it reaches most of the way along
    CHECK(box[1] == box[3]);
    // Turned a quarter: tall, one pixel wide.
    r.sourceAngle = 90;
    box = litBox(renderParticles(r, 0, 64, 64));
    CHECK(box[0] == box[2]);
    CHECK(box[3] - box[1] >= 17);

    // A filled rectangle 20 x 10 centered on the emitter.
    r = spread();
    r.source = ParticleRecipe::Source::Rectangle;
    r.sourceWidth = 20;
    r.sourceHeight = 10;
    box = litBox(renderParticles(r, 0, 64, 64));
    CHECK(box[0] >= 22);
    CHECK(box[2] <= 42);
    CHECK(box[1] >= 27);
    CHECK(box[3] <= 37);
    CHECK(box[2] - box[0] >= 17);
    CHECK(box[3] - box[1] >= 7);
    const int filledPixels = countPixels(renderParticles(r, 0, 64, 64));
    // Only its edge: far fewer pixels, none well inside.
    r.sourceFill = false;
    const Image edge = renderParticles(r, 0, 64, 64);
    CHECK(countPixels(edge) < filledPixels);
    for (int y = 29; y <= 35; ++y)
        for (int x = 25; x <= 39; ++x)
            CHECK((edge.pixel(x, y) >> 24) == 0);

    // An ellipse 20 x 10: the corners of its box stay empty.
    r = spread();
    r.source = ParticleRecipe::Source::Ellipse;
    r.sourceWidth = 20;
    r.sourceHeight = 10;
    const Image oval = renderParticles(r, 0, 64, 64);
    CHECK((oval.pixel(23, 28) >> 24) == 0);
    CHECK((oval.pixel(41, 36) >> 24) == 0);
    CHECK(countPixels(oval) > 80);
    CHECK(countPixels(oval) < filledPixels);

    // The same seed gives the same picture; another seed another spread.
    CHECK(renderParticles(r, 0, 64, 64) == oval);
    r.seed = 77;
    CHECK(renderParticles(r, 0, 64, 64) != oval);
}

TEST_CASE("particles: a field pulls particles toward it, or pushes them away")
{
    ParticleRecipe r = still();
    r.count = 1;
    r.x = 16.5;
    r.y = 16.5;
    r.lifetime = 8;
    r.speed = 0;
    const auto column = [&](int frame) {
        const Image image = renderParticles(r, frame, 40, 40);
        for (int x = 0; x < 40; ++x)
            for (int y = 0; y < 40; ++y)
                if (image.pixel(x, y) >> 24)
                    return std::pair{x, y};
        return std::pair{-1, -1};
    };

    // Nothing pulls: it stays where it was born.
    CHECK(column(5) == std::pair{16, 16});
    // A field to the right pulls it right, and the farther it gets the longer it has been pulled.
    r.fields = {ParticleField{ParticleField::Type::Point, 30.5, 16.5, 0.5, 0, 1}};
    CHECK(column(0) == std::pair{16, 16});
    CHECK(column(4).first > 16);
    CHECK(column(4).second == 16);
    CHECK(column(7).first > column(4).first);
    // A negative strength pushes it away instead.
    r.fields[0].strength = -0.5;
    CHECK(column(4).first < 16);
    // Out of reach it does nothing; within, less at the edge than in the middle.
    r.fields[0].strength = 0.5;
    r.fields[0].radius = 5;
    CHECK(column(5) == std::pair{16, 16});
    r.fields[0].x = 20.5;
    r.fields[0].radius = 8;
    r.fields[0].falloff = 1;
    const int constant = column(2).first;
    r.fields[0].falloff = 0;
    const int soft = column(2).first;
    CHECK(constant >= soft);
    CHECK(soft > 16);
    // Deterministic.
    CHECK(renderParticles(r, 5, 40, 40) == renderParticles(r, 5, 40, 40));
}

TEST_CASE("particles: a vortex swirls clockwise, a wind pushes along its angle")
{
    ParticleRecipe r = still();
    r.count = 1;
    r.x = 16.5;
    r.y = 16.5;
    r.lifetime = 8;
    r.speed = 0;
    const auto where = [&](int frame) {
        const Image image = renderParticles(r, frame, 48, 48);
        for (int x = 0; x < 48; ++x)
            for (int y = 0; y < 48; ++y)
                if (image.pixel(x, y) >> 24)
                    return std::pair{x, y};
        return std::pair{-1, -1};
    };

    // A vortex centered to the left: the particle is on its right side, and clockwise there is down.
    r.fields = {ParticleField{ParticleField::Type::Vortex, 6.5, 16.5, 0.3, 0, 1}};
    CHECK(where(4).second > 16);
    CHECK(where(4).first >= 16); // it is not pulled in, only around
    // The other way round for a negative strength.
    r.fields[0].strength = -0.3;
    CHECK(where(4).second < 16);

    // A wind: along 0 degrees it is pushed right, along 90 down, along 180 left.
    r.fields = {ParticleField{ParticleField::Type::Wind, 0, 0, 0.3, 0, 0, 0}};
    CHECK(where(4).first > 16);
    CHECK(where(4).second == 16);
    r.fields[0].angle = 90;
    CHECK(where(4).second > 16);
    CHECK(where(4).first == 16);
    r.fields[0].angle = 180;
    CHECK(where(4).first < 16);

    // Turbulence moves the path off the straight line, the same way every time.
    r.fields[0].angle = 0;
    r.lifetime = 12;
    r.fields[0].turbulence = 80;
    r.fields[0].scale = 6;
    CHECK(where(10).second != 16);
    CHECK(where(10) == where(10));
    const auto gusty = where(10);
    r.seed = 99; // another seed, another weather
    CHECK(where(10) != gusty);

    // A looping effect sees the same wind every period, so it tiles.
    r = still();
    r.count = 8;
    r.loop = true;
    r.endFrame = 7;
    r.lifetime = 12;
    r.speed = 0;
    r.fields = {ParticleField{ParticleField::Type::Wind, 0, 0, 0.2, 0, 0, 20, 60, 8}};
    for (int f = 0; f < 8; ++f)
        CHECK(renderParticles(r, f, 48, 48) == renderParticles(r, f + 8, 48, 48));
}

TEST_CASE("particles: a floor bounces, holds or removes the particles that fall on it")
{
    ParticleRecipe r = still();
    r.count = 1;
    r.x = 16.5;
    r.y = 5.5;
    r.lifetime = 40;
    r.speed = 0;
    r.gravityY = 0.4;
    const auto row = [&](int frame) {
        const Image image = renderParticles(r, frame, 32, 48);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 32; ++x)
                if (image.pixel(x, y) >> 24)
                    return y;
        return -1;
    };
    const double floorY = 25.5;
    const auto floorField = [&](int mode, int bounciness) {
        ParticleField f;
        f.type = ParticleField::Type::Floor;
        f.y = floorY;
        f.mode = mode;
        f.bounciness = bounciness;
        return f;
    };

    // No floor: it falls through.
    CHECK(row(12) > 30); // 5.5 + 0.4 * (1 + ... + 12) = 36.7

    // Bounce: never below the line, and it comes back up after touching it.
    r.fields = {floorField(0, 60)};
    int deepest = 0, afterTouch = 0;
    bool touched = false;
    for (int f = 0; f < 40; ++f) {
        const int y = row(f);
        REQUIRE(y >= 0);
        CHECK(y <= 26);
        if (y >= 24)
            touched = true;
        if (touched)
            afterTouch = std::max(afterTouch, 1);
        deepest = std::max(deepest, y);
    }
    CHECK(touched);
    CHECK(deepest >= 24);
    // A bounce rises again: some later frame is higher than an earlier one that was on the floor.
    int floorFrame = -1;
    for (int f = 0; f < 40 && floorFrame < 0; ++f)
        if (row(f) >= 24)
            floorFrame = f;
    REQUIRE(floorFrame >= 0);
    bool rose = false;
    for (int f = floorFrame + 1; f < 40; ++f)
        rose = rose || row(f) < row(floorFrame) - 1;
    CHECK(rose);

    // Bounciness 0 or at rest: it settles on the line and stays.
    r.fields = {floorField(0, 0)};
    CHECK(row(39) >= 24);
    CHECK(row(39) <= 26);

    // Stick: on the line from the landing on, not moving.
    r.fields = {floorField(1, 50)};
    CHECK(row(35) == row(39));
    CHECK(row(39) >= 24);
    CHECK(row(39) <= 26);

    // Vanish: gone once it reaches the line, there before.
    r.fields = {floorField(2, 50)};
    CHECK(row(2) > 0);
    CHECK(row(39) == -1);

    // Deterministic, and a floor above the particle's start (it begins below it) lets it fall.
    CHECK(renderParticles(r, 20, 32, 48) == renderParticles(r, 20, 32, 48));
}

TEST_CASE("particles: a floor that stacks lets particles pile up instead of landing in one place")
{
    ParticleRecipe r = still();
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 12;
    r.startFrame = 0;
    r.endFrame = 11;
    r.x = 16.5;
    r.y = 5.5;
    r.lifetime = 14;
    r.lifetimeRandom = 0;
    r.speed = 0;
    r.gravityY = 0.5;
    ParticleField floor;
    floor.type = ParticleField::Type::Floor;
    floor.y = 30.5;
    floor.mode = 1; // stick
    r.fields = {floor};

    // Stuck but not stacking: the four that have landed by frame 13 share one spot on the line (the
    // rest are still in the air above it), and they vanish when their lifetime is over.
    const auto onLine = [](const Image& image) {
        int n = 0;
        for (int x = 0; x < 32; ++x)
            n += (image.pixel(x, 30) >> 24) != 0;
        return n;
    };
    CHECK(onLine(renderParticles(r, 13, 32, 40)) == 1);
    CHECK(countPixels(renderParticles(r, 40, 32, 40)) == 0);

    // Stacking: all twelve are there, resting on each other, in a heap.
    r.fields[0].stack = true;
    const Image heap = renderParticles(r, 40, 32, 40);
    CHECK(countPixels(heap) == 12);
    int top = 40, left = 32, right = -1, bottom = -1;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 32; ++x)
            if (heap.pixel(x, y) >> 24) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
    CHECK(bottom == 29); // the lowest row sits on the line
    CHECK(top < 29);
    CHECK(right - left >= 2); // it spreads out as it grows, it is not a needle
    // Each column's pile has no gaps: standing on the floor or on a pixel below it.
    for (int x = left; x <= right; ++x) {
        bool seenEmptyBelow = false;
        for (int y = 29; y >= top; --y) {
            const bool lit = (heap.pixel(x, y) >> 24) != 0;
            if (!lit)
                seenEmptyBelow = true;
            else
                CHECK_FALSE(seenEmptyBelow);
        }
    }
    // The pile grows with time: fewer at an earlier frame, and it is the same every time.
    CHECK(countPixels(renderParticles(r, 14, 32, 40)) < 12);
    CHECK(renderParticles(r, 40, 32, 40) == heap);
    // It holds past the particles' lifetime, until the end of the animation.
    CHECK(renderParticles(r, 90, 32, 40) == heap);

    // A looping effect does not pile up.
    r.loop = true;
    CHECK(countPixels(renderParticles(r, 20, 32, 40)) < 12);
}

TEST_CASE("particles: the Fire preset is placed at the emitter and loops")
{
    const ParticlePreset* fire = findParticlePreset("fire");
    REQUIRE(fire);
    CHECK(fire->name == "Fire");
    CHECK(fire->recipe.preset == "fire");
    CHECK(findParticlePreset("nothing") == nullptr);
    CHECK(findParticlePreset("") == nullptr);

    // Placed: the emitter and the fields' places move by the same amount; nothing else changes.
    const ParticleRecipe at = placedPreset(*fire, 20.5, 40.5);
    CHECK(at.x == 20.5);
    CHECK(at.y == 40.5);
    REQUIRE(at.fields.size() == fire->recipe.fields.size());
    CHECK(at.fields[0].x == fire->recipe.fields[0].x + 20.5);
    CHECK(at.fields[0].y == fire->recipe.fields[0].y + 40.5);
    ParticleRecipe moved = at;
    moved.x = at.x;
    CHECK_FALSE(differsFromReference(at, at));

    // It differs from itself only by what was changed, not by the seed or a wind's handle.
    ParticleRecipe edited = at;
    edited.seed = 999;
    edited.fields[0].x += 5;
    CHECK_FALSE(differsFromReference(edited, at));
    edited.lifetime += 1;
    CHECK(differsFromReference(edited, at));

    // It draws something, and a period later the picture is the same (it tiles).
    const Image first = renderParticles(at, 0, 48, 64);
    CHECK(countPixels(first) > 20);
    for (int f = 0; f < 4; ++f)
        CHECK(renderParticles(at, f, 48, 64) == renderParticles(at, f + 16, 48, 64));
}

TEST_CASE("particles: every preset tiles over its period and draws something")
{
    for (const ParticlePreset& preset : particlePresets()) {
        INFO(preset.id);
        const ParticleRecipe r = placedPreset(preset, 24.5, 56.5);
        if (!r.loop)
            continue; // checked on its own
        const int period = particleFramesNeeded(r);
        CHECK(period == std::max(1, r.endFrame - r.startFrame + 1));
        for (int f = 0; f < 3; ++f) {
            CHECK(countPixels(renderParticles(r, f, 48, 64)) > 10);
            CHECK(renderParticles(r, f, 48, 64) == renderParticles(r, f + period, 48, 64));
        }
    }
    CHECK(findParticlePreset("smoke"));
    CHECK(findParticlePreset("sparks"));
}

TEST_CASE("particles: a preset is scaled by the sprite's smaller side, within limits")
{
    CHECK(presetScaleFor(64, 64) == 1.0);
    CHECK(presetScaleFor(128, 64) == 1.0); // the smaller side
    CHECK(presetScaleFor(32, 200) == 0.5);
    CHECK(presetScaleFor(96, 96) == 1.5);
    CHECK(presetScaleFor(16, 16) == 0.5);      // not smaller than half
    CHECK(presetScaleFor(2000, 2000) == 3.0);  // not bigger than three times

    const ParticlePreset* fire = findParticlePreset("fire");
    REQUIRE(fire);
    const ParticleRecipe one = placedPreset(*fire, 10, 10, 1.0);
    const ParticleRecipe two = placedPreset(*fire, 10, 10, 2.0);
    CHECK(two.presetScale == 2.0);
    // What is distance or size doubles; times, counts of frames and angles do not.
    CHECK(two.sizeStart == one.sizeStart * 2);
    CHECK(two.speed == one.speed * 2);
    CHECK(two.sourceWidth == one.sourceWidth * 2);
    CHECK(two.gravityY == one.gravityY * 2);
    CHECK(two.count == one.count * 2);
    CHECK(two.fields[0].strength == one.fields[0].strength * 2);
    CHECK(two.fields[0].x - 10 == (one.fields[0].x - 10) * 2);
    CHECK(two.lifetime == one.lifetime);
    CHECK(two.endFrame == one.endFrame);
    CHECK(two.direction == one.direction);
    CHECK(two.drag == one.drag);
    CHECK(two.x == 10);
    CHECK(two.y == 10);
    // The flame is about twice as tall.
    const auto height = [](const ParticleRecipe& r) {
        const Image image = renderParticles(r, 3, 100, 140);
        int top = 140, bottom = -1;
        for (int y = 0; y < 140; ++y)
            for (int x = 0; x < 100; ++x)
                if (image.pixel(x, y) >> 24) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        return bottom - top;
    };
    const double tall1 = height(placedPreset(*fire, 50, 120, 1.0));
    const double tall2 = height(placedPreset(*fire, 50, 120, 2.0));
    CHECK(tall2 > tall1 * 1.6);
    CHECK(tall2 < tall1 * 2.8); // a stray spark or the rounding of sizes can add some
}

TEST_CASE("particles: the snow presets fall from the top, and the pile one settles into drifts")
{
    const ParticlePreset* snow = findParticlePreset("snow");
    const ParticlePreset* pile = findParticlePreset("snowpile");
    REQUIRE(snow);
    REQUIRE(pile);
    CHECK(snow->homeY == 0.0);
    CHECK(pile->homeY == 0.0);

    // A looping snowfall: flakes everywhere from the top to the bottom, tiling.
    const ParticleRecipe falling = placedPreset(*snow, 32.5, 0.5);
    const Image shown = renderParticles(falling, 5, 64, 64);
    int top = 0, bottom = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (shown.pixel(x, y) >> 24)
                (y < 32 ? top : bottom)++;
    CHECK(top > 20);
    CHECK(bottom > 20);

    // The pile: snow is already falling at the first frame (a pre-roll), keeps falling to the last, and the
    // bottom rows fill with drifts as the frames go.
    ParticleRecipe settling = placedPreset(*pile, 32.5, 0.5);
    REQUIRE_FALSE(settling.loop);
    CHECK(settling.startFrame < 0);
    CHECK(settling.untilEnd);
    CHECK(settling.perFrame);
    const int last = 59;
    CHECK(particleFramesNeeded(settling) == 60);
    const auto rowCount = [](const Image& image, int y) {
        int n = 0;
        for (int x = 0; x < 64; ++x)
            n += (image.pixel(x, y) >> 24) != 0;
        return n;
    };
    const auto flakes = [&](int frame) { return countPixels(renderParticles(settling, frame, 64, 64, last)); };
    const Image first = renderParticles(settling, 0, 64, 64, last);
    int upperHalf = 0;
    for (int y = 0; y < 32; ++y)
        upperHalf += rowCount(first, y);
    int lowerHalf = 0;
    for (int y = 32; y < 56; ++y)
        lowerHalf += rowCount(first, y);
    CHECK(upperHalf > 100); // snow all the way down at the very first frame, not a curtain beginning at the top
    CHECK(lowerHalf > 100);
    const Image end = renderParticles(settling, last, 64, 64, last);
    CHECK(rowCount(end, 59) > 40); // the floor row is nearly all covered
    int highest = 64;
    for (int y = 50; y < 64; ++y)
        if (rowCount(end, y) > 0) {
            highest = y;
            break;
        }
    CHECK(highest < 57); // a drift stands up from the line
    CHECK(flakes(last) > flakes(0) / 2); // and it is still snowing at the end
    // A longer sprite is as thick, not thinner: the same snow a frame.
    const int perFrameShort = flakes(30);
    const ParticleRecipe longer = settling;
    const int perFrameLong = countPixels(renderParticles(longer, 30, 64, 64, 200));
    CHECK(perFrameShort == perFrameLong); // frame 30 is the same whatever the length after it
}

namespace {

// One drop falling straight onto a floor 20 rows below it.
ParticleRecipe drop()
{
    ParticleRecipe r = still();
    r.count = 1;
    r.x = 20.5;
    r.y = 4.5;
    r.direction = 90;
    r.spread = 0;
    r.speed = 1.5;
    r.speedRandom = 0;
    r.lifetime = 30;
    r.lifetimeRandom = 0;
    ParticleField floor;
    floor.type = ParticleField::Type::Floor;
    floor.y = 24.5;
    floor.mode = 2; // the drop vanishes
    r.fields = {floor};
    return r;
}

ParticleRecipe splash()
{
    ParticleRecipe c;
    c.emission = ParticleRecipe::Emission::Burst;
    c.count = 6;
    c.lifetime = 4;
    c.lifetimeRandom = 0;
    c.direction = -90;
    c.spread = 120;
    c.speed = 1.2;
    c.speedRandom = 30;
    c.gravityY = 0.25;
    c.sizeStart = c.sizeEnd = 1;
    c.opacityStart = c.opacityEnd = 100;
    c.colors = {Rgba{200, 220, 255, 255}};
    return c;
}

} // namespace

TEST_CASE("particles: a drop that hits the floor throws a splash from where it hit")
{
    ParticleRecipe r = drop();
    r.subTrigger = ParticleRecipe::Trigger::Impact;
    r.children = {splash()};

    // The drop reaches the floor row at age 14 or so (4.5 + 1.5 * 14 = 25.5): find the frame it is gone.
    int hit = -1;
    for (int f = 0; f < 30 && hit < 0; ++f) {
        const Image image = renderParticles(drop(), f, 48, 40);
        if (countPixels(image) == 0)
            hit = f;
    }
    REQUIRE(hit > 5);

    // Before it, only the drop; from it, the splash around the point it hit, above the floor.
    CHECK(countPixels(renderParticles(r, hit - 2, 48, 40)) == 1);
    const Image splashed = renderParticles(r, hit + 1, 48, 40);
    CHECK(countPixels(splashed) >= 3);
    const auto box = litBox(splashed);
    CHECK(box[0] >= 14);
    CHECK(box[2] <= 28);   // around x = 20
    CHECK(box[3] <= 25);   // not below the floor
    CHECK(box[1] >= 15);   // thrown up from it
    // It is over after its lifetime, and the same every time.
    CHECK(countPixels(renderParticles(r, hit + 12, 48, 40)) == 0);
    CHECK(renderParticles(r, hit + 1, 48, 40) == splashed);

    // Without the trigger nothing is thrown.
    r.subTrigger = ParticleRecipe::Trigger::None;
    CHECK(countPixels(renderParticles(r, hit + 1, 48, 40)) == 0);
}

TEST_CASE("particles: a rocket bursts into sparks when it dies, and they can carry its speed")
{
    ParticleRecipe r = still();
    r.count = 1;
    r.x = 24.5;
    r.y = 44.5;
    r.direction = -90;
    r.spread = 0;
    r.speed = 2.5;
    r.speedRandom = 0;
    r.gravityY = 0.1;
    r.lifetime = 12;
    r.lifetimeRandom = 0;
    r.subTrigger = ParticleRecipe::Trigger::Death;
    ParticleRecipe spark = splash();
    spark.count = 16;
    spark.direction = 0;
    spark.spread = 360; // all round
    spark.speed = 1.6;
    spark.speedRandom = 0;
    spark.gravityY = 0;
    spark.lifetime = 5;
    r.children = {spark};

    // While it flies there is the one rocket; the frame after its last, a ring of sparks.
    CHECK(countPixels(renderParticles(r, 6, 48, 48)) == 1);
    const Image burst = renderParticles(r, 12 + 2, 48, 48);
    CHECK(countPixels(burst) >= 8);
    const auto box = litBox(burst);
    const double centerX = (box[0] + box[2]) / 2.0, centerY = (box[1] + box[3]) / 2.0;
    CHECK(centerX == doctest::Approx(24.5).epsilon(0.1));
    CHECK(box[2] - box[0] >= 4); // spread both ways
    CHECK(box[3] - box[1] >= 4);
    CHECK(centerY < 30);          // up where the rocket got to
    CHECK(countPixels(renderParticles(r, 12 + 12, 48, 48)) == 0);

    // Carrying its speed: the whole burst moves up with it.
    const double without = centerY;
    r.subInherit = 100;
    const auto boxUp = litBox(renderParticles(r, 12 + 2, 48, 48));
    CHECK((boxUp[1] + boxUp[3]) / 2.0 < without - 1);
}

TEST_CASE("particles: sub-particles meet their parent's floor and tile in a loop")
{
    ParticleRecipe r = drop();
    r.subTrigger = ParticleRecipe::Trigger::Impact;
    ParticleRecipe fall = splash();
    fall.gravityY = 0.6; // pulled hard back down
    fall.lifetime = 12;
    r.children = {fall};
    for (int f = 10; f < 40; ++f) {
        const auto box = litBox(renderParticles(r, f, 48, 40));
        if (box[3] >= 0)
            CHECK(box[3] <= 25); // never under the line
    }

    // A looping drop, a round of splashes every period.
    r = drop();
    r.loop = true;
    r.endFrame = 17;
    r.count = 6;
    r.lifetime = 16;
    r.subTrigger = ParticleRecipe::Trigger::Impact;
    r.children = {splash()};
    for (int f = 0; f < 6; ++f)
        CHECK(renderParticles(r, f, 48, 40) == renderParticles(r, f + 18, 48, 40));
}

TEST_CASE("particles: rain streaks fall and splash on the floor, fireworks burst when the rockets die")
{
    const ParticlePreset* rain = findParticlePreset("rain");
    const ParticlePreset* fireworks = findParticlePreset("fireworks");
    REQUIRE(rain);
    REQUIRE(fireworks);
    CHECK(rain->homeY == 0.0);
    CHECK(rain->recipe.subTrigger == ParticleRecipe::Trigger::Impact);
    CHECK(fireworks->recipe.subTrigger == ParticleRecipe::Trigger::Death);

    // Rain: nothing is drawn under the floor line (the drops vanish there), and the splash puts pixels just above it.
    const ParticleRecipe wet = placedPreset(*rain, 32.5, 0.5);
    int splashPixels = 0;
    for (int f = 0; f < 16; ++f) {
        const Image image = renderParticles(wet, f, 64, 72);
        for (int y = 62; y < 72; ++y) // the streak's end may hang a pixel over the line
            for (int x = 0; x < 64; ++x)
                CHECK((image.pixel(x, y) >> 24) == 0);
        for (int y = 54; y < 62; ++y)
            for (int x = 0; x < 64; ++x)
                splashPixels += (image.pixel(x, y) >> 24) != 0;
    }
    CHECK(splashPixels > 20);
    // Streaks are lines along the fall: taller than wide.
    const Image shown = renderParticles(wet, 3, 64, 72);
    int pairs = 0;
    for (int y = 4; y < 50; ++y)
        for (int x = 0; x < 64; ++x)
            if ((shown.pixel(x, y) >> 24) && (shown.pixel(x, y + 1) >> 24))
                ++pairs;
    CHECK(pairs > 20);

    // Fireworks: the sparks reach well away from the rockets' line, up and to both sides.
    const ParticleRecipe show = placedPreset(*fireworks, 32.5, 58.5);
    int farUp = 0, left = 0, right = 0;
    for (int f = 0; f < 30; ++f) {
        const Image image = renderParticles(show, f, 64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (image.pixel(x, y) >> 24) {
                    farUp += y < 30;
                    left += x < 20;
                    right += x > 44;
                }
    }
    CHECK(farUp > 100);
    CHECK(left > 20);
    CHECK(right > 20);
}

TEST_CASE("particles: a group preset is made of presets that exist, sharing the longest loop")
{
    const ParticleGroupPreset* camp = findParticleGroupPreset("campfire");
    REQUIRE(camp);
    CHECK(findParticleGroupPreset("fire") == nullptr);
    REQUIRE(camp->parts.size() == 2);
    int period = 0;
    for (const auto& part : camp->parts) {
        const ParticlePreset* p = findParticlePreset(part.preset);
        REQUIRE(p);
        period = std::max(period, particleFramesNeeded(p->recipe));
    }
    CHECK(period == 32);
    // The shorter loop is a whole number of times in the longer one, so the group tiles at 32.
    for (const auto& part : camp->parts)
        CHECK(period % particleFramesNeeded(findParticlePreset(part.preset)->recipe) == 0);
    CHECK(camp->parts[1].dy < 0); // the smoke is above the flame
}

TEST_CASE("particles: a pre-roll starts the effect before the first frame, a rate keeps its thickness, until the end follows the sprite")
{
    ParticleRecipe r = still();
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 30;
    r.x = 16.5;
    r.y = 2.5;
    r.direction = 90;
    r.spread = 0;
    r.speed = 1;
    r.speedRandom = 0;
    r.lifetime = 40;
    r.lifetimeRandom = 0;
    r.startFrame = 0;
    r.endFrame = 29;
    const auto lit = [&](const ParticleRecipe& recipe, int frame, int last = -1) {
        return countPixels(renderParticles(recipe, frame, 32, 64, last));
    };

    // Without a pre-roll the first frame has the first particle only; with one, it is under way.
    CHECK(lit(r, 0) == 1);
    ParticleRecipe before = r;
    before.startFrame = -20;
    before.endFrame = 9; // the same 30 frames of emission, 20 of them before the first
    CHECK(lit(before, 0) > 10);
    // Pre-roll frames are what the later ones would have been: frame 0 of it is frame 20 of the other.
    ParticleRecipe shifted = r;
    CHECK(renderParticles(before, 0, 32, 64) == renderParticles(shifted, 20, 32, 64));
    CHECK(renderParticles(before, 5, 32, 64) == renderParticles(shifted, 25, 32, 64));

    // A rate: the number of particles follows the length; a total does not.
    ParticleRecipe rated = r;
    rated.perFrame = true;
    rated.rate = 2;
    CHECK(resolvedRecipe(rated, -1).count == 60); // 2 a frame over 30 frames
    rated.endFrame = 59;
    CHECK(resolvedRecipe(rated, -1).count == 120);
    // Until the end: the end is the sprite's last frame, whatever the recipe says.
    rated.untilEnd = true;
    CHECK(resolvedRecipe(rated, 99).endFrame == 99);
    CHECK(resolvedRecipe(rated, 99).count == 200);
    CHECK(resolvedRecipe(rated, 9).endFrame == 9);
    CHECK(resolvedRecipe(rated, -1).endFrame == 59); // not known: the recipe's own
    // A burst has neither.
    ParticleRecipe burst = rated;
    burst.emission = ParticleRecipe::Emission::Burst;
    CHECK(resolvedRecipe(burst, 99).count == burst.count);
    // The same snow a frame in a longer sprite: frame 10 looks the same.
    CHECK(renderParticles(rated, 10, 32, 64, 30) == renderParticles(rated, 10, 32, 64, 100));
    // And it keeps going to the last frame.
    CHECK(lit(rated, 80, 100) > 0);
    CHECK(lit(rated, 80, 60) == lit(rated, 80, 60)); // (beyond the last frame nothing is asked for)
}

TEST_CASE("particles: a whole layer, stepped once and on several threads, is the same as each frame drawn alone")
{
    // Enough particles to be spread over threads: a pile on a floor, a turbulent wind, an attractor, a
    // vortex, dithered fading and sub-particles born where the rain lands.
    const auto sameAsAlone = [](const ParticleRecipe& r, int frames) {
        Document doc(128, 128);
        for (int i = 1; i < frames; ++i)
            doc.insertFrame(i, Frame{});
        Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("Particles"));
        layer->particles = r;
        renderParticleLayer(doc, *layer);
        int drawn = 0;
        for (int f = 0; f < frames; ++f) {
            const Image alone = renderParticles(r, f, 128, 128, frames - 1);
            const CelRef cel = layer->cel(f);
            if (!cel) {
                CHECK(countPixels(alone) == 0);
                continue;
            }
            ++drawn;
            CHECK(*cel->image == alone);
        }
        CHECK(drawn > frames / 2);
    };

    ParticleRecipe pile = placedPreset(*findParticlePreset("snowpile"), 64, 0, 2.0);
    sameAsAlone(pile, 40);

    ParticleRecipe swirl = placedPreset(*findParticlePreset("smoke"), 64, 100, 2.0);
    ParticleField point;
    point.type = ParticleField::Type::Point;
    point.x = 64;
    point.y = 40;
    point.radius = 60;
    ParticleField vortex = point;
    vortex.type = ParticleField::Type::Vortex;
    swirl.fields.push_back(point);
    swirl.fields.push_back(vortex);
    swirl.count = 1500;
    swirl.lifetime = 60;
    sameAsAlone(swirl, 40);

    ParticleRecipe rain = placedPreset(*findParticlePreset("rain"), 64, 0, 2.0);
    rain.count = 1200;
    sameAsAlone(rain, 32);
}

TEST_CASE("particles: the emitter and its fields move with the artwork when the sprite changes size")
{
    Document doc(16, 16);
    for (int i = 1; i < 3; ++i)
        doc.insertFrame(i, Frame{});
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("Particles"));
    layer->particles = still();
    ParticleField floor;
    floor.type = ParticleField::Type::Floor;
    floor.x = 3;
    floor.y = 12.5;
    layer->particles->fields = {floor};
    renderParticleLayer(doc, *layer);
    // One read from a file and waiting for a decision moves too, so rendering it again lands in place.
    Layer* waiting = doc.insertLayer(doc.root(), 1, doc.makeImageLayer("Edited elsewhere"));
    waiting->staleRecipe = still();

    const auto litAt = [&](int x, int y) {
        Image frame(doc.width(), doc.height());
        renderFrame(doc, 0, frame, doc.bounds());
        return lit(frame, x, y);
    };
    REQUIRE(litAt(8, 8));

    SUBCASE("Canvas Size: shifted by the crop")
    {
        auto command = canvasSizeCommand(doc, Rect{4, 2, 20, 20}, 0);
        command->redo(doc);
        CHECK(layer->particles->x == doctest::Approx(4.5));
        CHECK(layer->particles->y == doctest::Approx(6.5));
        CHECK(layer->particles->fields[0].x == doctest::Approx(-1));
        CHECK(layer->particles->fields[0].y == doctest::Approx(10.5));
        CHECK(waiting->staleRecipe->x == doctest::Approx(4.5));
        CHECK(litAt(4, 6));
        CHECK_FALSE(litAt(8, 8));

        command->undo(doc);
        CHECK(layer->particles->x == doctest::Approx(8.5));
        CHECK(layer->particles->fields[0].y == doctest::Approx(12.5));
        CHECK(waiting->staleRecipe->y == doctest::Approx(8.5));
        CHECK(litAt(8, 8));
    }
    SUBCASE("Sprite Size: scaled by the factor, speeds and sizes as they were")
    {
        auto command = scaleSpriteCommand(doc, 32, 48, Resample::Nearest);
        command->redo(doc);
        CHECK(layer->particles->x == doctest::Approx(17));
        CHECK(layer->particles->y == doctest::Approx(25.5));
        CHECK(layer->particles->fields[0].x == doctest::Approx(6));
        CHECK(layer->particles->fields[0].y == doctest::Approx(37.5));
        CHECK(layer->particles->sizeStart == doctest::Approx(1));
        CHECK(waiting->staleRecipe->x == doctest::Approx(17));
        CHECK(litAt(17, 25));

        command->undo(doc);
        CHECK(layer->particles->x == doctest::Approx(8.5));
        CHECK(layer->particles->y == doctest::Approx(8.5));
        CHECK(litAt(8, 8));
    }
}

namespace {

// One square particle, thrown from (x, y) at `vx, vy` a frame, with gravity `g`, living long enough.
ParticleRecipe dropped(double x, double y, double speed, double direction, double g)
{
    ParticleRecipe r;
    r.count = 1;
    r.emission = ParticleRecipe::Emission::Burst;
    r.lifetime = 200;
    r.lifetimeRandom = 0;
    r.x = x;
    r.y = y;
    r.direction = direction;
    r.spread = 0;
    r.speed = speed;
    r.speedRandom = 0;
    r.gravityY = g;
    return r;
}

ParticleField collider(double x, double y, std::vector<ParticlePoint> points, int mode)
{
    ParticleField f;
    f.type = ParticleField::Type::Collider;
    f.x = x;
    f.y = y;
    f.points = std::move(points);
    f.mode = mode;
    return f;
}

// Where the only lit pixel of a frame is (-1, -1 for none).
std::array<int, 2> litPixel(const Image& image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (lit(image, x, y))
                return {x, y};
    return {-1, -1};
}

} // namespace

TEST_CASE("particles: a collider holds particles from either side, and a fast one can't pass through")
{
    // A horizontal line at y 20 from x 0 to 40 (its handle in the middle).
    const ParticleField line = collider(20, 20, {{-20, 0}, {20, 0}}, 1);

    ParticleRecipe falling = dropped(10.5, 5.5, 0, 90, 0.5);
    falling.fields = {line};
    const auto down = litPixel(renderParticles(falling, 40, 48, 48));
    CHECK(down[0] == 10);
    CHECK(down[1] == 19); // resting just above the line

    ParticleRecipe rising = dropped(10.5, 35.5, 2, -90, 0);
    rising.fields = {line};
    const auto up = litPixel(renderParticles(rising, 40, 48, 48));
    CHECK(up[1] == 20); // just below it

    ParticleRecipe fast = dropped(10.5, 5.5, 30, 90, 0); // 30 px a frame: from above the line to far past it in one step
    fast.fields = {line};
    CHECK(litPixel(renderParticles(fast, 3, 48, 48))[1] == 19);

    ParticleRecipe beside = dropped(44.5, 5.5, 0, 90, 0.5); // past the line's end: it goes by
    beside.fields = {line};
    CHECK(litPixel(renderParticles(beside, 8, 48, 48))[1] > 20);
}

TEST_CASE("particles: with no bounciness a particle slides down a collider's slope and off its end")
{
    // A slope down to the right: from (10, 20) to (40, 35).
    ParticleRecipe r = dropped(15.5, 5.5, 0, 90, 0.2);
    ParticleField slope = collider(10, 20, {{0, 0}, {30, 15}}, 0);
    slope.bounciness = 0;
    r.fields = {slope};
    const auto onIt = litPixel(renderParticles(r, 20, 64, 64));
    REQUIRE(onIt[0] >= 0);
    CHECK(onIt[0] > 16); // moved right, along the slope
    // On the line (within a pixel), not through it: y = 20 + (x - 10) / 2.
    CHECK(std::abs(onIt[1] - (20 + (onIt[0] - 10) / 2.0)) <= 1.5);
    const auto later = litPixel(renderParticles(r, 60, 64, 64));
    CHECK((later[0] > 40 || later[0] < 0)); // off the end, or fallen out of the picture

    // With bounciness it leaves the line instead.
    slope.bounciness = 80;
    r.fields = {slope};
    bool above = false;
    for (int f = 10; f < 30 && !above; ++f) {
        const auto at = litPixel(renderParticles(r, f, 64, 64));
        above = at[0] >= 0 && at[1] < 20 + (at[0] - 10) / 2.0 - 3;
    }
    CHECK(above);
}

TEST_CASE("particles: a collider can end particles, and a hit gives birth to sub-particles there")
{
    ParticleRecipe r = dropped(20.5, 5.5, 0, 90, 0.5);
    r.fields = {collider(20, 30, {{-10, 0}, {10, 0}}, 2)};
    CHECK(litPixel(renderParticles(r, 40, 48, 48))[0] == -1);

    ParticleRecipe child;
    child.emission = ParticleRecipe::Emission::Burst;
    child.count = 4;
    child.lifetime = 30;
    child.lifetimeRandom = 0;
    child.speed = 0;
    child.speedRandom = 0;
    child.spread = 0;
    r.subTrigger = ParticleRecipe::Trigger::Impact;
    r.children = {child};
    // The parent meets the line at about frame 10; its sub-particles live 30 frames there.
    const auto at = litPixel(renderParticles(r, 25, 48, 48));
    CHECK(at[0] == 20);
    CHECK(at[1] == 29); // born where it met the line, on its side

    // Falling, they meet the line like their parent: on a line that holds, they rest on it.
    r.fields[0].mode = 1;
    r.children[0].gravityY = 0.5;
    const auto fell = litPixel(renderParticles(r, 25, 48, 48));
    CHECK(fell[1] == 29);

    // Moved with the sprite: the points are relative to the handle, so they scale and don't shift.
    const ParticleRecipe moved = movedRecipe(r, 2, 3, 5, 7);
    CHECK(moved.fields[0].x == doctest::Approx(45));
    CHECK(moved.fields[0].y == doctest::Approx(97));
    CHECK(moved.fields[0].points[0].x == doctest::Approx(-20));
    CHECK(moved.fields[0].points[1].y == doctest::Approx(0));
}

TEST_CASE("particles: snow piles up on a collider too, and a steep one sheds it down to its low end")
{
    // A stream dropped on one spot of a flat line that holds and stacks: a heap above the line, on it.
    ParticleRecipe r = dropped(20.5, 2.5, 0, 90, 0.5);
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 30;
    r.startFrame = 0;
    r.endFrame = 29;
    ParticleField line = collider(20, 30, {{-20, 0}, {20, 0}}, 1);
    line.stack = true;
    r.fields = {line};
    const Image heap = renderParticles(r, 80, 48, 48);
    CHECK(countPixels(heap) == 30); // each on its own pixel
    int lowest = 0, left = 48, right = 0, rows = 0;
    for (int y = 0; y < 48; ++y) {
        bool any = false;
        for (int x = 0; x < 48; ++x)
            if (lit(heap, x, y)) {
                any = true;
                lowest = std::max(lowest, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        rows += any;
    }
    CHECK(lowest == 29);  // the first ones sit right on the line
    CHECK(rows > 2);      // stacked up
    CHECK(right > left);  // and spread sideways, like a heap
    CHECK(left >= 0);
    CHECK(right < 40);

    // A steep line, from (10, 10) down to (14, 40): what lands on it slides to its low end, not past it.
    ParticleRecipe one = dropped(11.5, 2.5, 0, 90, 0.5);
    ParticleField steep = collider(10, 10, {{0, 0}, {4, 30}}, 1);
    steep.stack = true;
    one.fields = {steep};
    const auto rested = litPixel(renderParticles(one, 60, 48, 48));
    CHECK(rested[0] == 13); // the last column the line is over
    CHECK(rested[1] == 35);
    // Without stacking it stays where it hit.
    one.fields[0].stack = false;
    const auto stuck = litPixel(renderParticles(one, 60, 48, 48));
    CHECK(stuck[0] == 11);
    CHECK(stuck[1] == 21);
}

TEST_CASE("particles: a custom easing curve shapes size and opacity, the named ones being curves too")
{
    ParticleRecipe r = still();
    r.lifetime = 10;
    r.sizeStart = 1;
    r.sizeEnd = 9;
    r.sizeEase = ParticleRecipe::Ease::In;
    std::vector<Image> named;
    for (int f = 0; f < 10; ++f)
        named.push_back(renderParticles(r, f, 32, 32));
    // Custom, with In's own curve: the same pictures.
    r.sizeEase = ParticleRecipe::Ease::Custom;
    r.sizeCurve = namedEasing(1);
    for (int f = 0; f < 10; ++f)
        CHECK(renderParticles(r, f, 32, 32) == named[size_t(f)]);
    // A curve that is all the way there at once: full size from the first frame.
    r.sizeCurve = {0, 1, 0, 1};
    CHECK(countPixels(renderParticles(r, 1, 32, 32)) > countPixels(named[1]));
}
