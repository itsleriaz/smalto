#include <doctest/doctest.h>

#include "core/BlendMode.h"

using namespace core;

TEST_CASE("mul255 is exact")
{
    CHECK(mul255(255, 255) == 255);
    CHECK(mul255(0, 255) == 0);
    CHECK(mul255(128, 255) == 128);
    CHECK(mul255(255, 1) == 1);
}

TEST_CASE("normal blending")
{
    const Rgba red{255, 0, 0, 255};
    const Rgba blue{0, 0, 255, 255};

    CHECK(blend(red, blue, 255, BlendMode::Normal) == blue);
    CHECK(blend(red, blue, 0, BlendMode::Normal) == red);
    CHECK(blend({}, blue, 255, BlendMode::Normal) == blue);

    const Rgba half = blend(red, blue, 128, BlendMode::Normal);
    CHECK(half.a == 255);
    CHECK(half.r == 127);
    CHECK(half.b == 128);

    // Transparent source leaves the backdrop alone.
    CHECK(blend(red, Rgba{0, 255, 0, 0}, 255, BlendMode::Normal) == red);

    // Two half-transparent layers.
    const Rgba a = blend(Rgba{255, 0, 0, 128}, Rgba{0, 0, 255, 128}, 255, BlendMode::Normal);
    CHECK(a.a == 192);
}

TEST_CASE("separable modes on opaque backdrop")
{
    const Rgba b{200, 100, 50, 255};
    const Rgba s{100, 100, 100, 255};
    CHECK(blend(b, s, 255, BlendMode::Multiply) == Rgba{78, 39, 20, 255});
    CHECK(blend(b, s, 255, BlendMode::Darken) == Rgba{100, 100, 50, 255});
    CHECK(blend(b, s, 255, BlendMode::Lighten) == Rgba{200, 100, 100, 255});
    CHECK(blend(b, s, 255, BlendMode::Difference) == Rgba{100, 0, 50, 255});
    CHECK(blend(b, s, 255, BlendMode::Addition) == Rgba{255, 200, 150, 255});
    CHECK(blend(b, s, 255, BlendMode::Subtract) == Rgba{100, 0, 0, 255});
}

TEST_CASE("non-normal modes over transparent backdrop act like normal")
{
    const Rgba s{10, 20, 30, 255};
    for (int m = 0; m < kBlendModeCount; ++m)
        CHECK(blend({}, s, 255, BlendMode(m)) == s);
}

TEST_CASE("luminosity keeps backdrop hue for gray source")
{
    const Rgba gray{128, 128, 128, 255};
    const Rgba r = blend(gray, gray, 255, BlendMode::Luminosity);
    CHECK(r == gray);
}
