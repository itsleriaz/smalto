#include "core/Mask.h"

#include <algorithm>

namespace core {

Mask::Mask(Rect rect)
{
    if (rect.isEmpty())
        return;
    m_bounds = rect;
    m_bits.assign(size_t(rect.w) * rect.h, 1);
}

bool Mask::contains(int x, int y) const
{
    if (!m_bounds.contains(x, y))
        return false;
    return m_bits[size_t(y - m_bounds.y) * m_bounds.w + (x - m_bounds.x)] != 0;
}

bool Mask::isRect() const
{
    return !isEmpty() && std::all_of(m_bits.begin(), m_bits.end(), [](uint8_t b) { return b != 0; });
}

// Builds a mask over `area` from a per-pixel predicate.
template <typename Fn>
static Mask build(Rect area, Fn fn)
{
    Image image(std::max(0, area.w), std::max(0, area.h), PixelFormat::Indexed8);
    for (int y = 0; y < area.h; ++y)
        for (int x = 0; x < area.w; ++x)
            if (fn(area.x + x, area.y + y))
                image.setPixel(x, y, 1);
    return Mask::fromImage(image, {area.x, area.y});
}

Mask Mask::united(const Mask& other) const
{
    if (other.isEmpty())
        return *this;
    if (isEmpty())
        return other;
    return build(m_bounds.united(other.m_bounds), [&](int x, int y) { return contains(x, y) || other.contains(x, y); });
}

Mask Mask::subtracted(const Mask& other) const
{
    if (isEmpty() || other.isEmpty() || m_bounds.intersected(other.m_bounds).isEmpty())
        return *this;
    return build(m_bounds, [&](int x, int y) { return contains(x, y) && !other.contains(x, y); });
}

Mask Mask::intersected(Rect rect) const
{
    const Rect area = m_bounds.intersected(rect);
    if (area == m_bounds)
        return *this;
    return build(area, [&](int x, int y) { return contains(x, y); });
}

Mask Mask::intersected(const Mask& other) const
{
    const Rect area = m_bounds.intersected(other.m_bounds);
    if (area.isEmpty())
        return {};
    return build(area, [&](int x, int y) { return contains(x, y) && other.contains(x, y); });
}

Mask Mask::inverted(Rect area) const
{
    return Mask(area).subtracted(*this);
}

Mask Mask::translated(int dx, int dy) const
{
    Mask out = *this;
    out.m_bounds = m_bounds.translated(dx, dy);
    return out;
}

Image Mask::toImage() const
{
    Image image(m_bounds.w, m_bounds.h, PixelFormat::Indexed8);
    for (int y = 0; y < m_bounds.h; ++y)
        for (int x = 0; x < m_bounds.w; ++x)
            image.setPixel(x, y, m_bits[size_t(y) * m_bounds.w + x]);
    return image;
}

Mask Mask::fromImage(const Image& image, Point at)
{
    Mask mask;
    if (image.isNull())
        return mask;
    mask.m_bounds = {at.x, at.y, image.width(), image.height()};
    mask.m_bits.resize(size_t(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            mask.m_bits[size_t(y) * image.width() + x] = image.pixel(x, y) != 0 ? 1 : 0;
    mask.normalize();
    return mask;
}

void Mask::normalize()
{
    int l = m_bounds.w, t = m_bounds.h, r = -1, b = -1;
    for (int y = 0; y < m_bounds.h; ++y)
        for (int x = 0; x < m_bounds.w; ++x)
            if (m_bits[size_t(y) * m_bounds.w + x]) {
                l = std::min(l, x);
                r = std::max(r, x);
                t = std::min(t, y);
                b = std::max(b, y);
            }
    if (r < 0) {
        *this = {};
        return;
    }
    if (l == 0 && t == 0 && r == m_bounds.w - 1 && b == m_bounds.h - 1)
        return;
    const Rect tight{l, t, r - l + 1, b - t + 1};
    std::vector<uint8_t> bits(size_t(tight.w) * tight.h);
    for (int y = 0; y < tight.h; ++y)
        std::copy_n(m_bits.begin() + ptrdiff_t(size_t(tight.y + y) * m_bounds.w + tight.x), tight.w,
                    bits.begin() + ptrdiff_t(size_t(y) * tight.w));
    m_bounds = tight.translated(m_bounds.x, m_bounds.y);
    m_bits = std::move(bits);
}

} // namespace core
