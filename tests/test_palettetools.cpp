#include <doctest/doctest.h>

#include "core/PaletteTools.h"

#include <cmath>
#include <vector>

using namespace core;

namespace {

const Rgba kBlack{0, 0, 0, 255};
const Rgba kWhite{255, 255, 255, 255};
const Rgba kRed{255, 0, 0, 255};
const Rgba kGreen{0, 255, 0, 255};
const Rgba kBlue{0, 0, 255, 255};
const Rgba kGray{128, 128, 128, 255};

} // namespace

TEST_CASE("OKLab round-trips sRGB colors")
{
    for (const Rgba c : {kBlack, kWhite, kRed, kGreen, kBlue, kGray, Rgba{12, 200, 99, 255}}) {
        CAPTURE(int(c.r)); CAPTURE(int(c.g)); CAPTURE(int(c.b));
        const Rgba back = fromOklab(toOklab(c));
        CHECK(std::abs(back.r - c.r) <= 1);
        CHECK(std::abs(back.g - c.g) <= 1);
        CHECK(std::abs(back.b - c.b) <= 1);
    }
    CHECK(toOklab(kBlack).L == doctest::Approx(0).epsilon(0.001));
    CHECK(toOklab(kWhite).L == doctest::Approx(1).epsilon(0.001));
}

TEST_CASE("ramp: exact ends, even lightness steps, alpha linear")
{
    const auto ramp = rampColors(kBlack, kWhite, 5);
    REQUIRE(ramp.size() == 5);
    CHECK(ramp.front() == kBlack);
    CHECK(ramp.back() == kWhite);
    for (size_t i = 1; i < ramp.size(); ++i) {
        CHECK(ramp[i].r == ramp[i].g); // grays stay gray
        CHECK(toOklab(ramp[i]).L - toOklab(ramp[i - 1]).L == doctest::Approx(0.25).epsilon(0.02));
    }
    const auto fade = rampColors({255, 0, 0, 0}, {255, 0, 0, 255}, 3);
    CHECK(fade[1].a == 128);
    CHECK(rampColors(kRed, kBlue, 1).size() == 2); // at least the two ends
}

TEST_CASE("sort orders: hue with grays first, saturation, lightness; stable")
{
    const std::vector<Rgba> colors{kBlue, kWhite, kRed, kGreen, kBlack, kGray};
    CHECK(sortOrder(colors, PaletteSort::Hue) == std::vector<int>{4, 5, 1, 2, 3, 0});
    CHECK(sortOrder(colors, PaletteSort::Lightness) == std::vector<int>{4, 0, 5, 2, 3, 1});
    const auto bySaturation = sortOrder(colors, PaletteSort::Saturation);
    CHECK(std::vector<int>(bySaturation.begin(), bySaturation.begin() + 3) == std::vector<int>{4, 5, 1}); // grays by lightness
    // Equal keys keep their order.
    CHECK(sortOrder({kRed, kRed, kRed}, PaletteSort::Hue) == std::vector<int>{0, 1, 2});
}

TEST_CASE("reduce colors: merges near colors, keeps order, maps every entry")
{
    const std::vector<Rgba> colors{kRed, {250, 5, 5, 255}, kBlue, {0, 0, 250, 255}, kWhite};
    const auto r = reduceColors(colors, {10, 0, 1, 1, 3}, 3);
    REQUIRE(r.colors.size() == 3);
    CHECK(r.map == std::vector<int>{0, 0, 1, 1, 2});
    CHECK(r.colors[0].r >= 250); // weighted toward the red that's used
    CHECK(r.colors[2] == kWhite);

    // Already small enough: unchanged.
    const auto same = reduceColors(colors, {}, 8);
    CHECK(same.colors == colors);
    CHECK(same.map == std::vector<int>{0, 1, 2, 3, 4});

    // Duplicates count as one color.
    const auto dup = reduceColors({kRed, kRed, kBlue}, {}, 2);
    CHECK(dup.colors == std::vector<Rgba>{kRed, kBlue});
    CHECK(dup.map == std::vector<int>{0, 0, 1});
}

TEST_CASE("median cut: weighted averages, never more boxes than colors")
{
    const auto two = medianCut({{kBlack, 1}, {kWhite, 1}}, 4);
    CHECK(two.size() == 2);
    const auto one = medianCut({{kBlack, 3}, {kWhite, 1}}, 1);
    REQUIRE(one.size() == 1);
    CHECK(one[0].r == 64);
}

TEST_CASE("palette tools: a hue arc ramp keeps its chroma between two hues")
{
    const Rgba purple{60, 20, 120, 255}, yellow{255, 230, 120, 255};
    const auto straight = rampColors(purple, yellow, 5, false);
    const auto arc = rampColors(purple, yellow, 5, true);
    REQUIRE(arc.size() == 5);
    CHECK(arc.front() == purple);
    CHECK(arc.back() == yellow);
    const auto chroma = [](Rgba c) { const Oklab o = toOklab(c); return std::hypot(o.a, o.b); };
    CHECK(chroma(arc[2]) > chroma(straight[2])); // the straight blend drifts toward gray
    CHECK(arc[2] != straight[2]);
    // A gray end takes the other end's hue instead of swinging through another.
    const auto fromGray = rampColors({128, 128, 128, 255}, {255, 0, 0, 255}, 3, true);
    const Oklab mid = toOklab(fromGray[1]);
    const Oklab red = toOklab({255, 0, 0, 255});
    CHECK(std::atan2(mid.b, mid.a) == doctest::Approx(std::atan2(red.b, red.a)).epsilon(0.05));
}
