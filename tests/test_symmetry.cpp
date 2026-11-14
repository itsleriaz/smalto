#include <doctest/doctest.h>

#include "core/Symmetry.h"

#include <algorithm>
#include <vector>

using namespace core;

TEST_CASE("symmetry elements: mirrors and the groups they make")
{
    CHECK(applySymmetry(MirrorLeftRight, {3, 5}) == Point{-3, 5});
    CHECK(applySymmetry(MirrorTopBottom, {3, 5}) == Point{3, -5});
    CHECK(applySymmetry(MirrorFalling, {3, 5}) == Point{5, 3});
    CHECK(applySymmetry(MirrorRising, {3, 5}) == Point{-5, -3});
    for (const int m : {1, 2, 4, 7})
        CHECK(composeSymmetry(m, m) == MirrorNone); // mirrors undo themselves
    CHECK(composeSymmetry(MirrorLeftRight, MirrorTopBottom) == 3); // half turn

    CHECK(symmetryGroup({}) == std::vector<int>{0});
    CHECK(symmetryGroup({MirrorLeftRight}) == std::vector<int>{0, 1});
    auto sorted = [](std::vector<int> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    CHECK(sorted(symmetryGroup({MirrorLeftRight, MirrorTopBottom})) == std::vector<int>{0, 1, 2, 3});
    CHECK(sorted(symmetryGroup({MirrorRising, MirrorFalling})) == std::vector<int>{0, 3, 4, 7});
    CHECK(symmetryGroup({MirrorLeftRight, MirrorRising}).size() == 8);
    CHECK(symmetryGroup({MirrorLeftRight, MirrorTopBottom, MirrorRising, MirrorFalling}).front() == 0);
}

TEST_CASE("mirror rect: same as the old center formulas, off center, half pixels, diagonals")
{
    // 16 wide canvas, center on the pixel edge at 8 (center2 = 16): x -> 15 - x.
    CHECK(mirrorRect({3, 4, 1, 1}, MirrorLeftRight, {16, 16}) == Rect{12, 4, 1, 1});
    // A 2 x 2 brush at 0..1 mirrors onto 14..15.
    CHECK(mirrorRect({0, 0, 2, 2}, MirrorLeftRight, {16, 16}) == Rect{14, 0, 2, 2});
    // 15 wide (center2 = 15, through pixel 7's middle): 3 -> 11, 7 stays.
    CHECK(mirrorRect({3, 0, 1, 1}, MirrorLeftRight, {15, 15}) == Rect{11, 0, 1, 1});
    CHECK(mirrorRect({7, 0, 1, 1}, MirrorLeftRight, {15, 15}) == Rect{7, 0, 1, 1});
    // Moved axis at x = 5 (center2.x = 10): 2 -> 7.
    CHECK(mirrorRect({2, 1, 1, 1}, MirrorLeftRight, {10, 0}) == Rect{7, 1, 1, 1});
    CHECK(mirrorRect({2, 1, 1, 1}, MirrorTopBottom, {10, 6}) == Rect{2, 4, 1, 1}); // axis at y = 3

    // "\" through (8, 8): (3, 5) -> (5, 3); a 3 x 1 row becomes a 1 x 3 column.
    CHECK(mirrorRect({3, 5, 1, 1}, MirrorFalling, {16, 16}) == Rect{5, 3, 1, 1});
    CHECK(mirrorRect({2, 5, 3, 1}, MirrorFalling, {16, 16}) == Rect{5, 2, 1, 3});
    // "/" through (8, 8): pixel (3, 5) -> (10, 12) (pixel centers mirror onto pixel centers).
    CHECK(mirrorRect({3, 5, 1, 1}, MirrorRising, {16, 16}) == Rect{10, 12, 1, 1});
    // Mirroring twice gives the rect back.
    for (const int m : {1, 2, 4, 7}) {
        const Rect r{3, 1, 4, 2};
        CHECK(mirrorRect(mirrorRect(r, m, {11, 9}), m, {11, 9}) == r);
    }
}

TEST_CASE("turned mask: a quarter turn round the center, optionally mirrored first")
{
    const Mask pixel(Rect{3, 5, 1, 1});
    const double quarter = 3.14159265358979323846 / 2;
    // Center (8, 8): the pixel's middle (-4.5, -2.5) from it goes clockwise to (2.5, -4.5).
    const Mask turned = turnedMask(pixel, quarter, false, {16, 16});
    CHECK(turned.bounds() == Rect{10, 3, 1, 1});
    const Mask both = turnedMask(pixel, quarter, true, {16, 16}); // mirrored: (4.5, -2.5) -> (2.5, 4.5)
    CHECK(both.bounds() == Rect{10, 12, 1, 1});
    CHECK(turnedMask(pixel, 0, false, {16, 16}) == pixel);
    // A bar keeps its pixel count through quarter turns.
    const Mask bar(Rect{2, 2, 5, 2});
    CHECK(turnedMask(bar, quarter, false, {16, 16}).bounds() == Rect{12, 2, 2, 5});
    CHECK(turnedMask(Mask(), quarter, false, {16, 16}).isEmpty());
}

TEST_CASE("oriented image: flips and diagonal turns of a brush")
{
    Image img(3, 2, PixelFormat::Indexed8); // 1 2 3 / 4 5 6
    for (int i = 0; i < 6; ++i)
        img.setPixel(i % 3, i / 3, uint32_t(i + 1));
    const Image lr = orientedImage(img, MirrorLeftRight);
    CHECK(lr.pixel(0, 0) == 3);
    CHECK(lr.pixel(2, 1) == 4);
    const Image fall = orientedImage(img, MirrorFalling); // transpose: 1 4 / 2 5 / 3 6
    REQUIRE(fall.width() == 2);
    REQUIRE(fall.height() == 3);
    CHECK(fall.pixel(1, 0) == 4);
    CHECK(fall.pixel(0, 2) == 3);
    const Image rise = orientedImage(img, MirrorRising); // 6 3 / 5 2 / 4 1
    CHECK(rise.pixel(0, 0) == 6);
    CHECK(rise.pixel(1, 2) == 1);
    CHECK(orientedImage(img, MirrorNone) == img);
}
