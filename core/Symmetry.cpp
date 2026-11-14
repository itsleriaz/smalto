#include "core/Symmetry.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

int floorDiv2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

} // namespace

Point applySymmetry(int element, Point v)
{
    if (element & 4)
        v = {v.y, v.x};
    if (element & 1)
        v.x = -v.x;
    if (element & 2)
        v.y = -v.y;
    return v;
}

int composeSymmetry(int a, int b)
{
    // (1, 2) tells all eight apart.
    const Point probe = applySymmetry(a, applySymmetry(b, {1, 2}));
    for (int e = 0; e < 8; ++e)
        if (applySymmetry(e, {1, 2}) == probe)
            return e;
    return 0;
}

std::vector<int> symmetryGroup(const std::vector<int>& generators)
{
    std::vector<int> group{MirrorNone};
    for (size_t i = 0; i < group.size(); ++i)
        for (const int g : generators) {
            const int e = composeSymmetry(g, group[i]);
            if (std::find(group.begin(), group.end(), e) == group.end())
                group.push_back(e);
        }
    return group;
}

Rect mirrorRect(Rect r, int element, Point center2)
{
    // Corners in half pixels, relative to the center.
    const Point a = applySymmetry(element, {2 * r.x - center2.x, 2 * r.y - center2.y});
    const Point b = applySymmetry(element, {2 * r.right() - center2.x, 2 * r.bottom() - center2.y});
    const bool swap = element & 4;
    return {floorDiv2(std::min(a.x, b.x) + center2.x), floorDiv2(std::min(a.y, b.y) + center2.y), swap ? r.h : r.w,
            swap ? r.w : r.h};
}

Image orientedImage(const Image& image, int element)
{
    if (element == MirrorNone)
        return image;
    const int w = image.width(), h = image.height();
    const bool swap = element & 4;
    const int nw = swap ? h : w, nh = swap ? w : h;
    Image out(nw, nh, image.format());
    for (int v = 0; v < h; ++v)
        for (int u = 0; u < w; ++u) {
            // Pixel centers in half pixels, relative to the image's center.
            const Point c = applySymmetry(element, {2 * u + 1 - w, 2 * v + 1 - h});
            out.setPixel((c.x + nw - 1) / 2, (c.y + nh - 1) / 2, image.pixel(u, v));
        }
    return out;
}

Mask mirroredMask(const Mask& mask, int element, Point center2)
{
    if (mask.isEmpty() || element == MirrorNone)
        return mask;
    const Rect target = mirrorRect(mask.bounds(), element, center2);
    return Mask::fromImage(orientedImage(mask.toImage(), element), {target.x, target.y});
}

Mask turnedMask(const Mask& mask, double radians, bool mirror, Point center2)
{
    if (mask.isEmpty())
        return mask;
    const double cx = center2.x / 2.0, cy = center2.y / 2.0;
    const double c = std::cos(radians), s = std::sin(radians);
    const Rect b = mask.bounds();
    // The bounds of the source's corners after the same mapping.
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const Point corner : {Point{b.x, b.y}, Point{b.right(), b.y}, Point{b.x, b.bottom()}, Point{b.right(), b.bottom()}}) {
        const double gx = (mirror ? -(corner.x - cx) : corner.x - cx), gy = corner.y - cy;
        const double x = cx + gx * c - gy * s, y = cy + gx * s + gy * c;
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    const Rect target{int(std::floor(minX)), int(std::floor(minY)), int(std::ceil(maxX)) - int(std::floor(minX)),
                      int(std::ceil(maxY)) - int(std::floor(minY))};
    if (target.isEmpty())
        return {};
    Image out(target.w, target.h, PixelFormat::Indexed8);
    out.clear(0);
    for (int y = 0; y < target.h; ++y)
        for (int x = 0; x < target.w; ++x) {
            // Back from the pixel's middle to the source.
            const double gx = target.x + x + 0.5 - cx, gy = target.y + y + 0.5 - cy;
            double ux = gx * c + gy * s;
            const double uy = -gx * s + gy * c;
            if (mirror)
                ux = -ux;
            if (mask.contains(int(std::floor(cx + ux)), int(std::floor(cy + uy))))
                out.setPixel(x, y, 1);
        }
    return Mask::fromImage(out, {target.x, target.y});
}

} // namespace core
