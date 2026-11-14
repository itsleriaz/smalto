#include "core/Palette.h"

namespace core {

Palette Palette::defaultPalette()
{
    // PICO-8 palette plus pure black/white.
    static constexpr uint32_t colors[] = {
        0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
        0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
        0xFFFFFF,
    };
    Palette p;
    for (uint32_t c : colors)
        p.entries.push_back({Rgba::fromHex(c), {}});
    return p;
}

} // namespace core
