#pragma once

#include "core/Color.h"
#include "core/Easing.h"
#include "core/Image.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace core {

// Where the art is at one moment of a Smear group's motion: moved by (x, y), and turned (degrees,
// clockwise), scaled and skewed (degrees) about the recipe's pivot. The identity leaves the art as drawn.
struct SmearPose {
    double x = 0;
    double y = 0;
    double rotation = 0;
    double scaleX = 1;
    double scaleY = 1;
    double skewX = 0;
    double skewY = 0;

    bool operator==(const SmearPose&) const = default;
};

// A pose on a frame, and how the motion gets from it to the next key.
struct SmearKey {
    enum class Ease : uint8_t { Linear, In, Out, InOut, Custom };
    int frame = 0;
    SmearPose pose;
    Ease ease = Ease::Linear;
    EasingCurve curve; // when `ease` is Custom

    bool operator==(const SmearKey&) const = default;
};

// The recipe of a Smear group: its art (the group's children) posed by keys, tweened between them, and
// smeared along the motion. Every frame is a function of the recipe, the frame number and the art of
// that frame, like a particle layer's.
struct SmearRecipe {
    enum class Style : uint8_t { Streak, Flat };

    // The point the art turns, scales and skews about, in canvas pixels of the art as drawn.
    double pivotX = 0;
    double pivotY = 0;
    // Sorted by frame, one per frame. Before the first key the art holds its pose, after the last too;
    // with no key it stays as drawn.
    std::vector<SmearKey> keys;

    // How far back the smear reaches: 1 is all the way to where the art was a frame before, 0 none.
    double trail = 1;
    bool behind = true; // the smear under the art, or over it
    // Streak drags the art's own colors along; Flat fills the swept shape with `color`.
    Style style = Style::Streak;
    Rgba color{255, 255, 255, 255};
    // How much the smear narrows toward its tail (0 none, 1 to nothing), shrinking toward the pivot.
    double taper = 0;
    // How much it fades toward the tail (0 none, 1 gone at the tail), by `fadePattern`: 0 smooth (alpha),
    // 1 Bayer 2x2, 2 Bayer 4x4, 3 Bayer 8x8, 4 blue noise (whole pixels shown or not).
    double fade = 0;
    int fadePattern = 2;
    // A one-pixel outline around the smear (not around the art).
    bool outline = false;
    Rgba outlineColor{0, 0, 0, 255};
    // The move between keys follows a smooth curve through their places (Catmull-Rom) instead of straight
    // lines; and the art turns along the way it goes (art drawn pointing right points ahead), on top of
    // the keys' own turn.
    bool smoothPath = false;
    bool facePath = false;

    bool operator==(const SmearRecipe&) const = default;
};

// A ready-made motion: its keys, pivot and smear settings, made for the art where it is.
struct SmearPreset {
    std::string id;   // stable
    std::string name; // shown
    std::string description;
    int frames = 1; // how many frames it takes, from the first key to the last
};
const std::vector<SmearPreset>& smearPresets();
const SmearPreset* findSmearPreset(std::string_view id);

// A recipe made independent of the art (to save as a preset): the keys from frame 0, the moves in
// units of the art's longer side, the pivot as a place on the art's box (0..1 across it).
SmearRecipe relativeSmear(const SmearRecipe& recipe, Rect art);
// Such a recipe for art whose box is `art`, its first key on `start`, its colors `base`'s.
SmearRecipe placedSmear(const SmearRecipe& relative, Rect art, int start, const SmearRecipe& base);

// The preset as a recipe for art whose box (as drawn, canvas pixels) is `art`, its first key on `start`;
// distances follow the art's size. The colors are `base`'s (the group's own). Unknown id: `base`.
SmearRecipe smearPresetRecipe(std::string_view id, Rect art, int start, const SmearRecipe& base);

// The recipe moved as the artwork moves when the sprite changes size: the pivot to pivot * scale + d,
// the keys' moves (relative to the art) scaled.
SmearRecipe movedSmear(const SmearRecipe& recipe, double scaleX, double scaleY, double dx, double dy);

// Where the point (x, y) of the art as drawn is at `pose`: (qx, qy) on the canvas.
void smearMapPoint(const SmearRecipe& recipe, const SmearPose& pose, double x, double y, double& qx, double& qy);

// The pose at `time` (in frames; between frames for the in-betweens of a smear).
SmearPose smearPoseAt(const SmearRecipe& recipe, double time);

// One frame of a Smear group as RGBA, the size of `art`: the art (RGBA, canvas-sized, as drawn at that
// frame) at its pose, with the smear.
Image renderSmear(const SmearRecipe& recipe, const Image& art, int frame);

} // namespace core
