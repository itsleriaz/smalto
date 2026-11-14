#pragma once

#include <cstdint>

namespace core {

// Straight (non-premultiplied) alpha color.
struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    static constexpr Rgba fromHex(uint32_t rgb, uint8_t alpha = 255)
    {
        return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), alpha};
    }

    bool operator==(const Rgba&) const = default;
};

// Packed RGBA8 as stored in memory (byte order r, g, b, a on little endian).
inline constexpr uint32_t pack(Rgba c)
{
    return uint32_t(c.r) | uint32_t(c.g) << 8 | uint32_t(c.b) << 16 | uint32_t(c.a) << 24;
}

inline constexpr Rgba unpack(uint32_t v)
{
    return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
}

// Exact rounding a*b/255 for 0..255 inputs.
inline constexpr int mul255(int a, int b)
{
    const int t = a * b + 128;
    return (t + (t >> 8)) >> 8;
}

} // namespace core
