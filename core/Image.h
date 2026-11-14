#pragma once

#include "core/Geometry.h"

#include <cstdint>
#include <vector>

namespace core {

enum class PixelFormat : uint8_t {
    Rgba8,     // 32-bit RGBA, straight alpha
    Indexed8,  // palette index
    Gray16,    // low byte value, high byte alpha (Aseprite grayscale)
    Tilemap32, // tile index + flip/rotate flags
};

int bytesPerPixel(PixelFormat format);

// Tightly packed pixel buffer. Pixels are read and written as raw values
// whose meaning depends on the format.
class Image {
public:
    Image() = default;
    Image(int width, int height, PixelFormat format = PixelFormat::Rgba8);

    int width() const { return m_width; }
    int height() const { return m_height; }
    PixelFormat format() const { return m_format; }
    int bpp() const { return bytesPerPixel(m_format); }
    int stride() const { return m_width * bpp(); }
    Rect bounds() const { return {0, 0, m_width, m_height}; }
    bool isNull() const { return m_width <= 0 || m_height <= 0; }

    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }
    uint8_t* row(int y) { return m_data.data() + size_t(y) * stride(); }
    const uint8_t* row(int y) const { return m_data.data() + size_t(y) * stride(); }

    uint32_t pixel(int x, int y) const;
    void setPixel(int x, int y, uint32_t value);

    void clear(uint32_t value = 0) { fillRect(bounds(), value); }
    void fillRect(Rect rect, uint32_t value);

    // Copy of the region (clipped to bounds).
    Image copy(Rect rect) const;
    // Raw copy of src (same format) with its top-left at `at`, clipped.
    void blit(const Image& src, Point at);

    bool operator==(const Image&) const = default;

private:
    int m_width = 0;
    int m_height = 0;
    PixelFormat m_format = PixelFormat::Rgba8;
    std::vector<uint8_t> m_data;
};

} // namespace core
