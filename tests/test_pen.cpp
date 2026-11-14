#include <doctest/doctest.h>

#include "core/Pen.h"

#include <cmath>

using namespace core;

TEST_CASE("pressure level follows the curve and is clamped")
{
    const EasingCurve linear;
    CHECK(pressureLevel(linear, 0) == doctest::Approx(0));
    CHECK(pressureLevel(linear, 0.5) == doctest::Approx(0.5).epsilon(0.01));
    CHECK(pressureLevel(linear, 1) == doctest::Approx(1));
    CHECK(pressureLevel(linear, 2) == doctest::Approx(1));
    CHECK(pressureLevel(linear, -1) == 0);
    // "In" (t^2): light pressure counts for less.
    CHECK(pressureLevel(namedEasing(1), 0.5) == doctest::Approx(0.25).epsilon(0.02));
    // An overshooting curve stays within 0..1.
    const EasingCurve over{0.3, 1.6, 0.6, 1.4};
    for (double p = 0; p <= 1; p += 0.05) {
        CHECK(pressureLevel(over, p) >= 0);
        CHECK(pressureLevel(over, p) <= 1);
    }
}

TEST_CASE("pressure size goes from 1 px (or the minimum) to the brush size")
{
    CHECK(pressureSize(9, 1, 0) == 9);
    CHECK(pressureSize(9, 0, 0) == 1);
    CHECK(pressureSize(9, 0.5, 0) == 5);
    CHECK(pressureSize(9, 0, 50) == 5);
    CHECK(pressureSize(9, 0, 100) == 9);
    CHECK(pressureSize(1, 0, 0) == 1);
    CHECK(pressureSize(2, 0.2, 0) == 1);
    for (int i = 0; i <= 10; ++i) // never past the brush or under 1
        CHECK((pressureSize(20, i / 10.0, 30) >= 1 && pressureSize(20, i / 10.0, 30) <= 20));
}

TEST_CASE("pressure colors: mixing, painting over and erasing")
{
    const Rgba red{255, 0, 0, 255}, blue{0, 0, 255, 255};
    CHECK(mixRgba(red, blue, 0) == red);
    CHECK(mixRgba(red, blue, 1) == blue);
    CHECK(mixRgba(red, blue, 0.5) == Rgba{128, 0, 128, 255});

    CHECK(paintOver(red, blue, 1) == blue);
    CHECK(paintOver(red, blue, 0) == red);
    CHECK(paintOver(red, blue, 0.5) == Rgba{128, 0, 128, 255});
    // Over nothing the color stays, at that strength.
    CHECK(paintOver(Rgba{}, blue, 0.5) == Rgba{0, 0, 255, 128});
    CHECK(paintOver(Rgba{}, blue, 0) == Rgba{});

    CHECK(eraseFrom(red, 1) == Rgba{});
    CHECK(eraseFrom(red, 0) == red);
    CHECK(eraseFrom(red, 0.5) == Rgba{255, 0, 0, 128});
}

TEST_CASE("tilt gives the brush angle in steps, steady while upright or within a step")
{
    CHECK(tiltAngle(40, 0, 0) == 0);    // leaning right
    CHECK(tiltAngle(0, 40, 0) == 90);   // toward the bottom: clockwise on screen
    CHECK(tiltAngle(0, -40, 0) == -90);
    CHECK(tiltAngle(-40, 0, 0) == 180);
    CHECK(tiltAngle(30, 30, 0) == 45);
    // Nearly upright: the angle it had.
    CHECK(tiltAngle(3, -4, 35) == 35);
    // Within most of a step of the current angle: kept (no flicker at a step's edge).
    CHECK(tiltAngle(std::cos(0.06) * 40, std::sin(0.06) * 40, 0) == 0); // about 3.4 degrees
    CHECK(tiltAngle(std::cos(0.12) * 40, std::sin(0.12) * 40, 0) == 5); // about 6.9 degrees
    // Across the -180 / 180 seam.
    CHECK(tiltAngle(-40, -1, 180) == 180);
    CHECK(tiltAngle(-40, 1, -180) == -180);
}

TEST_CASE("rope stabilizer follows only past its length")
{
    Rope rope{10, 10};
    CHECK_FALSE(rope.pull(13, 14, 5)); // 5 away: within reach
    CHECK(rope.x == 10);
    CHECK(rope.pull(20, 10, 5));       // 10 away: pulled to 5 behind
    CHECK(rope.x == doctest::Approx(15));
    CHECK(rope.y == doctest::Approx(10));
    // Jitter around a point the brush already trails doesn't move it.
    CHECK_FALSE(rope.pull(19, 13, 5)); // exactly 5 away
    CHECK_FALSE(rope.pull(17, 8, 5));
    // No string: the brush is the pointer.
    CHECK(rope.pull(30, 30, 0));
    CHECK(rope.x == doctest::Approx(30));
    CHECK(rope.y == doctest::Approx(30));
}
