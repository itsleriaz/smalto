#include <doctest/doctest.h>

#include "core/Transform.h"

using namespace core;

namespace {

// 3x2 image with distinct values 1..6, row by row.
Image numbered()
{
    Image img(3, 2, PixelFormat::Indexed8);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            img.setPixel(x, y, uint32_t(1 + y * 3 + x));
    return img;
}

std::vector<uint32_t> values(const Image& img)
{
    std::vector<uint32_t> out;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            out.push_back(img.pixel(x, y));
    return out;
}

} // namespace

TEST_CASE("transform: flips and quarter turns")
{
    const Image img = numbered(); // 1 2 3 / 4 5 6
    CHECK(values(flippedHorizontally(img)) == std::vector<uint32_t>{3, 2, 1, 6, 5, 4});
    CHECK(values(flippedVertically(img)) == std::vector<uint32_t>{4, 5, 6, 1, 2, 3});
    const Image cw = rotated90(img, true); // 2x3: 4 1 / 5 2 / 6 3
    CHECK(cw.width() == 2);
    CHECK(values(cw) == std::vector<uint32_t>{4, 1, 5, 2, 6, 3});
    CHECK(values(rotated90(img, false)) == std::vector<uint32_t>{3, 6, 2, 5, 1, 4});
    CHECK(rotated90(rotated90(cw, false), true) == cw);

    CHECK(rotated(img, 90, 0) == cw);
    CHECK(rotated(img, -270, 0) == cw);
    CHECK(rotated(img, 360, 0) == img);
    CHECK(values(rotated(img, 180, 0)) == std::vector<uint32_t>{6, 5, 4, 3, 2, 1});
}

TEST_CASE("transform: nearest scaling")
{
    const Image img = numbered();
    const Image big = scaledNearest(img, 6, 4);
    CHECK(big.pixel(0, 0) == 1);
    CHECK(big.pixel(1, 1) == 1);
    CHECK(big.pixel(5, 3) == 6);
    CHECK(scaledNearest(big, 3, 2) == img);
    CHECK(scaledNearest(img, 0, 0).width() == 1);
}

TEST_CASE("transform: scale2x smooths a diagonal step")
{
    // 0 1 / 1 1: the empty corner's inner quarter stays, the step rounds.
    Image img(2, 2, PixelFormat::Indexed8);
    img.setPixel(1, 0, 1);
    img.setPixel(0, 1, 1);
    img.setPixel(1, 1, 1);
    const Image out = scale2x(img);
    CHECK(out.width() == 4);
    CHECK(out.pixel(0, 0) == 0);
    CHECK(out.pixel(1, 1) == 1); // filled by the rule
    CHECK(out.pixel(3, 3) == 1);
}

TEST_CASE("transform: free rotation keeps colors and grows the box")
{
    Image square(8, 8, PixelFormat::Indexed8);
    square.fillRect({2, 2, 4, 4}, 7);
    int w, h;
    rotatedSize(8, 8, 45, w, h);
    CHECK(w == 12);
    CHECK(h == 12);
    const Image out = rotated(square, 45, 0);
    CHECK(out.width() == 12);
    int filled = 0;
    for (uint32_t v : values(out)) {
        CHECK((v == 0 || v == 7)); // no new colors, no blending
        filled += v == 7;
    }
    // Roughly the same area as the 4x4 square.
    CHECK(filled >= 12);
    CHECK(filled <= 20);
    CHECK(out.pixel(6, 6) == 7); // the center stays filled
    CHECK(out.pixel(0, 0) == 0);
}
