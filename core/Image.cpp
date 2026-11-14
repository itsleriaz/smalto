#include "core/Image.h"

#include <cassert>
#include <cstring>

namespace core {

int bytesPerPixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Rgba8: return 4;
    case PixelFormat::Indexed8: return 1;
    case PixelFormat::Gray16: return 2;
    case PixelFormat::Tilemap32: return 4;
    }
    return 4;
}

Image::Image(int width, int height, PixelFormat format)
    : m_width(std::max(0, width))
    , m_height(std::max(0, height))
    , m_format(format)
    , m_data(size_t(m_width) * size_t(m_height) * size_t(bytesPerPixel(format)), 0)
{
}

uint32_t Image::pixel(int x, int y) const
{
    assert(bounds().contains(x, y));
    const uint8_t* p = row(y) + size_t(x) * bpp();
    switch (bpp()) {
    case 1:
        return *p;
    case 2: {
        uint16_t v;
        std::memcpy(&v, p, 2);
        return v;
    }
    default: {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
    }
}

void Image::setPixel(int x, int y, uint32_t value)
{
    assert(bounds().contains(x, y));
    uint8_t* p = row(y) + size_t(x) * bpp();
    switch (bpp()) {
    case 1:
        *p = uint8_t(value);
        break;
    case 2: {
        const uint16_t v = uint16_t(value);
        std::memcpy(p, &v, 2);
        break;
    }
    default:
        std::memcpy(p, &value, 4);
        break;
    }
}

void Image::fillRect(Rect rect, uint32_t value)
{
    rect = rect.intersected(bounds());
    if (rect.isEmpty())
        return;
    const size_t count = size_t(rect.w);
    for (int y = rect.y; y < rect.bottom(); ++y) {
        uint8_t* p = row(y) + size_t(rect.x) * bpp();
        switch (bpp()) {
        case 1:
            std::memset(p, int(uint8_t(value)), count);
            break;
        case 2: {
            const uint16_t v = uint16_t(value);
            for (size_t i = 0; i < count; ++i)
                std::memcpy(p + i * 2, &v, 2);
            break;
        }
        default: {
            if (value == 0) {
                std::memset(p, 0, count * 4);
                break;
            }
            for (size_t i = 0; i < count; ++i)
                std::memcpy(p + i * 4, &value, 4);
            break;
        }
        }
    }
}

Image Image::copy(Rect rect) const
{
    rect = rect.intersected(bounds());
    Image out(rect.w, rect.h, m_format);
    const size_t bytes = size_t(rect.w) * bpp();
    for (int y = 0; y < rect.h; ++y)
        std::memcpy(out.row(y), row(rect.y + y) + size_t(rect.x) * bpp(), bytes);
    return out;
}

void Image::blit(const Image& src, Point at)
{
    assert(src.format() == m_format);
    const Rect dst = Rect{at.x, at.y, src.width(), src.height()}.intersected(bounds());
    if (dst.isEmpty())
        return;
    const size_t bytes = size_t(dst.w) * bpp();
    const int sx = dst.x - at.x;
    const int sy = dst.y - at.y;
    for (int y = 0; y < dst.h; ++y)
        std::memcpy(row(dst.y + y) + size_t(dst.x) * bpp(), src.row(sy + y) + size_t(sx) * bpp(), bytes);
}

} // namespace core
