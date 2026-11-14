#pragma once

#include "core/Image.h"

#include <cstdint>

// Image transforms for pixel art. All work on raw pixel values, so they
// apply to any pixel format; `transparent` is the format's empty value.
namespace core {

Image flippedHorizontally(const Image& src);
Image flippedVertically(const Image& src);
Image rotated90(const Image& src, bool clockwise);

// Nearest-neighbor resize to w x h (each at least 1).
Image scaledNearest(const Image& src, int w, int h);

// How a sprite is resampled when its size changes.
enum class Resample : uint8_t {
    Nearest,  // hard pixels, every output pixel is a source pixel
    Smooth,   // Scale2x three times, then nearest: straight edges stay clean, no new colors
    Bilinear, // blends neighbours (and averages when shrinking); new colors appear.
              // Indexed and tile images can't blend, so they take Smooth.
};

// Resizes to w x h (each at least 1) by `method`.
Image scaled(const Image& src, int w, int h, Resample method);

// Scale2x (EPX): doubles the size, rounding off stair steps.
Image scale2x(const Image& src);

// Rotation by `degrees` (clockwise on screen) around the center; the
// result is the rotated bounding box, empty corners `transparent`.
// Multiples of 90 degrees are exact. Other angles use a RotSprite-style
// method (Scale2x three times, nearest-neighbor rotation at 8x, sampled
// back down) so lines stay clean; large images fall back to plain
// nearest-neighbor rotation.
Image rotated(const Image& src, double degrees, uint32_t transparent);

// Size of the bounding box of a w x h image rotated by `degrees`.
void rotatedSize(int w, int h, double degrees, int& outW, int& outH);

} // namespace core
