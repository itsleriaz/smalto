#pragma once

#include "core/Text.h"

#include <optional>
#include <string>
#include <string_view>

namespace formats {

// Reads a BDF bitmap font (the subset the misc-fixed fonts use: FONT,
// FONT_ASCENT / FONT_DESCENT, and per glyph ENCODING, DWIDTH, BBX, BITMAP).
// Glyphs with no encoding (-1) are skipped. Nullopt (and `error`) if it isn't
// a usable BDF font.
std::optional<core::BitmapFont> parseBdf(std::string_view text, std::string* error = nullptr);

} // namespace formats
