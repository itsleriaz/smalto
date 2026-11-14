#pragma once

#include <algorithm>

namespace core {

struct Point {
    int x = 0;
    int y = 0;

    bool operator==(const Point&) const = default;
};

// Integer rectangle. right() and bottom() are exclusive.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool isEmpty() const { return w <= 0 || h <= 0; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }
    bool contains(Point p) const { return contains(p.x, p.y); }

    Rect united(const Rect& o) const
    {
        if (isEmpty())
            return o;
        if (o.isEmpty())
            return *this;
        const int l = std::min(x, o.x);
        const int t = std::min(y, o.y);
        const int r = std::max(right(), o.right());
        const int b = std::max(bottom(), o.bottom());
        return {l, t, r - l, b - t};
    }

    Rect intersected(const Rect& o) const
    {
        const int l = std::max(x, o.x);
        const int t = std::max(y, o.y);
        const int r = std::min(right(), o.right());
        const int b = std::min(bottom(), o.bottom());
        if (r <= l || b <= t)
            return {};
        return {l, t, r - l, b - t};
    }

    Rect translated(int dx, int dy) const { return {x + dx, y + dy, w, h}; }

    bool operator==(const Rect&) const = default;
};

} // namespace core
