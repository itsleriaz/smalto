#pragma once

#include "core/Image.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

// A bitmap font (read from BDF by formats::parseBdf): glyphs by code point,
// each with its bitmap and where it sits relative to the baseline.
struct Glyph {
    int advance = 0;         // pixels to the next glyph
    int width = 0, height = 0;
    int xOffset = 0;         // from the pen position to the bitmap's left
    int yOffset = 0;         // from the baseline up to the bitmap's bottom
    std::vector<uint8_t> bits; // width x height, row by row from the top, 1 = ink
};

struct BitmapFont {
    std::string name;
    int ascent = 0;  // above the baseline
    int descent = 0; // below it
    std::unordered_map<uint32_t, Glyph> glyphs;

    int lineHeight() const { return ascent + descent; }
    // The glyph for `code`, or '?' / nothing when the font lacks it.
    const Glyph* glyph(uint32_t code) const;
};

enum class TextAlign : uint8_t { Left, Center, Right };

// `text` (code points; '\n' starts a new line) as an Indexed8 image, 1 = ink,
// as wide as its furthest inked column and lines x lineHeight tall (each line
// lineHeight + `lineGap` apart); shorter lines sit left, centered or right in
// that width. Empty text gives an empty image.
Image renderText(const BitmapFont& font, const std::u32string& text, int letterSpacing = 0, int lineGap = 0,
                 TextAlign align = TextAlign::Left);

// Where the insertion point is for `index` code points into `text`, in the
// coordinates of renderText's image: x of the line's pen there, y of the
// line's top, h the line height. (The image can be narrower than the caret's x.)
struct TextCaret {
    int x = 0, y = 0, h = 0;
};
TextCaret textCaret(const BitmapFont& font, const std::u32string& text, size_t index, int letterSpacing = 0,
                    int lineGap = 0, TextAlign align = TextAlign::Left);

// The same font a pixel wider and heavier: each glyph drawn again one pixel to
// the right, advances one more.
BitmapFont emboldened(const BitmapFont& font);

// A 3 x 5 font of capitals, digits and punctuation, made here (lowercase
// letters use the capitals), for tiny labels.
BitmapFont tinyFont();

} // namespace core
