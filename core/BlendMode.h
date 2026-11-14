#pragma once

#include "core/Color.h"

namespace core {

// Values match the Aseprite file format.
enum class BlendMode : uint16_t {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
    Addition,
    Subtract,
    Divide,
};

constexpr int kBlendModeCount = int(BlendMode::Divide) + 1;

const char* blendModeName(BlendMode mode);

// Composites src over dst (both straight alpha). opacity is 0..255.
Rgba blend(Rgba dst, Rgba src, int opacity, BlendMode mode);

} // namespace core
