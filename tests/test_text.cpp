#include <doctest/doctest.h>

#include "core/Text.h"
#include "formats/Bdf.h"

#include <fstream>
#include <sstream>

using namespace core;

namespace {

const char* kFont = R"(STARTFONT 2.1
FONT -Test-Tiny
FONT_ASCENT 3
FONT_DESCENT 1
CHARS 2
STARTCHAR A
ENCODING 65
DWIDTH 3 0
BBX 2 3 0 0
BITMAP
40
C0
40
ENDCHAR
STARTCHAR question
ENCODING 63
DWIDTH 2 0
BBX 1 1 0 -1
BITMAP
80
ENDCHAR
ENDFONT
)";

std::string rows(const Image& image)
{
    std::string out;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            out += image.pixel(x, y) ? '#' : '.';
        out += '\n';
    }
    return out;
}

} // namespace

TEST_CASE("text: BDF fonts parse")
{
    const auto font = formats::parseBdf(kFont);
    REQUIRE(font);
    CHECK(font->ascent == 3);
    CHECK(font->descent == 1);
    REQUIRE(font->glyphs.count(65));
    const Glyph& a = font->glyphs.at(65);
    CHECK(a.advance == 3);
    CHECK(a.bits == std::vector<uint8_t>{0, 1, 1, 1, 0, 1});
    CHECK_FALSE(formats::parseBdf("hello"));
    CHECK_FALSE(formats::parseBdf("STARTFONT 2.1\nENDFONT\n"));
}

TEST_CASE("text: rendering lines, baselines and missing glyphs")
{
    const auto font = formats::parseBdf(kFont);
    REQUIRE(font);
    // "AA": up to the last inked column (3 + 2), 4 tall.
    CHECK(rows(renderText(*font, U"AA")) == ".#..#\n"
                                             "##.##\n"
                                             ".#..#\n"
                                             ".....\n");
    // A missing glyph shows as '?' (here a dot below the baseline).
    CHECK(rows(renderText(*font, U"Z")) == ".\n.\n.\n#\n");
    // Two lines, one pixel apart.
    const Image two = renderText(*font, U"A\nA", 0, 1);
    CHECK(two.height() == 9);
    CHECK(two.pixel(1, 5) == 1);
    CHECK(renderText(*font, U"").isNull());
}

TEST_CASE("text: the shipped fonts read and cover Latin-1")
{
    for (const char* name : {"4x6", "5x7", "6x10"}) {
        std::ifstream file(std::string(SMALTO_FONT_DIR) + "/" + name + ".bdf");
        REQUIRE(file);
        std::stringstream text;
        text << file.rdbuf();
        const auto font = formats::parseBdf(text.str());
        REQUIRE_MESSAGE(font, name);
        CHECK(font->glyphs.size() == 191);
        CHECK(font->glyphs.count(U'A'));
        CHECK(font->glyphs.count(U'é'));
        CHECK(renderText(*font, U"Hi").height() == font->lineHeight());
    }
}

TEST_CASE("text: shorter lines can be centered or right-aligned, and the caret follows")
{
    const auto font = formats::parseBdf(kFont);
    REQUIRE(font);
    // "AAA" is 3 + 3 + 2 = 8 wide; "A" is 2: left, centered by 3, right by 6.
    const Image left = renderText(*font, U"AAA\nA", 0, 0, TextAlign::Left);
    const Image center = renderText(*font, U"AAA\nA", 0, 0, TextAlign::Center);
    const Image right = renderText(*font, U"AAA\nA", 0, 0, TextAlign::Right);
    REQUIRE(left.width() == 8);
    CHECK(left.pixel(1, 4) == 1);   // the A's top, second line
    CHECK(center.pixel(4, 4) == 1); // 3 px over
    CHECK(right.pixel(7, 4) == 1);  // 6 px over

    // The caret: after one A the pen is 3 along; on the second line it's at its start.
    CHECK(textCaret(*font, U"AAA\nA", 1).x == 3);
    CHECK(textCaret(*font, U"AAA\nA", 1).y == 0);
    CHECK(textCaret(*font, U"AAA\nA", 4).x == 0);
    CHECK(textCaret(*font, U"AAA\nA", 4).y == font->lineHeight());
    CHECK(textCaret(*font, U"AAA\nA", 4, 0, 0, TextAlign::Right).x == 6);
    CHECK(textCaret(*font, U"AAA\nA", 99).h == font->lineHeight()); // past the end: the end
}

TEST_CASE("text: the bold font is a pixel wider and the 3 x 5 font has the capitals")
{
    const auto font = formats::parseBdf(kFont);
    REQUIRE(font);
    const BitmapFont bold = emboldened(*font);
    const Image plain = renderText(*font, U"A");
    const Image heavy = renderText(bold, U"A");
    CHECK(heavy.width() == plain.width() + 1);
    CHECK(heavy.pixel(0, 1) == 1);
    CHECK(heavy.pixel(2, 1) == 1); // the right shifted copy
    CHECK(bold.glyph('A')->advance == font->glyph('A')->advance + 1);

    const BitmapFont tiny = tinyFont();
    CHECK(tiny.lineHeight() == 5);
    for (char32_t c = 'A'; c <= 'Z'; ++c)
        CHECK(tiny.glyphs.count(c) == 1);
    CHECK(tiny.glyphs.count('a') == 1); // lowercase uses the capitals
    CHECK(rows(renderText(tiny, U"T")) == "###\n.#.\n.#.\n.#.\n.#.\n");
    CHECK(rows(renderText(tiny, U"H")) == "#.#\n#.#\n###\n#.#\n#.#\n");
}
