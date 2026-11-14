#include "core/Smear.h"

#include "core/ColorConvert.h"
#include "core/Transform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;

double eased(SmearKey::Ease ease, double t, const EasingCurve& curve)
{
    switch (ease) {
    case SmearKey::Ease::Custom:
        return evalEasing(curve, t);
    case SmearKey::Ease::In:
        return t * t;
    case SmearKey::Ease::Out:
        return 1 - (1 - t) * (1 - t);
    case SmearKey::Ease::InOut:
        return t * t * (3 - 2 * t);
    case SmearKey::Ease::Linear:
        break;
    }
    return t;
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// The pose as a map from the art as drawn to the canvas: q = pivot + move + size * M (p - pivot), M being
// the turn after the skew after the scale. `size` shrinks it toward the pivot (a smear's taper).
struct Affine {
    double a = 1, b = 0, c = 0, d = 1; // M times size: [a b; c d]
    double px = 0, py = 0;             // the pivot
    double mx = 0, my = 0;             // the move

    Affine(const SmearRecipe& r, const SmearPose& pose, double size)
    {
        const double turn = pose.rotation * kPi / 180.0;
        const double cs = std::cos(turn), sn = std::sin(turn);
        const double kx = std::tan(std::clamp(pose.skewX, -89.0, 89.0) * kPi / 180.0);
        const double ky = std::tan(std::clamp(pose.skewY, -89.0, 89.0) * kPi / 180.0);
        // K S = [sx, kx sy; ky sx, sy]; then R K S.
        const double k11 = pose.scaleX, k12 = kx * pose.scaleY, k21 = ky * pose.scaleX, k22 = pose.scaleY;
        a = (cs * k11 - sn * k21) * size;
        b = (cs * k12 - sn * k22) * size;
        c = (sn * k11 + cs * k21) * size;
        d = (sn * k12 + cs * k22) * size;
        px = r.pivotX;
        py = r.pivotY;
        mx = pose.x;
        my = pose.y;
    }
    void forward(double x, double y, double& qx, double& qy) const
    {
        const double dx = x - px, dy = y - py;
        qx = px + mx + a * dx + b * dy;
        qy = py + my + c * dx + d * dy;
    }
    bool inverse(double qx, double qy, double& x, double& y) const
    {
        const double det = a * d - b * c;
        if (std::abs(det) < 1e-9)
            return false;
        const double dx = qx - px - mx, dy = qy - py - my;
        x = px + (d * dx - b * dy) / det;
        y = py + (-c * dx + a * dy) / det;
        return true;
    }
    // A quarter turn or a flip at full size: every pixel lands on a pixel, so the art is copied exactly.
    bool exact() const
    {
        const auto whole = [](double v) { return std::abs(v - std::round(v)) < 1e-9; };
        const auto unit = [](double v) { return std::abs(std::abs(v) - 1) < 1e-9 || std::abs(v) < 1e-9; };
        return unit(a) && unit(b) && unit(c) && unit(d) && std::abs(a * d - b * c) > 0.5 && whole(mx) && whole(my);
    }
};

// The art cropped to what it shows, as it is and 8 times bigger by Scale2x three times (what a RotSprite
// turn samples, so lines stay clean when it turns, scales or skews).
struct Art {
    Rect box;
    Image plain;
    Image big;
    int factor = 8;
};

Art prepare(const Image& art)
{
    Art out;
    int x0 = art.width(), y0 = art.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < art.height(); ++y)
        for (int x = 0; x < art.width(); ++x)
            if (art.pixel(x, y) >> 24) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    if (x1 < 0)
        return out;
    out.box = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    out.plain = art.copy(out.box);
    if (int64_t(out.box.w) * out.box.h <= 128 * 128) {
        out.big = scale2x(scale2x(scale2x(out.plain)));
    } else { // too big to upscale: plain nearest neighbor
        out.big = out.plain;
        out.factor = 1;
    }
    return out;
}

// Draws the art at a pose: `put(x, y, value)` for each canvas pixel it covers (value: its RGBA there).
template <class Put>
void stampArt(const Art& art, const Affine& m, int width, int height, Put&& put)
{
    if (art.box.isEmpty())
        return;
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    const std::array<std::array<double, 2>, 4> corners{{{double(art.box.x), double(art.box.y)},
                                                       {double(art.box.x + art.box.w), double(art.box.y)},
                                                       {double(art.box.x), double(art.box.y + art.box.h)},
                                                       {double(art.box.x + art.box.w), double(art.box.y + art.box.h)}}};
    for (const auto& c : corners) {
        double qx, qy;
        m.forward(c[0], c[1], qx, qy);
        minX = std::min(minX, qx);
        minY = std::min(minY, qy);
        maxX = std::max(maxX, qx);
        maxY = std::max(maxY, qy);
    }
    const int left = std::max(0, int(std::floor(minX)) - 1), top = std::max(0, int(std::floor(minY)) - 1);
    const int right = std::min(width - 1, int(std::ceil(maxX)) + 1), bottom = std::min(height - 1, int(std::ceil(maxY)) + 1);
    const bool exact = m.exact();
    const Image& source = exact ? art.plain : art.big;
    const int factor = exact ? 1 : art.factor;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            double sx, sy;
            if (!m.inverse(x + 0.5, y + 0.5, sx, sy))
                return;
            const int u = int(std::floor((sx - art.box.x) * factor)), v = int(std::floor((sy - art.box.y) * factor));
            if (u < 0 || v < 0 || u >= source.width() || v >= source.height())
                continue;
            const uint32_t value = source.pixel(u, v);
            if (value >> 24)
                put(x, y, value);
        }
}

// `top` over `under`, straight alpha.
uint32_t over(uint32_t top, uint32_t under)
{
    const Rgba s = unpack(top), d = unpack(under);
    if (s.a == 255 || d.a == 0)
        return top;
    if (s.a == 0)
        return under;
    const double sa = s.a / 255.0, da = d.a / 255.0, oa = sa + da * (1 - sa);
    const auto channel = [&](uint8_t sc, uint8_t dc) { return uint8_t(std::lround((sc * sa + dc * da * (1 - sa)) / oa)); };
    return pack({channel(s.r, d.r), channel(s.g, d.g), channel(s.b, d.b), uint8_t(std::lround(oa * 255))});
}

} // namespace

const std::vector<SmearPreset>& smearPresets()
{
    static const std::vector<SmearPreset> presets = {
        {"swing", "Swing", "A wind-up, a fast swing and a follow-through about the bottom of the art (the grip of something drawn pointing up, like a sword).", 10},
        {"spin", "Spin", "A whole turn about the middle, fading toward the tail.", 7},
        {"dash", "Dash", "A quick move to the right, about one and a half times the art's size, squashing as it goes.", 4},
        {"pop", "Pop", "Grows in from small, a little too big, then settles.", 5},
        {"slash", "Slash", "An upward slash about the bottom of the art: a short wind-up, then fast up and across.", 8},
        {"thrust", "Thrust", "Pulled back, then stabbed forward to the right, and eased back a little.", 8},
        {"throw", "Throw", "Thrown in an arc to the right, turning to face where it flies (art drawn upright points ahead).", 13},
        {"bounce", "Bounce", "Dropped from above, landing with a squash, and a smaller second bounce.", 11},
        {"shake", "Shake", "A quick shake side to side, dying down.", 9},
        {"wobble", "Wobble", "Rocks to and fro about its bottom, dying down.", 9},
    };
    return presets;
}

const SmearPreset* findSmearPreset(std::string_view id)
{
    for (const SmearPreset& p : smearPresets())
        if (p.id == id)
            return &p;
    return nullptr;
}

SmearRecipe smearPresetRecipe(std::string_view id, Rect art, int start, const SmearRecipe& base)
{
    SmearRecipe r = base;
    const auto key = [&](int frame, SmearKey::Ease ease, auto&& set) {
        SmearKey k;
        k.frame = start + frame;
        k.ease = ease;
        set(k.pose);
        return k;
    };
    const double middleX = art.x + art.w / 2.0, middleY = art.y + art.h / 2.0;
    // The settings every preset starts from (its colors are the group's).
    r.trail = 1;
    r.behind = true;
    r.style = SmearRecipe::Style::Streak;
    r.taper = 0;
    r.fade = 0;
    r.fadePattern = 2;
    r.outline = false;
    using E = SmearKey::Ease;
    if (id == "swing") {
        r.pivotX = middleX;
        r.pivotY = art.y + art.h; // the bottom: the grip
        r.taper = 0.3;
        r.keys = {key(0, E::Out, [](SmearPose&) {}),
                  key(3, E::In, [](SmearPose& p) { p.rotation = -40; }),
                  key(6, E::Out, [](SmearPose& p) { p.rotation = 150; }),
                  key(9, E::Linear, [](SmearPose& p) { p.rotation = 165; })};
    } else if (id == "spin") {
        r.pivotX = middleX;
        r.pivotY = middleY;
        r.trail = 0.8;
        r.fade = 0.6;
        r.keys = {key(0, E::Linear, [](SmearPose&) {}), key(6, E::Linear, [](SmearPose& p) { p.rotation = 360; })};
    } else if (id == "dash") {
        r.pivotX = middleX;
        r.pivotY = art.y + art.h;
        r.fade = 0.5;
        const double reach = std::round(1.5 * std::max({1, art.w, art.h}));
        r.keys = {key(0, E::Out, [](SmearPose&) {}),
                  key(1, E::Out, [&](SmearPose& p) { p.x = std::round(reach * 0.55); p.scaleX = 1.3; p.scaleY = 0.8; }),
                  key(3, E::Linear, [&](SmearPose& p) { p.x = reach; })};
    } else if (id == "pop") {
        r.pivotX = middleX;
        r.pivotY = middleY;
        r.trail = 0.6;
        r.fade = 0.5;
        r.keys = {key(0, E::Out, [](SmearPose& p) { p.scaleX = p.scaleY = 0.2; }),
                  key(2, E::InOut, [](SmearPose& p) { p.scaleX = p.scaleY = 1.2; }),
                  key(4, E::Linear, [](SmearPose&) {})};
    } else if (id == "slash") {
        r.pivotX = middleX;
        r.pivotY = art.y + art.h;
        r.taper = 0.3;
        r.keys = {key(0, E::Out, [](SmearPose& p) { p.rotation = 110; }),
                  key(2, E::In, [](SmearPose& p) { p.rotation = 125; }),
                  key(4, E::Out, [](SmearPose& p) { p.rotation = -40; }),
                  key(7, E::Linear, [](SmearPose& p) { p.rotation = -50; })};
    } else if (id == "thrust") {
        const double size = std::max({1, art.w, art.h});
        r.pivotX = middleX;
        r.pivotY = middleY;
        r.fade = 0.4;
        r.keys = {key(0, E::Out, [](SmearPose&) {}),
                  key(2, E::In, [&](SmearPose& p) { p.x = std::round(-0.25 * size); }),
                  key(4, E::Out, [&](SmearPose& p) { p.x = std::round(size); }),
                  key(7, E::Linear, [&](SmearPose& p) { p.x = std::round(0.8 * size); })};
    } else if (id == "throw") {
        const double size = std::max({1, art.w, art.h});
        const double upright = art.h > art.w ? 90 : 0; // drawn pointing up: a quarter turn points it ahead
        r.pivotX = middleX;
        r.pivotY = middleY;
        r.smoothPath = true;
        r.facePath = true;
        r.fade = 0.5;
        r.keys = {key(0, E::Linear, [&](SmearPose& p) { p.rotation = upright; }),
                  key(6, E::Linear, [&](SmearPose& p) { p.x = std::round(2 * size); p.y = std::round(-size); p.rotation = upright; }),
                  key(12, E::Linear, [&](SmearPose& p) { p.x = std::round(4 * size); p.rotation = upright; })};
    } else if (id == "bounce") {
        const double size = std::max({1, art.w, art.h});
        r.pivotX = middleX;
        r.pivotY = art.y + art.h; // squashes onto its bottom
        r.trail = 0.5;
        r.keys = {key(0, E::In, [&](SmearPose& p) { p.y = std::round(-size); }),
                  key(4, E::Out, [](SmearPose& p) { p.scaleX = 1.3; p.scaleY = 0.7; }),
                  key(5, E::Out, [](SmearPose&) {}),
                  key(7, E::In, [&](SmearPose& p) { p.y = std::round(-0.4 * size); }),
                  key(9, E::Out, [](SmearPose& p) { p.scaleX = 1.15; p.scaleY = 0.85; }),
                  key(10, E::Linear, [](SmearPose&) {})};
    } else if (id == "shake") {
        const double size = std::max({1, art.w, art.h});
        r.pivotX = middleX;
        r.pivotY = middleY;
        r.trail = 0.4;
        const double amounts[] = {0, 0.12, -0.12, 0.09, -0.09, 0.06, -0.06, 0.03, 0};
        r.keys.clear();
        for (int i = 0; i < 9; ++i)
            r.keys.push_back(key(i, E::Linear, [&](SmearPose& p) { p.x = std::round(amounts[i] * size); }));
    } else if (id == "wobble") {
        r.pivotX = middleX;
        r.pivotY = art.y + art.h;
        r.trail = 0.5;
        const double angles[] = {0, 14, -12, 9, -7, 5, -3, 1, 0};
        r.keys.clear();
        for (int i = 0; i < 9; ++i)
            r.keys.push_back(key(i, E::InOut, [&](SmearPose& p) { p.rotation = angles[i]; }));
    } else {
        return base;
    }
    return r;
}

SmearRecipe relativeSmear(const SmearRecipe& recipe, Rect art)
{
    SmearRecipe r = recipe;
    const double size = std::max({1, art.w, art.h});
    const int first = r.keys.empty() ? 0 : r.keys.front().frame;
    for (SmearKey& k : r.keys) {
        k.frame -= first;
        k.pose.x /= size;
        k.pose.y /= size;
    }
    r.pivotX = (r.pivotX - art.x) / std::max(1, art.w);
    r.pivotY = (r.pivotY - art.y) / std::max(1, art.h);
    return r;
}

SmearRecipe placedSmear(const SmearRecipe& relative, Rect art, int start, const SmearRecipe& base)
{
    SmearRecipe r = relative;
    const double size = std::max({1, art.w, art.h});
    for (SmearKey& k : r.keys) {
        k.frame += start;
        k.pose.x = std::round(k.pose.x * size);
        k.pose.y = std::round(k.pose.y * size);
    }
    r.pivotX = art.x + relative.pivotX * std::max(1, art.w);
    r.pivotY = art.y + relative.pivotY * std::max(1, art.h);
    r.color = base.color;
    r.outlineColor = base.outlineColor;
    return r;
}

void smearMapPoint(const SmearRecipe& recipe, const SmearPose& pose, double x, double y, double& qx, double& qy)
{
    Affine(recipe, pose, 1).forward(x, y, qx, qy);
}

SmearRecipe movedSmear(const SmearRecipe& recipe, double scaleX, double scaleY, double dx, double dy)
{
    SmearRecipe r = recipe;
    r.pivotX = r.pivotX * scaleX + dx;
    r.pivotY = r.pivotY * scaleY + dy;
    for (SmearKey& k : r.keys) {
        k.pose.x *= scaleX;
        k.pose.y *= scaleY;
    }
    return r;
}

namespace {

// The pose from the keys alone: tweened, the move along straight lines or the smooth path; not yet
// facing the path.
SmearPose tweened(const SmearRecipe& r, double time)
{
    if (r.keys.empty())
        return {};
    if (time <= r.keys.front().frame)
        return r.keys.front().pose;
    if (time >= r.keys.back().frame)
        return r.keys.back().pose;
    size_t k = 0;
    while (k + 1 < r.keys.size() && r.keys[k + 1].frame <= time)
        ++k;
    const SmearKey &from = r.keys[k], &to = r.keys[k + 1];
    const double t = eased(from.ease, (time - from.frame) / double(std::max(1, to.frame - from.frame)), from.curve);
    const SmearPose &a = from.pose, &b = to.pose;
    SmearPose out{lerp(a.x, b.x, t),           lerp(a.y, b.y, t),           lerp(a.rotation, b.rotation, t),
                  lerp(a.scaleX, b.scaleX, t), lerp(a.scaleY, b.scaleY, t), lerp(a.skewX, b.skewX, t),
                  lerp(a.skewY, b.skewY, t)};
    if (r.smoothPath && r.keys.size() > 2) {
        // Catmull-Rom through the keys' places (the ends doubled), so it passes through every key.
        const SmearPose& p0 = r.keys[k > 0 ? k - 1 : k].pose;
        const SmearPose& p3 = r.keys[k + 2 < r.keys.size() ? k + 2 : k + 1].pose;
        const auto curve = [&](double v0, double v1, double v2, double v3) {
            return 0.5 * (2 * v1 + (-v0 + v2) * t + (2 * v0 - 5 * v1 + 4 * v2 - v3) * t * t
                          + (-v0 + 3 * v1 - 3 * v2 + v3) * t * t * t);
        };
        out.x = curve(p0.x, a.x, b.x, p3.x);
        out.y = curve(p0.y, a.y, b.y, p3.y);
    }
    return out;
}

// Which way the move goes at `time` (radians), or nothing where it stands still. Looked at over a little
// time around it, kept within the keys (so a held pose keeps the way it ended).
std::optional<double> heading(const SmearRecipe& r, double time)
{
    if (r.keys.size() < 2)
        return std::nullopt;
    const double first = r.keys.front().frame, last = r.keys.back().frame;
    time = std::clamp(time, first, last); // held before the first key and after the last: the way it began / ended
    for (const double reach : {0.25, 1.0, 3.0}) {
        const double from = std::clamp(time - reach, first, last), to = std::clamp(time + reach, first, last);
        if (to - from < 1e-6)
            continue;
        const SmearPose a = tweened(r, from), b = tweened(r, to);
        if (std::hypot(b.x - a.x, b.y - a.y) > 1e-3)
            return std::atan2(b.y - a.y, b.x - a.x);
    }
    return std::nullopt;
}

} // namespace

SmearPose smearPoseAt(const SmearRecipe& r, double time)
{
    SmearPose pose = tweened(r, time);
    if (r.facePath)
        if (const auto way = heading(r, time))
            pose.rotation += *way * 180.0 / kPi;
    return pose;
}

Image renderSmear(const SmearRecipe& r, const Image& source, int frame)
{
    const int width = source.width(), height = source.height();
    Image out(width, height);
    out.clear(0);
    const Art art = prepare(source);
    if (art.box.isEmpty())
        return out;
    const SmearPose now = smearPoseAt(r, frame);

    // The smear: the art stamped at in-between moments from `trail` of a frame ago up to now, oldest first,
    // each pixel remembering how old the newest stamp on it is (0 now, 1 the tail).
    Image smear(width, height);
    smear.clear(0);
    std::vector<float> age(size_t(width) * size_t(height), -1.0f);
    const double trail = std::clamp(r.trail, 0.0, 1.0);
    if (trail > 0 && !(smearPoseAt(r, frame - trail) == now)) {
        // Enough stamps that no corner of the art moves more than about a pixel from one to the next.
        double travel = 0;
        const std::array<std::array<double, 2>, 4> corners{{{double(art.box.x), double(art.box.y)},
                                                           {double(art.box.x + art.box.w), double(art.box.y)},
                                                           {double(art.box.x), double(art.box.y + art.box.h)},
                                                           {double(art.box.x + art.box.w), double(art.box.y + art.box.h)}}};
        constexpr int kParts = 16;
        for (int i = 0; i < kParts; ++i) {
            const Affine m0(r, smearPoseAt(r, frame - trail + trail * i / kParts), 1);
            const Affine m1(r, smearPoseAt(r, frame - trail + trail * (i + 1) / kParts), 1);
            double most = 0;
            for (const auto& c : corners) {
                double x0, y0, x1, y1;
                m0.forward(c[0], c[1], x0, y0);
                m1.forward(c[0], c[1], x1, y1);
                most = std::max(most, std::hypot(x1 - x0, y1 - y0));
            }
            travel += most;
        }
        const int stamps = std::clamp(int(std::ceil(travel)) + 1, 2, 512);
        const uint32_t flat = pack(r.color);
        for (int i = 0; i < stamps; ++i) { // the last, now, is the art itself
            const double tail = 1.0 - double(i) / stamps;
            const Affine m(r, smearPoseAt(r, frame - trail * tail), 1.0 - std::clamp(r.taper, 0.0, 1.0) * tail);
            stampArt(art, m, width, height, [&](int x, int y, uint32_t value) {
                smear.setPixel(x, y, r.style == SmearRecipe::Style::Flat ? flat : value);
                age[size_t(y) * size_t(width) + size_t(x)] = float(tail);
            });
        }
        // The fade toward the tail.
        const double fade = std::clamp(r.fade, 0.0, 1.0);
        if (fade > 0) {
            const IndexedDither pattern = r.fadePattern == 1 ? IndexedDither::Bayer2
                : r.fadePattern == 2 ? IndexedDither::Ordered
                : r.fadePattern == 3 ? IndexedDither::Bayer8 : IndexedDither::BlueNoise;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const float a = age[size_t(y) * size_t(width) + size_t(x)];
                    if (a < 0)
                        continue;
                    const double opacity = 1.0 - fade * a;
                    if (r.fadePattern == 0) {
                        Rgba c = unpack(smear.pixel(x, y));
                        c.a = uint8_t(std::lround(c.a * opacity));
                        smear.setPixel(x, y, pack(c));
                    } else if (!(opacity > double(ditherThreshold(pattern, x, y)))) {
                        smear.setPixel(x, y, 0);
                    }
                }
        }
        // A pixel wide outline around what is left of it.
        if (r.outline) {
            const uint32_t edge = pack(r.outlineColor);
            const auto shown = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < width && y < height && (smear.pixel(x, y) >> 24) != 0;
            };
            std::vector<std::array<int, 2>> ring;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (!shown(x, y) && (shown(x - 1, y) || shown(x + 1, y) || shown(x, y - 1) || shown(x, y + 1)))
                        ring.push_back({x, y});
            for (const auto& p : ring)
                smear.setPixel(p[0], p[1], edge);
        }
    }

    // The art where it is now, with the smear under or over it.
    Image posed(width, height);
    posed.clear(0);
    stampArt(art, Affine(r, now, 1), width, height, [&](int x, int y, uint32_t value) { posed.setPixel(x, y, value); });
    const Image& lower = r.behind ? smear : posed;
    const Image& upper = r.behind ? posed : smear;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            out.setPixel(x, y, over(upper.pixel(x, y), lower.pixel(x, y)));
    return out;
}

} // namespace core
