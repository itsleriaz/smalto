#pragma once

#include "core/Color.h"

#include <string>
#include <vector>

namespace core {

struct PaletteEntry {
    Rgba color;
    std::string name;
};

class Palette {
public:
    std::vector<PaletteEntry> entries;

    int size() const { return int(entries.size()); }
    // Out-of-range indices return transparent.
    Rgba color(int index) const
    {
        return index >= 0 && index < size() ? entries[size_t(index)].color : Rgba{};
    }

    static Palette defaultPalette();
};

} // namespace core
