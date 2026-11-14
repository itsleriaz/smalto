#include <doctest/doctest.h>

#include "core/Image.h"

using namespace core;

TEST_CASE("image stores raw pixels per format")
{
    Image rgba(4, 3);
    CHECK(rgba.stride() == 16);
    rgba.setPixel(2, 1, 0xAABBCCDD);
    CHECK(rgba.pixel(2, 1) == 0xAABBCCDD);
    CHECK(rgba.pixel(0, 0) == 0);

    Image indexed(4, 3, PixelFormat::Indexed8);
    indexed.setPixel(3, 2, 7);
    CHECK(indexed.pixel(3, 2) == 7);

    Image gray(2, 2, PixelFormat::Gray16);
    gray.setPixel(1, 1, 0xFF80);
    CHECK(gray.pixel(1, 1) == 0xFF80);
}

TEST_CASE("copy and blit clip to bounds")
{
    Image img(4, 4);
    img.fillRect({1, 1, 2, 2}, 5);

    Image part = img.copy({1, 1, 10, 10});
    CHECK(part.width() == 3);
    CHECK(part.height() == 3);
    CHECK(part.pixel(0, 0) == 5);
    CHECK(part.pixel(2, 2) == 0);

    Image dst(4, 4);
    dst.blit(part, {-1, -1});
    CHECK(dst.pixel(0, 0) == 5);
    CHECK(dst.pixel(1, 1) == 0);
    CHECK(dst.pixel(3, 3) == 0);
}
