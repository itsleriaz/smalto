#include "core/Text.h"

#include <algorithm>
#include <vector>

namespace core {

const Glyph* BitmapFont::glyph(uint32_t code) const
{
    if (auto it = glyphs.find(code); it != glyphs.end())
        return &it->second;
    if (auto it = glyphs.find('?'); it != glyphs.end())
        return &it->second;
    return nullptr;
}

namespace {

// How a text lays out: the inked extent of each line (its furthest inked
// column from the line's start), and the widest of them.
struct Measure {
    std::vector<int> extent;
    int width = 0;
};

Measure measure(const BitmapFont& font, const std::u32string& text, int letterSpacing)
{
    Measure m;
    m.extent.push_back(0);
    int pen = 0;
    for (const char32_t c : text) {
        if (c == U'\n') {
            m.extent.push_back(0);
            pen = 0;
            continue;
        }
        if (const Glyph* g = font.glyph(c)) {
            if (g->width > 0)
                m.extent.back() = std::max(m.extent.back(), pen + g->xOffset + g->width);
            pen += g->advance + letterSpacing;
        }
    }
    for (const int e : m.extent)
        m.width = std::max(m.width, e);
    return m;
}

int alignOffset(TextAlign align, int width, int extent)
{
    return align == TextAlign::Center ? (width - extent) / 2 : align == TextAlign::Right ? width - extent : 0;
}

} // namespace

Image renderText(const BitmapFont& font, const std::u32string& text, int letterSpacing, int lineGap, TextAlign align)
{
    if (text.empty())
        return {};
    const Measure m = measure(font, text, letterSpacing);
    const int width = m.width;
    const int lines = int(m.extent.size());
    const int height = lines * font.lineHeight() + (lines - 1) * lineGap;
    if (width == 0 || height <= 0)
        return {};
    Image out(width, height, PixelFormat::Indexed8);
    out.clear(0);
    int line = 0;
    int penX = alignOffset(align, width, m.extent[0]);
    for (const char32_t c : text) {
        if (c == U'\n') {
            ++line;
            penX = alignOffset(align, width, m.extent[size_t(line)]);
            continue;
        }
        const Glyph* g = font.glyph(c);
        if (!g)
            continue;
        const int baseline = line * (font.lineHeight() + lineGap) + font.ascent;
        const int top = baseline - g->yOffset - g->height;
        for (int y = 0; y < g->height; ++y)
            for (int x = 0; x < g->width; ++x)
                if (g->bits[size_t(y * g->width + x)]) {
                    const int px = penX + g->xOffset + x, py = top + y;
                    if (px >= 0 && py >= 0 && px < width && py < height)
                        out.setPixel(px, py, 1);
                }
        penX += g->advance + letterSpacing;
    }
    return out;
}

TextCaret textCaret(const BitmapFont& font, const std::u32string& text, size_t index, int letterSpacing, int lineGap,
                    TextAlign align)
{
    const Measure m = measure(font, text, letterSpacing);
    index = std::min(index, text.size());
    int line = 0, pen = 0;
    for (size_t i = 0; i < index; ++i) {
        if (text[i] == U'\n') {
            ++line;
            pen = 0;
            continue;
        }
        if (const Glyph* g = font.glyph(text[i]))
            pen += g->advance + letterSpacing;
    }
    TextCaret caret;
    caret.x = alignOffset(align, m.width, m.extent[size_t(line)]) + pen;
    caret.y = line * (font.lineHeight() + lineGap);
    caret.h = font.lineHeight();
    return caret;
}

BitmapFont emboldened(const BitmapFont& font)
{
    BitmapFont out = font;
    out.name = font.name + " Bold";
    for (auto& [code, g] : out.glyphs) {
        if (g.width == 0)
            continue;
        std::vector<uint8_t> bits(size_t(g.width + 1) * size_t(g.height), 0);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
                if (g.bits[size_t(y * g.width + x)]) {
                    bits[size_t(y * (g.width + 1) + x)] = 1;
                    bits[size_t(y * (g.width + 1) + x + 1)] = 1;
                }
        g.bits = std::move(bits);
        ++g.width;
        ++g.advance;
    }
    return out;
}

BitmapFont tinyFont()
{
    // Rows of three columns, '#' = ink; five rows each.
    struct Def {
        char32_t code;
        const char* rows[5];
    };
    static const Def defs[] = {
        {'A', {".#.", "#.#", "###", "#.#", "#.#"}}, {'B', {"##.", "#.#", "##.", "#.#", "##."}},
        {'C', {".##", "#..", "#..", "#..", ".##"}}, {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
        {'E', {"###", "#..", "##.", "#..", "###"}}, {'F', {"###", "#..", "##.", "#..", "#.."}},
        {'G', {".##", "#..", "#.#", "#.#", ".##"}}, {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
        {'I', {"###", ".#.", ".#.", ".#.", "###"}}, {'J', {"..#", "..#", "..#", "#.#", ".#."}},
        {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}}, {'L', {"#..", "#..", "#..", "#..", "###"}},
        {'M', {"#.#", "###", "###", "#.#", "#.#"}}, {'N', {"##.", "#.#", "#.#", "#.#", "#.#"}},
        {'O', {".#.", "#.#", "#.#", "#.#", ".#."}}, {'P', {"##.", "#.#", "##.", "#..", "#.."}},
        {'Q', {".#.", "#.#", "#.#", "##.", ".##"}}, {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
        {'S', {".##", "#..", ".#.", "..#", "##."}}, {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
        {'U', {"#.#", "#.#", "#.#", "#.#", "###"}}, {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
        {'W', {"#.#", "#.#", "###", "###", "#.#"}}, {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
        {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}}, {'Z', {"###", "..#", ".#.", "#..", "###"}},
        {'0', {"###", "#.#", "#.#", "#.#", "###"}}, {'1', {".#.", "##.", ".#.", ".#.", "###"}},
        {'2', {"##.", "..#", ".#.", "#..", "###"}}, {'3', {"##.", "..#", ".#.", "..#", "##."}},
        {'4', {"#.#", "#.#", "###", "..#", "..#"}}, {'5', {"###", "#..", "##.", "..#", "##."}},
        {'6', {".##", "#..", "###", "#.#", "###"}}, {'7', {"###", "..#", ".#.", ".#.", ".#."}},
        {'8', {"###", "#.#", "###", "#.#", "###"}}, {'9', {"###", "#.#", "###", "..#", "##."}},
        {'.', {"...", "...", "...", "...", ".#."}}, {',', {"...", "...", "...", ".#.", "#.."}},
        {'!', {".#.", ".#.", ".#.", "...", ".#."}}, {'?', {"##.", "..#", ".#.", "...", ".#."}},
        {':', {"...", ".#.", "...", ".#.", "..."}}, {';', {"...", ".#.", "...", ".#.", "#.."}},
        {'-', {"...", "...", "###", "...", "..."}}, {'_', {"...", "...", "...", "...", "###"}},
        {'+', {"...", ".#.", "###", ".#.", "..."}}, {'=', {"...", "###", "...", "###", "..."}},
        {'*', {"...", "#.#", ".#.", "#.#", "..."}}, {'/', {"..#", "..#", ".#.", "#..", "#.."}},
        {'\\', {"#..", "#..", ".#.", "..#", "..#"}}, {'(', {".#.", "#..", "#..", "#..", ".#."}},
        {')', {".#.", "..#", "..#", "..#", ".#."}}, {'[', {"##.", "#..", "#..", "#..", "##."}},
        {']', {".##", "..#", "..#", "..#", ".##"}}, {'{', {".##", ".#.", "##.", ".#.", ".##"}},
        {'}', {"##.", ".#.", ".##", ".#.", "##."}}, {'<', {"..#", ".#.", "#..", ".#.", "..#"}},
        {'>', {"#..", ".#.", "..#", ".#.", "#.."}}, {'\'', {".#.", ".#.", "...", "...", "..."}},
        {'"', {"#.#", "#.#", "...", "...", "..."}}, {'#', {"#.#", "###", "#.#", "###", "#.#"}},
        {'%', {"#.#", "..#", ".#.", "#..", "#.#"}}, {'&', {".#.", "#.#", ".#.", "#.#", ".##"}},
        {'@', {".#.", "#.#", "###", "#..", ".##"}}, {'$', {".##", "##.", "###", ".##", "##."}},
        {'|', {".#.", ".#.", ".#.", ".#.", ".#."}}, {'^', {".#.", "#.#", "...", "...", "..."}},
        {'~', {"...", ".##", "##.", "...", "..."}}, {'`', {"#..", ".#.", "...", "...", "..."}},
    };
    BitmapFont font;
    font.name = "3 \u00d7 5";
    font.ascent = 5;
    font.descent = 0;
    const auto make = [&](char32_t code, const char* const* rows) {
        Glyph g;
        g.advance = 4;
        g.width = 3;
        g.height = 5;
        g.xOffset = 0;
        g.yOffset = 0;
        g.bits.resize(15);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 3; ++x)
                g.bits[size_t(y * 3 + x)] = rows[y][x] == '#' ? 1 : 0;
        font.glyphs[code] = std::move(g);
    };
    for (const Def& d : defs) {
        make(d.code, d.rows);
        if (d.code >= 'A' && d.code <= 'Z')
            make(d.code + 32, d.rows); // lowercase uses the capital
    }
    Glyph space;
    space.advance = 3;
    font.glyphs[U' '] = space;
    return font;
}

} // namespace core
