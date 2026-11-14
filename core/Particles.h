#pragma once

#include "core/Color.h"
#include "core/Easing.h"
#include "core/Image.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace core {

class Document;
struct Layer;

// The shape of a Brush particle: a copy of a custom brush's pixels (packed RGBA as in `pack`), taken when
// the brush was picked, so the recipe doesn't depend on the editor's brush any more.
struct ParticleBrush {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // width * height, row by row

    bool empty() const { return width <= 0 || height <= 0 || pixels.size() != size_t(width) * size_t(height); }
    bool operator==(const ParticleBrush&) const = default;
};

// A force that acts on the particles of a layer. A Point pulls particles toward a place on the canvas (a
// negative strength pushes them away, a repeller), up to `radius` pixels from it (0: no limit), as
// strongly everywhere (`falloff` 1) or less and less out to the edge (0). A Vortex swirls them around
// the place, clockwise for a positive strength. Wind pushes everywhere along `angle` (degrees, 0 right,
// 90 down); with `turbulence` (percent) the direction wanders by up to that part of half a turn, with
// gusts about `scale` pixels across and a frame apart. A wind has a place only for its handle. A Floor is
// a horizontal line at `y` that particles above it cannot cross: they bounce (keeping `bounciness`
// percent of their speed and losing `friction` percent of their sideways speed), stick where they land,
// or vanish (`mode` 0, 1, 2). Its x is only for its handle. A Collider is a line of any shape through
// `points` (relative to x, y, which is its handle): particles can't cross it from either side; it
// bounces, holds or ends them like a floor, and a bounce keeps the motion along the line, so with no
// bounciness they slide down a slope and off its end.
struct ParticlePoint {
    double x = 0;
    double y = 0;
    bool operator==(const ParticlePoint&) const = default;
};

struct ParticleField {
    enum class Type : uint8_t { Point, Vortex, Wind, Floor, Collider };
    Type type = Type::Point;
    double x = 0; // canvas pixels
    double y = 0;
    double strength = 0.2; // pixels per frame squared at its strongest
    int radius = 30;
    int falloff = 0;
    double angle = 0;
    int turbulence = 0;
    int scale = 24;
    int mode = 0;
    int bounciness = 50;
    int friction = 0;
    bool stack = false; // with `mode` 1 (stick): later particles come to rest on top of the earlier ones
    std::vector<ParticlePoint> points; // a Collider's corners, at least two, relative to (x, y)

    bool operator==(const ParticleField&) const = default;
};

// A particle emitter: the recipe of a live layer. Every frame is a pure function of the recipe and
// the frame number (no state carried from frame to frame, no simulation of the whole sprite), so
// scrubbing, undo and export agree. Each particle's properties come from the seed and its index; a
// particle at age t is stepped from its birth, which allows gravity and drag.
struct ParticleRecipe {
    enum class Emission : uint8_t { Burst, Stream };
    // Where particles are born: at the emitter, along a line through it, or in or on a rectangle or an
    // ellipse centered on it (all turned by `sourceAngle`).
    // What makes a particle give birth to `children[0]`: its end (its lifetime running out, or
    // vanishing on a floor), or its first touch of a floor.
    enum class Trigger : uint8_t { None, Death, Impact };
    enum class Source : uint8_t { Point, Line, Rectangle, Ellipse };
    enum class Shape : uint8_t { Square, Circle, Plus, Diamond, Brush };
    enum class ColorMode : uint8_t { OverLife, Random };
    // How a value over a particle's life gets from its start to its end.
    // Custom: the curve kept beside it (sizeCurve, opacityCurve), see core/Easing.
    enum class Ease : uint8_t { Linear, In, Out, InOut, Custom };

    // Emission. A burst is born all at `startFrame`; a stream spreads `count` over startFrame..endFrame
    // (inclusive), whose length is also the loop's period.
    Emission emission = Emission::Stream;
    int count = 40;
    // A negative start is a pre-roll: the effect began that many frames before the first, so it is
    // under way when the animation starts.
    int startFrame = 0;
    int endFrame = 15;
    // A stream can give `rate` particles every frame instead of `count` in all (so a longer range is
    // not a thinner one), and run until the last frame of the sprite instead of `endFrame`.
    bool perFrame = false;
    double rate = 2.5;
    bool untilEnd = false;
    bool loop = false; // the effect repeats every period, so a sprite's loop has particles at frame 0
    uint32_t seed = 1;
    double presetScale = 1; // how much the preset was scaled by when it was applied
    std::string preset; // the id of the preset it started from (see particlePresets), empty for none

    // Lifetime in frames, and how much it varies (percent either way).
    int lifetime = 12;
    int lifetimeRandom = 25;

    // The emitter, in canvas pixels (a whole number is a pixel's corner, .5 its middle).
    double x = 0;
    double y = 0;
    // The source around it: a line is `sourceWidth` long; a rectangle or an ellipse is `sourceWidth` x
    // `sourceHeight`, filled (born anywhere inside) or only its edge.
    Source source = Source::Point;
    int sourceWidth = 16;
    int sourceHeight = 8;
    double sourceAngle = 0;
    bool sourceFill = true;

    // Motion: direction in degrees (0 right, 90 down, -90 up), the cone's total width, speed in pixels
    // per frame (varied by `speedRandom`), gravity in pixels per frame squared, and the part of the
    // speed lost every frame.
    double direction = -90;
    double spread = 40;
    double speed = 1;
    int speedRandom = 50; // percent either way, one roll per particle
    double gravityX = 0;
    double gravityY = 0;
    double drag = 0;

    // Size in pixels: at birth, at the end of life (eased between), and how much each particle's
    // whole size varies (percent either way, one roll per particle so big ones stay big).
    double sizeStart = 1;
    double sizeEnd = 1;
    int sizeRandom = 0;
    Ease sizeEase = Ease::Linear;
    EasingCurve sizeCurve; // when sizeEase is Custom
    Shape shape = Shape::Square;
    ParticleBrush brush; // used by Shape::Brush; its longest side is the particle's size
    bool brushColors = false; // the brush's own colors instead of the particle's (the color ramp is not used)

    // Orientation: an angle in degrees (clockwise, 0 has the shape as drawn), how far it varies either
    // way, a spin in degrees per frame (varied by `spinRandom` percent), and facing along the motion (the
    // angle then turns the shape from the direction it moves in). `angleSnap` rounds the result to
    // steps: 0 free, 1 15 degrees, 2 45, 3 90. A circle looks the same turned, so it is not.
    double angle = 0;
    int angleRandom = 0;
    double spin = 0;
    int spinRandom = 0;
    bool faceMotion = false;
    int angleSnap = 0;

    // Opacity in percent over life, eased like the size. Smooth scales each color's alpha; the others
    // show or hide each pixel by a dither pattern (what a sprite in indexed mode needs): `fadePattern`
    // 0 smooth, 1 Bayer 2x2, 2 Bayer 4x4, 3 Bayer 8x8, 4 blue noise.
    int opacityStart = 100;
    int opacityEnd = 100;
    Ease opacityEase = Ease::Linear;
    EasingCurve opacityCurve; // when opacityEase is Custom
    int fadePattern = 0;

    // Sub-particles: `subCount` (the child's own `count`) of them are born from each particle where
    // the trigger happens, with `subInherit` percent of its velocity added to theirs. They have a
    // recipe of their own (`children[0]`: its emitter place, seed and loop are not used; its source is
    // around the place of the trigger, its emission frames counted from it) and meet the floors of
    // their parent. One level deep.
    Trigger subTrigger = Trigger::None;
    int subInherit = 0;
    std::vector<ParticleRecipe> children;

    // Forces from places on the canvas, on top of gravity.
    std::vector<ParticleField> fields;

    // Colors: spread evenly over a particle's life, or one picked at random per particle.
    ColorMode colorMode = ColorMode::OverLife;
    std::vector<Rgba> colors{Rgba{255, 255, 255, 255}};

    bool operator==(const ParticleRecipe&) const = default;
};

// A starting point for an effect: a whole recipe with the emitter at (0, 0) and its fields placed
// relative to it. Applying one keeps the layer's emitter where it is (`placedPreset`).
struct ParticlePreset {
    std::string id;   // stable, stored in files
    std::string name; // shown
    std::string description;
    // Where its emitter starts on a new layer, as fractions of the canvas (snow starts at the top).
    double homeX = 0.5;
    double homeY = 0.7;
    ParticleRecipe recipe;
};

// Several presets that go together, added as one group of live layers (bottom to top), each part
// placed at an offset from where the group starts (in a sprite 64 pixels tall; scaled like a preset).
struct ParticleGroupPreset {
    struct Part {
        std::string preset; // an id of particlePresets()
        double dx = 0;
        double dy = 0;
    };
    std::string id;
    std::string name;
    std::string description;
    double homeX = 0.5;
    double homeY = 0.7;
    std::vector<Part> parts;
};

const std::vector<ParticleGroupPreset>& particleGroupPresets();
const ParticleGroupPreset* findParticleGroupPreset(std::string_view id);

const std::vector<ParticlePreset>& particlePresets();
const ParticlePreset* findParticlePreset(std::string_view id);

// How much a preset (made for a sprite 64 pixels tall) is scaled for a sprite of this size: by the
// smaller side, at most three times, at least half (a particle can't be smaller than a pixel).
double presetScaleFor(int width, int height);

// The preset's recipe scaled by `scale` (distances, sizes, speeds, strengths, reach and the number of
// particles; not times, frame counts or angles), with the emitter at (x, y) and its fields placed
// around it.
ParticleRecipe placedPreset(const ParticlePreset& preset, double x, double y, double scale = 1.0);

// The recipe with what is distance or size scaled by `factor` (see placedPreset), sub-particles too.
ParticleRecipe scaledRecipe(const ParticleRecipe& recipe, double factor);

// The recipe with its places on the canvas (the emitter and every field, sub-particles' fields too)
// moved as the artwork moves when the sprite changes size: each x becomes x * scaleX + dx (y alike).
// Speeds, sizes and strengths stay as they are.
ParticleRecipe movedRecipe(const ParticleRecipe& recipe, double scaleX, double scaleY, double dx, double dy);

// How many frames the effect needs to play whole: its period when it loops, else up to its last
// particle's death.
int particleFramesNeeded(const ParticleRecipe& recipe);

// Whether `recipe` differs from `reference` in anything but the seed and where a wind's handle sits.
bool differsFromReference(const ParticleRecipe& recipe, const ParticleRecipe& reference);

// The recipe as it is drawn in a sprite whose last frame is `lastFrame` (-1: not known): a stream that
// runs until the end ends there, and one with a rate has as many particles as the frames take.
ParticleRecipe resolvedRecipe(const ParticleRecipe& recipe, int lastFrame);

// One frame of the emitter as RGBA, `width` x `height`; `lastFrame` is the sprite's last (see above).
Image renderParticles(const ParticleRecipe& recipe, int frame, int width, int height, int lastFrame = -1);

// Fills the layer's cels with the recipe's render for every frame of the document, in the document's
// pixel format (frames without particles get no cel, identical neighbors share one).
void renderParticleLayer(const Document& doc, Layer& layer);

} // namespace core
