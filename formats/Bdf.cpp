#include "formats/Bdf.h"

#include <charconv>
#include <vector>

namespace formats {

namespace {

// Splits a line into its space-separated words.
std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
            ++i;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

bool toInt(std::string_view s, int& out, int base = 10)
{
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out, base);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

} // namespace

std::optional<core::BitmapFont> parseBdf(std::string_view text, std::string* error)
{
    const auto fail = [&](const char* message) -> std::optional<core::BitmapFont> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    core::BitmapFont font;
    bool started = false, ascentSet = false;
    // The glyph being read.
    bool inChar = false, inBitmap = false;
    int code = -1, row = 0;
    core::Glyph glyph;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        const auto w = words(line);
        if (w.empty())
            continue;
        const std::string_view key = w[0];
        if (!started) {
            if (key != "STARTFONT")
                return fail("not a BDF font");
            started = true;
            continue;
        }
        if (inBitmap) {
            if (key == "ENDCHAR") {
                if (code >= 0 && row == glyph.height)
                    font.glyphs[uint32_t(code)] = glyph;
                inChar = inBitmap = false;
                continue;
            }
            if (row >= glyph.height)
                continue;
            // A row is hex, its leftmost bit first, padded to whole bytes.
            for (int x = 0; x < glyph.width; ++x) {
                const size_t digit = size_t(x / 4);
                int nibble = 0;
                if (digit >= key.size() || !toInt(key.substr(digit, 1), nibble, 16))
                    return fail("bad bitmap row");
                if (nibble & (8 >> (x % 4)))
                    glyph.bits[size_t(row * glyph.width + x)] = 1;
            }
            ++row;
            continue;
        }
        if (key == "FONT" && w.size() > 1) {
            font.name = std::string(line.substr(line.find("FONT") + 5));
        } else if (key == "FONT_ASCENT" && w.size() > 1) {
            ascentSet = toInt(w[1], font.ascent);
        } else if (key == "FONT_DESCENT" && w.size() > 1) {
            toInt(w[1], font.descent);
        } else if (key == "STARTCHAR") {
            inChar = true;
            code = -1;
            glyph = {};
        } else if (inChar && key == "ENCODING" && w.size() > 1) {
            toInt(w[1], code);
        } else if (inChar && key == "DWIDTH" && w.size() > 1) {
            toInt(w[1], glyph.advance);
        } else if (inChar && key == "BBX" && w.size() > 4) {
            if (!toInt(w[1], glyph.width) || !toInt(w[2], glyph.height) || !toInt(w[3], glyph.xOffset)
                || !toInt(w[4], glyph.yOffset) || glyph.width < 0 || glyph.height < 0 || glyph.width > 256
                || glyph.height > 256)
                return fail("bad glyph box");
        } else if (inChar && key == "BITMAP") {
            inBitmap = true;
            row = 0;
            glyph.bits.assign(size_t(glyph.width) * size_t(glyph.height), 0);
        }
    }
    if (!started || !ascentSet || font.glyphs.empty())
        return fail("no glyphs");
    return font;
}

} // namespace formats
