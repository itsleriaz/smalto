#include "core/Particles.h"
#include "core/ColorConvert.h"

#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Raster.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint32_t mix(uint32_t a)
{
    a ^= a >> 16;
    a *= 0x7feb352dU;
    a ^= a >> 15;
    a *= 0x846ca68bU;
    a ^= a >> 16;
    return a;
}

// A number in [0, 1) for the seed, the particle and the property (`channel`).
double random01(uint32_t seed, uint32_t index, uint32_t channel)
{
    return mix(seed * 0x9e3779b9U ^ mix(index * 0x85ebca6bU + channel * 0xc2b2ae35U + 0x27d4eb2fU)) / 4294967296.0;
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// Smooth noise in 0..1 over a lattice of values (x, y, t), the same for the same seed.
double noise3(uint32_t seed, double x, double y, double t)
{
    const double fx = std::floor(x), fy = std::floor(y), ft = std::floor(t);
    const auto ease = [](double v) { return v * v * (3 - 2 * v); };
    const double ux = ease(x - fx), uy = ease(y - fy), ut = ease(t - ft);
    const int ix = int(fx), iy = int(fy), it = int(ft);
    // The value at a lattice point is mix(seed ^ mix(x ^ mix(y ^ mix(t)))); the inner parts are shared
    // by the corners, so they are hashed once.
    const uint32_t s = seed * 0x9e3779b9U;
    const uint32_t x0 = uint32_t(ix) * 0x85ebca6bU, x1 = uint32_t(ix + 1) * 0x85ebca6bU;
    const uint32_t y0 = uint32_t(iy) * 0xc2b2ae35U, y1 = uint32_t(iy + 1) * 0xc2b2ae35U;
    const auto value = [&](uint32_t xs, uint32_t ys) { return mix(s ^ mix(xs ^ ys)) / 4294967296.0; };
    double along[2];
    for (int dt = 0; dt < 2; ++dt) {
        const uint32_t ts = mix(uint32_t(it + dt) + 0x27d4eb2fU);
        const uint32_t row0 = mix(y0 ^ ts), row1 = mix(y1 ^ ts);
        const double a = value(x0, row0) * (1 - ux) + value(x1, row0) * ux;
        const double b = value(x0, row1) * (1 - ux) + value(x1, row1) * ux;
        along[dt] = a * (1 - uy) + b * uy;
    }
    return along[0] * (1 - ut) + along[1] * ut;
}

// `t` (0..1) shaped by an easing (`curve` when it is Custom).
double eased(ParticleRecipe::Ease ease, double t, const EasingCurve& curve = {})
{
    switch (ease) {
    case ParticleRecipe::Ease::Custom:
        return evalEasing(curve, t);
    case ParticleRecipe::Ease::In:
        return t * t;
    case ParticleRecipe::Ease::Out:
        return 1 - (1 - t) * (1 - t);
    case ParticleRecipe::Ease::InOut:
        return t * t * (3 - 2 * t);
    case ParticleRecipe::Ease::Linear:
        break;
    }
    return t;
}

// Calls fn(i) for every i in [0, n), on several threads when there is enough work (`steps`, about
// how many particle steps it takes) to be worth starting them. fn must only write what belongs to i.
template <class Fn>
void parallelFor(int n, int64_t steps, Fn&& fn)
{
    constexpr int64_t kStepsPerThread = 20000; // below this a thread costs more than it saves
    const int hardware = int(std::max(1u, std::thread::hardware_concurrency()));
    const int threads = int(std::clamp<int64_t>(steps / kStepsPerThread, 1, std::min(hardware, std::max(1, n))));
    if (threads <= 1) {
        for (int i = 0; i < n; ++i)
            fn(i);
        return;
    }
    // Interleaved, so early and late particles (often shorter or longer lived) are shared out evenly.
    const auto work = [&](int t) {
        for (int i = t; i < n; i += threads)
            fn(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(size_t(threads - 1));
    for (int t = 1; t < threads; ++t)
        pool.emplace_back(work, t);
    work(0);
    for (std::thread& thread : pool)
        thread.join();
}

int floorMod(int a, int m) { return ((a % m) + m) % m; }
int floorDiv(int a, int m) { return (a - floorMod(a, m)) / m; }

Rgba colorAt(const std::vector<Rgba>& colors, double t)
{
    if (colors.empty())
        return {255, 255, 255, 255};
    if (colors.size() == 1)
        return colors[0];
    const double position = std::clamp(t, 0.0, 1.0) * double(colors.size() - 1);
    const size_t i = std::min(size_t(position), colors.size() - 2);
    const double f = position - double(i);
    const Rgba &a = colors[i], &b = colors[i + 1];
    auto channel = [&](uint8_t x, uint8_t y) { return uint8_t(std::lround(lerp(x, y, f))); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}

// Where a particle's pixels go: `contains` says whether a pixel can be drawn, `put` draws it. Plain is
// the frame itself.
struct PlainTarget {
    Image& image;
    bool contains(int x, int y) const { return image.bounds().contains(x, y); }
    void put(int x, int y, uint32_t value) { image.setPixel(x, y, value); }
};

// A dithered fade: each pixel of the shape shows where the pattern's threshold (by canvas position) is
// under the opacity. The shape is clipped to the box around it it was once drawn into first (`clip`), so
// the pictures don't change.
struct DitherTarget {
    Image& image;
    Rect clip;
    IndexedDither pattern;
    double opacity;
    bool contains(int x, int y) const { return clip.contains(x, y) && image.bounds().contains(x, y); }
    void put(int x, int y, uint32_t value)
    {
        if ((value >> 24) && opacity > double(ditherThreshold(pattern, x, y)))
            image.setPixel(x, y, value);
    }
};

// A built-in brush of `size` centered on (cx, cy), as stampBrush draws it.
template <class Target>
void stampSpans(Target& target, int cx, int cy, int size, uint32_t value, BrushShape shape)
{
    size = std::max(1, size);
    const Rect area = brushRect({cx, cy}, size);
    const bool square = shape == BrushShape::Square || size <= 2; // round 1 and 2 are squares too
    const auto& spans = brushSpans(size, square ? BrushShape::Square : shape);
    for (int i = 0; i < size; ++i)
        for (int x = area.x + spans[size_t(i)].first; x <= area.x + spans[size_t(i)].second; ++x)
            if (target.contains(x, area.y + i))
                target.put(x, area.y + i, value);
}

// The brush scaled (nearest neighbor) so its longest side is `size`, centered on (cx, cy).
template <class Target>
void stampParticleBrush(Target& image, int cx, int cy, int size, uint32_t value, const ParticleBrush& brush)
{
    const double scale = double(size) / double(std::max(brush.width, brush.height));
    const int w = std::max(1, int(std::lround(brush.width * scale)));
    const int h = std::max(1, int(std::lround(brush.height * scale)));
    const int left = cx - w / 2, top = cy - h / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // The source pixel under this one's middle, so a brush squeezed small still samples its middle.
            const int sx = std::min(brush.width - 1, (2 * x + 1) * brush.width / (2 * w));
            const int sy = std::min(brush.height - 1, (2 * y + 1) * brush.height / (2 * h));
            if ((brush.pixels[size_t(sy) * size_t(brush.width) + size_t(sx)] >> 24) >= 128
                && image.contains(left + x, top + y))
                image.put(left + x, top + y, value);
        }
}

// The shape turned by `angle` degrees about its middle: every pixel of the canvas around it asks the
// unturned shape what is under its center (nearest neighbor, so the pixels stay hard). `put` gets each
// pixel that is part of it with the brush pixel there (0 for the other shapes).
template <class Put>
void rasterTurned(const ParticleRecipe& r, int cx, int cy, int size, double angle, bool ownColors, Put&& put)
{
    const bool brush = r.shape == ParticleRecipe::Shape::Brush && !r.brush.empty();
    int w = size, h = size;
    if (brush) {
        const double scale = double(size) / double(std::max(r.brush.width, r.brush.height));
        w = std::max(1, int(std::lround(r.brush.width * scale)));
        h = std::max(1, int(std::lround(r.brush.height * scale)));
    }
    const double centerX = double(cx - w / 2) + w / 2.0;
    const double centerY = double(cy - h / 2) + h / 2.0;
    const double radians = angle * kPi / 180.0;
    const double c = std::cos(radians), s = std::sin(radians);
    const int reach = int(std::ceil(std::hypot(double(w), double(h)) / 2.0)) + 1;
    const int half = size / 2;
    for (int y = int(std::floor(centerY)) - reach; y <= int(std::floor(centerY)) + reach; ++y)
        for (int x = int(std::floor(centerX)) - reach; x <= int(std::floor(centerX)) + reach; ++x) {
            const double vx = x + 0.5 - centerX, vy = y + 0.5 - centerY;
            // Back into the unturned shape.
            const int ix = int(std::floor(vx * c + vy * s + w / 2.0));
            const int iy = int(std::floor(-vx * s + vy * c + h / 2.0));
            if (ix < 0 || iy < 0 || ix >= w || iy >= h)
                continue;
            uint32_t pixel = 0;
            bool inside = true;
            switch (r.shape) {
            case ParticleRecipe::Shape::Plus:
                inside = ix == half || iy == half;
                break;
            case ParticleRecipe::Shape::Diamond:
                inside = std::abs(ix - half) + std::abs(iy - half) <= half;
                break;
            case ParticleRecipe::Shape::Brush:
                if (brush) {
                    const int sx = std::min(r.brush.width - 1, (2 * ix + 1) * r.brush.width / (2 * w));
                    const int sy = std::min(r.brush.height - 1, (2 * iy + 1) * r.brush.height / (2 * h));
                    pixel = r.brush.pixels[size_t(sy) * size_t(r.brush.width) + size_t(sx)];
                    inside = ownColors ? (pixel >> 24) > 0 : (pixel >> 24) >= 128;
                }
                break;
            default:
                break;
            }
            if (inside)
                put(x, y, pixel);
        }
}

// One particle's shape at (cx, cy). `alpha` scales the opacity of the brush's own colors; the other
// shapes are in `value` already.
template <class Target>
void stamp(Target& image, int cx, int cy, int size, uint32_t value, const ParticleRecipe& recipe, double angle,
           double alpha)
{
    const ParticleRecipe::Shape shape = recipe.shape;
    const bool brush = shape == ParticleRecipe::Shape::Brush && !recipe.brush.empty();
    const bool own = brush && recipe.brushColors;
    const bool turned = shape != ParticleRecipe::Shape::Circle && std::fmod(angle, 360.0) != 0.0;
    if (own || turned) {
        rasterTurned(recipe, cx, cy, size, angle, own, [&](int x, int y, uint32_t pixel) {
            if (!image.contains(x, y))
                return;
            if (!own) {
                image.put(x, y, value);
                return;
            }
            Rgba color = unpack(pixel);
            color.a = uint8_t(std::lround(color.a * alpha));
            if (color.a > 0)
                image.put(x, y, pack(color));
        });
        return;
    }
    switch (shape) {
    case ParticleRecipe::Shape::Brush:
        if (brush) {
            stampParticleBrush(image, cx, cy, size, value, recipe.brush);
            break;
        }
        [[fallthrough]]; // no brush yet: a square
    case ParticleRecipe::Shape::Square:
        stampSpans(image, cx, cy, size, value, BrushShape::Square);
        break;
    case ParticleRecipe::Shape::Circle:
        stampSpans(image, cx, cy, size, value, BrushShape::Round);
        break;
    case ParticleRecipe::Shape::Plus:
    case ParticleRecipe::Shape::Diamond: {
        const int r = size / 2;
        const bool diamond = shape == ParticleRecipe::Shape::Diamond;
        for (int dy = -r; dy <= (size - 1) / 2; ++dy)
            for (int dx = -r; dx <= (size - 1) / 2; ++dx) {
                const bool inside = diamond ? std::abs(dx) + std::abs(dy) <= r : dx == 0 || dy == 0;
                if (inside && image.contains(cx + dx, cy + dy))
                    image.put(cx + dx, cy + dy, value);
            }
        break;
    }
    }
}

} // namespace

namespace {

// What is decided about one particle by the seed and its index, before it moves.
struct Particle {
    int spawn = 0;
    int life = 1;
    double angle = 0; // radians, the way it is thrown
    double speed = 0;
    double sizeScale = 1;
    double pick = 0;
    double turnStart = 0;
    double turnPerFrame = 0;
    double bornX = 0, bornY = 0;
    double extraVx = 0, extraVy = 0; // added to its first velocity (what a parent hands on)
};

Particle makeParticle(const ParticleRecipe& r, int i, int count, int span)
{
    Particle p;
    const uint32_t index = uint32_t(i);
    p.spawn = r.emission == ParticleRecipe::Emission::Burst
        ? r.startFrame
        : r.startFrame + int(int64_t(i) * span / count);
    const double lifeJitter = (random01(r.seed, index, 0) - 0.5) * 2.0 * r.lifetimeRandom / 100.0;
    p.life = std::max(1, int(std::lround(r.lifetime * (1.0 + lifeJitter))));
    p.angle = (r.direction + (random01(r.seed, index, 1) - 0.5) * r.spread) * kPi / 180.0;
    p.speed = r.speed * (1.0 + (random01(r.seed, index, 2) - 0.5) * 2.0 * r.speedRandom / 100.0);
    p.sizeScale = 1.0 + (random01(r.seed, index, 3) - 0.5) * 2.0 * r.sizeRandom / 100.0;
    p.pick = random01(r.seed, index, 4);
    p.turnStart = r.angle + (random01(r.seed, index, 5) - 0.5) * 2.0 * r.angleRandom;
    p.turnPerFrame = r.spin * (1.0 + (random01(r.seed, index, 6) - 0.5) * 2.0 * r.spinRandom / 100.0);

    // Where it is born: the emitter, or a point of the source around it.
    p.bornX = r.x;
    p.bornY = r.y;
    if (r.source != ParticleRecipe::Source::Point) {
        const double u = random01(r.seed, index, 7), v = random01(r.seed, index, 8);
        const double W = std::max(0, r.sourceWidth), H = std::max(0, r.sourceHeight);
        double lx = 0, ly = 0;
        switch (r.source) {
        case ParticleRecipe::Source::Line:
            lx = (u - 0.5) * W;
            break;
        case ParticleRecipe::Source::Rectangle:
            if (r.sourceFill) {
                lx = (u - 0.5) * W;
                ly = (v - 0.5) * H;
            } else { // along the edge, once around
                const double e = u * 2.0 * (W + H);
                if (e < W) {
                    lx = e - W / 2; ly = -H / 2;
                } else if (e < W + H) {
                    lx = W / 2; ly = e - W - H / 2;
                } else if (e < 2 * W + H) {
                    lx = W / 2 - (e - W - H); ly = H / 2;
                } else {
                    lx = -W / 2; ly = H / 2 - (e - 2 * W - H);
                }
            }
            break;
        case ParticleRecipe::Source::Ellipse: {
            const double radius = r.sourceFill ? std::sqrt(u) : 1.0; // uniform over the area
            lx = std::cos(v * 2.0 * kPi) * radius * W / 2;
            ly = std::sin(v * 2.0 * kPi) * radius * H / 2;
            break;
        }
        case ParticleRecipe::Source::Point:
            break;
        }
        const double turned = r.sourceAngle * kPi / 180.0;
        p.bornX += lx * std::cos(turned) - ly * std::sin(turned);
        p.bornY += lx * std::sin(turned) + ly * std::cos(turned);
    }
    return p;
}

// Where a particle is, how it moves, and whether it has landed or vanished, `age` frames after its birth.
struct Motion {
    double px = 0, py = 0, vx = 0, vy = 0;
    bool stuck = false;
    bool vanished = false;
    int landed = -1; // the age at which it stuck on a floor
    int landedOn = -1; // which field (an index into the recipe's fields) it stuck to
    // The first time it met a floor (any mode): when, where on the line, and how fast it was going.
    int hitAge = -1;
    double hitX = 0, hitY = 0, hitVx = 0, hitVy = 0;
    int endAge = -1; // the age at which it vanished on a floor
};

Motion birthMotion(const Particle& p)
{
    Motion m;
    m.px = p.bornX;
    m.py = p.bornY;
    m.vx = std::cos(p.angle) * p.speed + p.extraVx;
    m.vy = std::sin(p.angle) * p.speed + p.extraVy;
    return m;
}

// The step from (fromX, fromY) to where the particle is now, met by the colliders: the first line it
// crosses on the way (so a fast particle can't pass through) bounces, holds or ends it like a floor. A
// bounce reverses the part of the motion into the line by the bounciness and keeps the part along it
// (less the friction), and the rest of the step goes on from there, so with no bounciness a particle
// slides along. A few hits a step at most; past that it stays on the near side.
void collide(const ParticleRecipe& r, Motion& m, double fromX, double fromY, int k)
{
    constexpr double kOff = 1e-3; // how far off the line a particle is put, on the side it came from
    double ax = fromX, ay = fromY;
    for (int hit = 0; hit < 4; ++hit) {
        const double dx = m.px - ax, dy = m.py - ay;
        double best = 2, hx = 0, hy = 0, sx = 0, sy = 0;
        const ParticleField* by = nullptr;
        for (const ParticleField& f : r.fields) {
            if (f.type != ParticleField::Type::Collider)
                continue;
            for (size_t i = 0; i + 1 < f.points.size(); ++i) {
                const double x1 = f.x + f.points[i].x, y1 = f.y + f.points[i].y;
                const double ex = f.points[i + 1].x - f.points[i].x, ey = f.points[i + 1].y - f.points[i].y;
                const double denom = dx * ey - dy * ex;
                if (std::abs(denom) < 1e-12)
                    continue; // along the line: it doesn't cross it
                const double wx = x1 - ax, wy = y1 - ay;
                const double t = (wx * ey - wy * ex) / denom; // along the step
                const double u = (wx * dy - wy * dx) / denom; // along the line
                if (t > 1e-9 && t <= 1 && u >= 0 && u <= 1 && t < best) {
                    best = t;
                    hx = ax + dx * t;
                    hy = ay + dy * t;
                    sx = ex;
                    sy = ey;
                    by = &f;
                }
            }
        }
        if (!by)
            return;
        // The line's normal on the side the particle came from.
        const double len = std::hypot(sx, sy);
        double nx = -sy / len, ny = sx / len;
        if (nx * (ax - hx) + ny * (ay - hy) < 0) {
            nx = -nx;
            ny = -ny;
        }
        if (m.hitAge < 0) { // on its side of the line, so what is born there (sub-particles) starts there too
            m.hitAge = k + 1;
            m.hitX = hx + nx * kOff;
            m.hitY = hy + ny * kOff;
            m.hitVx = m.vx;
            m.hitVy = m.vy;
        }
        if (by->mode == 1) { // holds it where it met the line
            m.px = hx + nx * kOff;
            m.py = hy + ny * kOff;
            m.vx = m.vy = 0;
            m.stuck = true;
            m.landed = k + 1;
            m.landedOn = int(by - r.fields.data());
            return;
        }
        if (by->mode == 2) {
            m.px = hx;
            m.py = hy;
            m.vanished = true;
            m.endAge = k + 1;
            return;
        }
        const double into = m.vx * nx + m.vy * ny; // negative: toward the line
        const double kept = std::clamp(by->bounciness, 0, 100) / 100.0;
        const double slide = 1.0 - std::clamp(by->friction, 0, 100) / 100.0;
        const double tx = (m.vx - into * nx) * slide, ty = (m.vy - into * ny) * slide;
        m.vx = tx - into * kept * nx;
        m.vy = ty - into * kept * ny;
        const double rest = 1 - best; // what is left of the step, along the new motion
        ax = hx + nx * kOff;
        ay = hy + ny * kOff;
        m.px = ax + m.vx * rest;
        m.py = ay + m.vy * rest;
    }
    m.px = ax; // still meeting lines: it stays where it last was, on the near side
    m.py = ay;
}

// One frame of motion: step `k` of the particle's life (it was `k` frames old), unless it has landed or
// vanished already.
void stepParticle(const ParticleRecipe& r, const Particle& p, Motion& m, int k, double q)
{
    if (m.vanished || m.stuck)
        return;
    {
        double ax = r.gravityX, ay = r.gravityY;
        for (const ParticleField& f : r.fields) {
            if (f.type == ParticleField::Type::Floor || f.type == ParticleField::Type::Collider)
                continue; // not a force
            if (f.type == ParticleField::Type::Wind) {
                double heading = f.angle;
                if (f.turbulence > 0) {
                    const double scale = std::max(1, f.scale);
                    // The wind of the frame this step happens in, as seen from the particle's birth
                    // frame, so a looping effect sees the same wind every period.
                    const double gust = noise3(r.seed, m.px / scale, m.py / scale, double(p.spawn + k) * 0.35);
                    heading += (gust - 0.5) * 2.0 * f.turbulence / 100.0 * 180.0;
                }
                ax += std::cos(heading * kPi / 180.0) * f.strength;
                ay += std::sin(heading * kPi / 180.0) * f.strength;
                continue;
            }
            const double dx = f.x - m.px, dy = f.y - m.py;
            const double dist = std::hypot(dx, dy);
            if (dist < 1e-6 || (f.radius > 0 && dist > f.radius))
                continue;
            const double weight = f.falloff == 1 || f.radius <= 0 ? 1.0 : 1.0 - dist / f.radius;
            if (f.type == ParticleField::Type::Vortex) { // around it, clockwise on the screen
                ax += dy / dist * f.strength * weight;
                ay -= dx / dist * f.strength * weight;
            } else {
                ax += dx / dist * f.strength * weight;
                ay += dy / dist * f.strength * weight;
            }
        }
        m.vx = (m.vx + ax) * q;
        m.vy = (m.vy + ay) * q;
        const double fromX = m.px, fromY = m.py;
        m.px += m.vx;
        m.py += m.vy;
        for (const ParticleField& f : r.fields) {
            if (f.type != ParticleField::Type::Floor || m.py <= f.y)
                continue;
            if (m.hitAge < 0) {
                m.hitAge = k + 1;
                m.hitX = m.px;
                m.hitY = f.y;
                m.hitVx = m.vx;
                m.hitVy = m.vy;
            }
            if (f.mode == 1) { // sticks where it lands
                m.py = f.y;
                m.vx = m.vy = 0;
                m.stuck = true;
                m.landed = k + 1;
                m.landedOn = int(&f - r.fields.data());
            } else if (f.mode == 2) {
                m.vanished = true;
                m.endAge = k + 1;
            } else { // back up by what it kept; resting once it barely moves
                const double kept = std::clamp(f.bounciness, 0, 100) / 100.0;
                m.py = f.y - (m.py - f.y) * kept;
                m.vy = -m.vy * kept;
                m.vx *= 1.0 - std::clamp(f.friction, 0, 100) / 100.0;
                if (std::abs(m.vy) < 0.1) {
                    m.vy = 0;
                    m.py = f.y;
                }
            }
        }
        if (!m.vanished && !m.stuck)
            collide(r, m, fromX, fromY, k);
    }
}

Motion moveParticle(const ParticleRecipe& r, const Particle& p, int age, double q)
{
    Motion m = birthMotion(p);
    for (int k = 0; k < age && !m.vanished && !m.stuck; ++k)
        stepParticle(r, p, m, k, q);
    return m;
}

// The particle's motion at every age from 0 to `last`, stepped once (`track[a]` is moveParticle at a).
void trackParticle(const ParticleRecipe& r, const Particle& p, int last, double q, std::vector<Motion>& track)
{
    track.resize(size_t(std::max(0, last) + 1));
    track[0] = birthMotion(p);
    for (int k = 0; k < last; ++k) {
        track[size_t(k) + 1] = track[size_t(k)];
        stepParticle(r, p, track[size_t(k) + 1], k, q);
    }
}

int sizeAtAge(const ParticleRecipe& r, const Particle& p, int age)
{
    const double t = p.life > 1 ? double(age) / double(p.life - 1) : 0.0;
    return int(std::lround(lerp(r.sizeStart, r.sizeEnd, eased(r.sizeEase, t, r.sizeCurve)) * p.sizeScale));
}

// Where a particle that piled up comes to rest, and from when.
struct Rest {
    bool landed = false;
    int frame = 0; // the first frame it is at rest
    int age = 0;   // its age then, which its look stays at
    int cx = 0, cy = 0;
};

// Whether particles that stick to this field pile up on it: a floor or a collider that holds them and stacks.
bool stacks(const ParticleField& f)
{
    return (f.type == ParticleField::Type::Floor || f.type == ParticleField::Type::Collider) && f.mode == 1 && f.stack;
}

// Drops every particle that sticks to a stacking floor or collider in the order they land, each on top of
// what has landed on that surface before it (sliding to a lower neighbor when it would stand 2 pixels
// higher, so a steep slope sheds what a gentle one holds), and says where each ends up. The order is by
// landing frame and then by index, so it is the same every time. Each surface keeps, per column of the
// canvas, the first row that is taken: the surface itself at first (a floor's row, or where a collider's
// line crosses the column, the topmost of its segments there), then the top of the pile. A collider's
// pile doesn't slide past the ends of its line.
std::vector<Rest> pilePlan(const ParticleRecipe& r, int width, int count, int span)
{
    struct Landing {
        int frame, index, age, size, on;
        double x, y;
    };
    std::vector<std::optional<Landing>> landed(static_cast<size_t>(count));
    parallelFor(count, int64_t(count) * std::max(1, r.lifetime), [&](int i) {
        const Particle p = makeParticle(r, i, count, span);
        const Motion m = moveParticle(r, p, p.life, 1.0 - std::clamp(r.drag, 0.0, 1.0));
        if (m.stuck && !m.vanished && m.landed >= 0 && m.landedOn >= 0 && stacks(r.fields[size_t(m.landedOn)])) {
            const int size = sizeAtAge(r, p, m.landed);
            if (size >= 1)
                landed[size_t(i)] = Landing{p.spawn + m.landed, i, m.landed, size, m.landedOn, m.px, m.py};
        }
    });
    std::vector<Landing> landings;
    std::vector<Rest> rests(static_cast<size_t>(count));
    for (const std::optional<Landing>& l : landed)
        if (l)
            landings.push_back(*l);
    std::sort(landings.begin(), landings.end(), [](const Landing& a, const Landing& b) {
        return a.frame != b.frame ? a.frame < b.frame : a.index < b.index;
    });

    // The first taken row of each column, per stacking surface (made when something lands on it).
    constexpr int kNone = std::numeric_limits<int>::max(); // a collider's line doesn't reach this column
    const int columns = std::max(1, width);
    std::vector<std::vector<int>> taken(r.fields.size());
    const auto surface = [&](int on) -> std::vector<int>& {
        std::vector<int>& top = taken[size_t(on)];
        if (!top.empty())
            return top;
        const ParticleField& f = r.fields[size_t(on)];
        if (f.type == ParticleField::Type::Floor) {
            top.assign(size_t(columns), int(std::floor(f.y)));
            return top;
        }
        top.assign(size_t(columns), kNone);
        for (int x = 0; x < columns; ++x) {
            const double cx = x + 0.5;
            double best = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i + 1 < f.points.size(); ++i) {
                const double x1 = f.x + f.points[i].x, y1 = f.y + f.points[i].y;
                const double x2 = f.x + f.points[i + 1].x, y2 = f.y + f.points[i + 1].y;
                if (x1 == x2 || cx < std::min(x1, x2) || cx > std::max(x1, x2))
                    continue; // upright, or not over this column
                best = std::min(best, y1 + (y2 - y1) * (cx - x1) / (x2 - x1));
            }
            if (best != std::numeric_limits<double>::infinity())
                top[size_t(x)] = int(std::floor(best));
        }
        return top;
    };
    for (const Landing& l : landings) {
        std::vector<int>& top = surface(l.on);
        const bool floor = r.fields[size_t(l.on)].type == ParticleField::Type::Floor;
        // The highest taken row under columns from..to (the smallest), kNone where there is nothing; past
        // the canvas a floor is at its own row.
        const auto highest = [&](int from, int to) {
            int row = kNone;
            for (int x = std::max(0, from); x <= std::min(columns - 1, to); ++x)
                row = std::min(row, top[size_t(x)]);
            if (row == kNone && floor)
                row = int(std::floor(r.fields[size_t(l.on)].y));
            return row;
        };
        int c = int(std::floor(l.x));
        const int s = l.size;
        const bool leftFirst = random01(r.seed, uint32_t(l.index), 9) < 0.5;
        for (int guard = 0; guard < 4 * s + 64; ++guard) {
            const int left = c - s / 2, right = left + s - 1;
            const int here = highest(left, right);
            if (here == kNone)
                break;
            const int toLeft = highest(left - 1, right - 1), toRight = highest(left + 1, right + 1);
            // A neighbor at least 2 rows lower is somewhere to slide (nothing there is no ground).
            const bool canLeft = toLeft != kNone && toLeft >= here + 2, canRight = toRight != kNone && toRight >= here + 2;
            if (canLeft && (!canRight || toLeft > toRight || (toLeft == toRight && leftFirst)))
                --c;
            else if (canRight)
                ++c;
            else
                break;
        }
        const int left = c - s / 2, right = left + s - 1;
        int base = highest(left, right);
        if (base == kNone) // off the ends of the line: where it stuck
            base = int(std::floor(l.y)) + 1;
        for (int x = std::max(0, left); x <= std::min(columns - 1, right); ++x)
            top[size_t(x)] = base - s;
        Rest& rest = rests[size_t(l.index)];
        rest.landed = true;
        rest.frame = l.frame;
        rest.age = l.age;
        rest.cx = c;
        rest.cy = base - s + s / 2; // its bottom row sits on what is below
    }
    return rests;
}

} // namespace

namespace {

ParticlePreset makeFire()
{
    ParticlePreset p;
    p.id = "fire";
    p.name = "Fire";
    p.description = "A small flame: a row of embers rising, cooling from white-yellow through orange to a dark red, flickering sideways. Loops.";
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 64;
    r.startFrame = 0;
    r.endFrame = 15;
    r.loop = true;
    r.seed = 11;
    r.lifetime = 19;
    r.lifetimeRandom = 30;
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 5;
    r.sourceAngle = 0;
    r.direction = -90;
    r.spread = 22;
    r.speed = 1.25;
    r.speedRandom = 45;
    r.gravityY = -0.025;
    r.drag = 0.04;
    r.sizeStart = 3;
    r.sizeEnd = 1;
    r.sizeRandom = 30;
    r.shape = ParticleRecipe::Shape::Square;
    r.opacityStart = 100;
    r.opacityEnd = 0;
    r.opacityEase = ParticleRecipe::Ease::In;
    r.fadePattern = 2; // dithered, for crisp pixels
    r.colorMode = ParticleRecipe::ColorMode::OverLife;
    r.colors = {Rgba{255, 226, 120, 255}, Rgba{255, 170, 50, 255}, Rgba{226, 72, 24, 255}, Rgba{110, 30, 22, 255}};
    ParticleField wind; // a flicker: the direction wanders all round
    wind.type = ParticleField::Type::Wind;
    wind.x = 9;
    wind.y = -12;
    wind.strength = 0.05;
    wind.angle = 0;
    wind.turbulence = 80;
    wind.scale = 12;
    r.fields = {wind};
    return p;
}

ParticlePreset makeSmoke()
{
    ParticlePreset p;
    p.id = "smoke";
    p.name = "Smoke";
    p.description = "A slow grey plume that widens as it rises and fades out, drifting a little. Loops.";
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 30;
    r.startFrame = 0;
    r.endFrame = 31;
    r.loop = true;
    r.seed = 5;
    r.lifetime = 36;
    r.lifetimeRandom = 30;
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 4;
    r.direction = -90;
    r.spread = 20;
    r.speed = 0.45;
    r.speedRandom = 40;
    r.drag = 0.01;
    r.gravityY = -0.012;
    r.sizeStart = 2;
    r.sizeEnd = 7;
    r.sizeRandom = 25;
    r.sizeEase = ParticleRecipe::Ease::Out;
    r.shape = ParticleRecipe::Shape::Circle;
    r.opacityStart = 60;
    r.opacityEnd = 0;
    r.opacityEase = ParticleRecipe::Ease::In;
    r.fadePattern = 2;
    r.colors = {Rgba{96, 96, 106, 255}, Rgba{140, 140, 152, 255}, Rgba{182, 182, 192, 255}};
    ParticleField wind;
    wind.type = ParticleField::Type::Wind;
    wind.x = 10;
    wind.y = -16;
    wind.strength = 0.012;
    wind.angle = 0;
    wind.turbulence = 60;
    wind.scale = 20;
    r.fields = {wind};
    return p;
}

ParticlePreset makeSparks()
{
    ParticlePreset p;
    p.id = "sparks";
    p.name = "Sparks";
    p.description = "Bright sparks thrown up and out that fall back, cooling from white-yellow to orange. Loops.";
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 24;
    r.startFrame = 0;
    r.endFrame = 11;
    r.loop = true;
    r.seed = 23;
    r.lifetime = 20;
    r.lifetimeRandom = 40;
    r.direction = -90;
    r.spread = 84;
    r.speed = 3.1;
    r.speedRandom = 45;
    r.drag = 0.02;
    r.gravityY = 0.17;
    r.sizeStart = 2;
    r.sizeEnd = 1;
    r.sizeRandom = 20;
    r.shape = ParticleRecipe::Shape::Square;
    r.opacityStart = 100;
    r.opacityEnd = 0;
    r.opacityEase = ParticleRecipe::Ease::In;
    r.fadePattern = 2;
    r.colors = {Rgba{255, 250, 200, 255}, Rgba{255, 200, 70, 255}, Rgba{255, 120, 30, 255}};
    return p;
}

ParticlePreset makeSnow()
{
    ParticlePreset p;
    p.id = "snow";
    p.name = "Snow";
    p.description = "Flakes drifting down across the whole width, swaying in a light wind. Loops. The emitter starts at the top.";
    p.homeY = 0.0;
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 56;
    r.startFrame = 0;
    r.endFrame = 31;
    r.loop = true;
    r.seed = 3;
    r.lifetime = 72;
    r.lifetimeRandom = 10;
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 64;
    r.direction = 90;
    r.spread = 20;
    r.speed = 0.9;
    r.speedRandom = 35;
    r.drag = 0.05; // with this gravity a flake falls at about its starting speed, and the wind can't make it run away
    r.gravityY = 0.05;
    r.sizeStart = 1.4;
    r.sizeEnd = 1.4;
    r.sizeRandom = 45;
    r.shape = ParticleRecipe::Shape::Square;
    r.opacityStart = 100;
    r.opacityEnd = 100;
    r.colors = {Rgba{255, 255, 255, 255}, Rgba{214, 232, 250, 255}};
    r.colorMode = ParticleRecipe::ColorMode::Random;
    ParticleField wind;
    wind.type = ParticleField::Type::Wind;
    wind.x = 30;
    wind.y = 10;
    wind.strength = 0.03;
    wind.angle = 0;
    wind.turbulence = 90;
    wind.scale = 22;
    r.fields = {wind};
    return p;
}

ParticlePreset makeSnowPile()
{
    ParticlePreset p;
    p.id = "snowpile";
    p.name = "Snow Pile";
    p.description = "Snow that settles: it is already falling at the first frame (a pre-roll), keeps falling to the last, and the flakes pile up on a floor near the bottom into drifts. Not a loop.";
    p.homeY = 0.0;
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 420;
    r.perFrame = true;
    r.rate = 10;    // a flake count a frame, so the length of the animation does not thin it
    r.startFrame = -64; // a pre-roll of about the fall: the first frame already has snow all the way down
    r.endFrame = 59;    // (the length it was made for)
    r.untilEnd = true;
    r.loop = false;
    r.seed = 8;
    r.lifetime = 80;
    r.lifetimeRandom = 5;
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 64;
    r.direction = 90;
    r.spread = 14;
    r.speed = 0.95;
    r.speedRandom = 25;
    r.drag = 0.05;
    r.gravityY = 0.05;
    r.sizeStart = 1;
    r.sizeEnd = 1;
    r.shape = ParticleRecipe::Shape::Square;
    r.opacityStart = 100;
    r.opacityEnd = 100;
    r.colors = {Rgba{255, 255, 255, 255}, Rgba{214, 232, 250, 255}};
    r.colorMode = ParticleRecipe::ColorMode::Random;
    ParticleField wind;
    wind.type = ParticleField::Type::Wind;
    wind.x = 30;
    wind.y = 10;
    wind.strength = 0.02;
    wind.angle = 0;
    wind.turbulence = 80;
    wind.scale = 24;
    ParticleField floor;
    floor.type = ParticleField::Type::Floor;
    floor.x = 0;
    floor.y = 59.5; // near the bottom of a 64 pixel sprite, from the emitter at the top
    floor.mode = 1;
    floor.stack = true;
    r.fields = {wind, floor};
    return p;
}

ParticlePreset makeRain()
{
    ParticlePreset p;
    p.id = "rain";
    p.name = "Rain";
    p.description = "Streaks falling across the whole width onto a floor near the bottom, where each throws a little splash. Loops. The emitter starts at the top.";
    p.homeY = 0.0;
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 60;
    r.startFrame = 0;
    r.endFrame = 15;
    r.loop = true;
    r.seed = 17;
    r.lifetime = 24;
    r.lifetimeRandom = 0;
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 64;
    r.direction = 90;
    r.spread = 4;
    r.speed = 3.2;
    r.speedRandom = 12;
    r.sizeStart = 3; // the longest side of the streak below
    r.sizeEnd = 3;
    r.shape = ParticleRecipe::Shape::Brush;
    r.brush.width = 3;
    r.brush.height = 1;
    r.brush.pixels = std::vector<uint32_t>(3, pack(Rgba{255, 255, 255, 255}));
    r.faceMotion = true; // the streak lies along the way it falls
    r.angleSnap = 2;
    r.opacityStart = 85;
    r.opacityEnd = 85;
    r.colors = {Rgba{150, 184, 232, 255}};
    ParticleField floor;
    floor.type = ParticleField::Type::Floor;
    floor.x = 0;
    floor.y = 59.5;
    floor.mode = 2; // the drop is gone, the splash takes over
    r.fields = {floor};
    r.subTrigger = ParticleRecipe::Trigger::Impact;
    ParticleRecipe splash;
    splash.emission = ParticleRecipe::Emission::Burst;
    splash.count = 3;
    splash.lifetime = 5;
    splash.lifetimeRandom = 20;
    splash.direction = -90;
    splash.spread = 130;
    splash.speed = 0.95;
    splash.speedRandom = 30;
    splash.gravityY = 0.19;
    splash.sizeStart = 1;
    splash.sizeEnd = 1;
    splash.opacityStart = 100;
    splash.opacityEnd = 0;
    splash.opacityEase = ParticleRecipe::Ease::In;
    splash.fadePattern = 2;
    splash.colors = {Rgba{196, 220, 250, 255}};
    r.children = {splash};
    return p;
}

ParticlePreset makeFireworks()
{
    ParticlePreset p;
    p.id = "fireworks";
    p.name = "Fireworks";
    p.description = "Rockets rising from the bottom that burst into a ring of sparks when they die, the sparks cooling and falling. Loops. The emitter starts near the bottom.";
    p.homeY = 0.92;
    ParticleRecipe& r = p.recipe;
    r.preset = p.id;
    r.emission = ParticleRecipe::Emission::Stream;
    r.count = 3;
    r.startFrame = 0;
    r.endFrame = 29;
    r.loop = true;
    r.seed = 29;
    r.lifetime = 18;
    r.lifetimeRandom = 18;
    r.source = ParticleRecipe::Source::Line;
    r.sourceWidth = 30;
    r.direction = -90;
    r.spread = 10;
    r.speed = 3.0;
    r.speedRandom = 12;
    r.gravityY = 0.1;
    r.sizeStart = 2;
    r.sizeEnd = 1;
    r.shape = ParticleRecipe::Shape::Square;
    r.opacityStart = 100;
    r.opacityEnd = 100;
    r.colors = {Rgba{255, 232, 170, 255}};
    r.subTrigger = ParticleRecipe::Trigger::Death;
    ParticleRecipe spark;
    spark.emission = ParticleRecipe::Emission::Burst;
    spark.count = 30;
    spark.lifetime = 26;
    spark.lifetimeRandom = 20;
    spark.direction = 0;
    spark.spread = 360;
    spark.speed = 1.9;
    spark.speedRandom = 25;
    spark.drag = 0.06;
    spark.gravityY = 0.05;
    spark.sizeStart = 2;
    spark.sizeEnd = 1;
    spark.opacityStart = 100;
    spark.opacityEnd = 0;
    spark.opacityEase = ParticleRecipe::Ease::In;
    spark.fadePattern = 2;
    spark.colors = {Rgba{255, 250, 210, 255}, Rgba{255, 196, 70, 255}, Rgba{240, 90, 40, 255}};
    r.children = {spark};
    return p;
}

} // namespace

const std::vector<ParticlePreset>& particlePresets()
{
    static const std::vector<ParticlePreset> presets = {makeFire(), makeSmoke(), makeSparks(), makeSnow(), makeSnowPile(), makeRain(), makeFireworks()};
    return presets;
}

const std::vector<ParticleGroupPreset>& particleGroupPresets()
{
    static const std::vector<ParticleGroupPreset> groups = {
        {"campfire", "Campfire", "A flame with its smoke rising above it, as two layers in a group. Loops over the smoke's 32 frames.",
         0.5, 0.7, {{"fire", 0, 0}, {"smoke", 0, -14}}},
    };
    return groups;
}

const ParticleGroupPreset* findParticleGroupPreset(std::string_view id)
{
    for (const ParticleGroupPreset& g : particleGroupPresets())
        if (g.id == id)
            return &g;
    return nullptr;
}

const ParticlePreset* findParticlePreset(std::string_view id)
{
    for (const ParticlePreset& p : particlePresets())
        if (p.id == id)
            return &p;
    return nullptr;
}

double presetScaleFor(int width, int height)
{
    return std::clamp(double(std::min(width, height)) / 64.0, 0.5, 3.0);
}

namespace {

// Scales what is distance or size in a recipe (and in its sub-particles').
void scaleRecipe(ParticleRecipe& r, double scale)
{
    const auto whole = [&](int v) { return v > 0 ? std::max(1, int(std::lround(v * scale))) : 0; };
    r.count = std::max(1, int(std::lround(r.count * scale)));
    r.rate *= scale;
    r.sourceWidth = whole(r.sourceWidth);
    r.sourceHeight = whole(r.sourceHeight);
    r.speed *= scale;
    r.gravityX *= scale;
    r.gravityY *= scale;
    r.sizeStart *= scale;
    r.sizeEnd *= scale;
    for (ParticleField& f : r.fields) {
        f.x *= scale;
        f.y *= scale;
        f.strength *= scale;
        f.radius = whole(f.radius);
        f.scale = std::max(1, int(std::lround(f.scale * scale)));
        for (ParticlePoint& p : f.points) {
            p.x *= scale;
            p.y *= scale;
        }
    }
    for (ParticleRecipe& child : r.children) {
        const int perParent = child.count; // a round of sub-particles is not bigger on a bigger sprite
        scaleRecipe(child, scale);
        child.count = perParent;
    }
}

} // namespace

ParticleRecipe scaledRecipe(const ParticleRecipe& recipe, double factor)
{
    ParticleRecipe r = recipe;
    if (factor != 1.0)
        scaleRecipe(r, factor);
    return r;
}

ParticleRecipe placedPreset(const ParticlePreset& preset, double x, double y, double scale)
{
    ParticleRecipe r = preset.recipe;
    r.presetScale = scale;
    if (scale != 1.0)
        scaleRecipe(r, scale);
    r.x = x;
    r.y = y;
    for (ParticleField& f : r.fields) {
        f.x += x;
        f.y += y;
    }
    return r;
}

ParticleRecipe movedRecipe(const ParticleRecipe& recipe, double scaleX, double scaleY, double dx, double dy)
{
    ParticleRecipe r = recipe;
    r.x = r.x * scaleX + dx;
    r.y = r.y * scaleY + dy;
    const auto move = [&](ParticleField& f) {
        f.x = f.x * scaleX + dx;
        f.y = f.y * scaleY + dy;
        for (ParticlePoint& p : f.points) { // relative to (x, y)
            p.x *= scaleX;
            p.y *= scaleY;
        }
    };
    for (ParticleField& f : r.fields)
        move(f);
    for (ParticleRecipe& child : r.children) // its emitter isn't used (it is born where its parent ends)
        for (ParticleField& f : child.fields)
            move(f);
    return r;
}

int particleFramesNeeded(const ParticleRecipe& r)
{
    const int span = std::max(1, r.endFrame - r.startFrame + 1);
    // A round of sub-particles born when the last particle ends lasts a while longer.
    int tail = 0;
    if (r.subTrigger != ParticleRecipe::Trigger::None && !r.children.empty()) {
        const ParticleRecipe& c = r.children.front();
        tail = std::max(1, c.endFrame - c.startFrame + 1) + int(std::ceil(c.lifetime * (1.0 + c.lifetimeRandom / 100.0)));
    }
    if (r.loop)
        return span;
    if (r.untilEnd && r.emission == ParticleRecipe::Emission::Stream)
        return std::max(1, r.endFrame + 1); // as long as it was made for: it runs to the end of whatever there is
    const int last = r.emission == ParticleRecipe::Emission::Burst ? r.startFrame : r.endFrame;
    return last + std::max(1, int(std::ceil(r.lifetime * (1.0 + r.lifetimeRandom / 100.0)))) + tail;
}

bool differsFromReference(const ParticleRecipe& recipe, const ParticleRecipe& reference)
{
    ParticleRecipe a = recipe;
    a.seed = reference.seed;
    if (a.fields.size() == reference.fields.size())
        for (size_t i = 0; i < a.fields.size(); ++i)
            if (a.fields[i].type == ParticleField::Type::Wind && reference.fields[i].type == ParticleField::Type::Wind) {
                a.fields[i].x = reference.fields[i].x; // a wind has no place, only a handle
                a.fields[i].y = reference.fields[i].y;
            }
    return !(a == reference);
}

namespace {

// The particle at `age` frames old drawn into `out`, from its track (moveParticle at each age); one at
// rest shows its look from when it landed, in its place.
void drawParticle(Image& out, const ParticleRecipe& r, const Particle& p, int age, const Rest* rest,
                  const std::vector<Motion>& track)
{
    if (rest)
        age = std::min(age, rest->age);
    Motion m = track[size_t(age)];
    if (m.vanished)
        return;
    if (rest && age >= rest->age) {
        m.px = rest->cx;
        m.py = rest->cy;
    }
    const int size = sizeAtAge(r, p, age);
    if (size < 1)
        return;
    const double t = p.life > 1 ? double(age) / double(p.life - 1) : 0.0;
    const Rgba color = r.colorMode == ParticleRecipe::ColorMode::Random && !r.colors.empty()
        ? r.colors[std::min(r.colors.size() - 1, size_t(p.pick * double(r.colors.size())))]
        : colorAt(r.colors, t);
    const double opacity = std::clamp(lerp(r.opacityStart, r.opacityEnd, eased(r.opacityEase, t, r.opacityCurve)) / 100.0, 0.0, 1.0);
    if (opacity <= 0)
        return;
    const int cx = int(std::floor(m.px)), cy = int(std::floor(m.py));
    // Where the shape points: its own angle, turning, and (facing) along the way it moves.
    double turn = p.turnStart + p.turnPerFrame * age;
    if (r.faceMotion)
        turn += std::hypot(m.vx, m.vy) > 1e-9 ? std::atan2(m.vy, m.vx) * 180.0 / kPi : p.angle * 180.0 / kPi;
    static constexpr double kSnap[] = {0, 15, 45, 90};
    if (const double step = kSnap[std::clamp(r.angleSnap, 0, 3)]; step > 0)
        turn = std::round(turn / step) * step;
    PlainTarget plain{out};
    if (r.fadePattern == 0) {
        Rgba shown = color;
        shown.a = uint8_t(std::lround(color.a * opacity));
        stamp(plain, cx, cy, size, pack(shown), r, turn, opacity);
    } else if (opacity >= 1.0) {
        stamp(plain, cx, cy, size, pack(color), r, turn, 1.0);
    } else {
        const IndexedDither pattern = r.fadePattern == 1 ? IndexedDither::Bayer2
            : r.fadePattern == 2 ? IndexedDither::Ordered
            : r.fadePattern == 3 ? IndexedDither::Bayer8 : IndexedDither::BlueNoise;
        const int side = size * 3 / 2 + 4; // room for a turned corner
        const int c = side / 2;
        DitherTarget dithered{out, Rect{cx - c, cy - c, side, side}, pattern, opacity};
        stamp(dithered, cx, cy, size, pack(color), r, turn, 1.0);
    }
}

// When and where a particle gives birth to its sub-particles, and how fast it was going.
struct Birth {
    bool happens = false;
    int age = 0;
    double x = 0, y = 0, vx = 0, vy = 0;
};

// `m` is the particle's motion at the end of its life (moveParticle at p.life).
Birth birthOf(const ParticleRecipe& r, const Particle& p, bool atRest, const Motion& m)
{
    Birth b;
    if (r.subTrigger == ParticleRecipe::Trigger::Impact) {
        if (m.hitAge >= 0 && m.hitAge <= p.life)
            b = {true, m.hitAge, m.hitX, m.hitY, m.hitVx, m.hitVy};
    } else if (r.subTrigger == ParticleRecipe::Trigger::Death) {
        if (m.vanished)
            b = {true, m.endAge, m.hitX, m.hitY, m.hitVx, m.hitVy}; // it ended on the floor
        else if (!atRest) // a particle that piled up stays: it doesn't die
            b = {true, p.life, m.px, m.py, m.vx, m.vy};
    }
    return b;
}

// Frames [first, first + outs.size()) being drawn, one image each.
struct FrameRange {
    int first = 0;
    std::vector<Image>& outs;
    int end() const { return first + int(outs.size()); }
    Image& at(int frame) { return outs[size_t(frame - first)]; }
};

// The sub-particles of one parent in every frame of the range: for each time the parent gives birth (once,
// or once a period in a looping effect) a round of them, from a recipe of their own placed where it happened.
void drawChildren(FrameRange& range, const ParticleRecipe& r, int parentIndex, const Particle& p, const Birth& b,
                  int span, std::vector<Motion>& track)
{
    const ParticleRecipe& c = r.children.front();
    const int childSpan = std::max(1, c.endFrame - c.startFrame + 1);
    const int childCount = std::max(0, c.count);
    const int reach = childSpan + int(std::ceil(c.lifetime * (1.0 + c.lifetimeRandom / 100.0))) + 1; // frames a round lasts
    const int first = p.spawn + b.age;
    // The rounds that can be alive in the range: one, or those of the periods around it (a round is alive
    // at frames born .. born + reach - 1).
    int oldest = 0, newest = 0;
    if (r.loop) {
        oldest = floorDiv(range.first - reach + 1 - first, span);
        newest = floorDiv(range.end() - 1 - first, span);
    }
    for (int n = oldest; n <= newest; ++n) {
        const int born = first + n * span;
        const int from = std::max(range.first, born), to = std::min(range.end(), born + reach);
        if (from >= to)
            continue;
        ParticleRecipe round = c;
        round.children.clear();
        round.loop = false;
        round.subTrigger = ParticleRecipe::Trigger::None;
        round.x = b.x;
        round.y = b.y;
        round.startFrame = born;
        round.endFrame = born + childSpan - 1;
        round.seed = mix(r.seed ^ mix(uint32_t(parentIndex) * 0x9e3779b9U + 0x7f4a7c15U)) ^ c.seed;
        for (const ParticleField& f : r.fields)
            if (f.type == ParticleField::Type::Floor || f.type == ParticleField::Type::Collider)
                round.fields.push_back(f); // they meet the same floors and colliders
        const double q = 1.0 - std::clamp(round.drag, 0.0, 1.0);
        for (int k = 0; k < childCount; ++k) {
            Particle cp = makeParticle(round, k, childCount, childSpan);
            cp.extraVx = b.vx * r.subInherit / 100.0;
            cp.extraVy = b.vy * r.subInherit / 100.0;
            const int lo = std::max(from, cp.spawn), hi = std::min(to, cp.spawn + cp.life);
            if (lo >= hi)
                continue;
            trackParticle(round, cp, hi - 1 - cp.spawn, q, track);
            for (int frame = lo; frame < hi; ++frame)
                drawParticle(range.at(frame), round, cp, frame - cp.spawn, nullptr, track);
        }
    }
}

// Draws every frame of the range. A particle is stepped once over its life and drawn into each frame it is
// in, in the same order as frame by frame (by index, then age, then its sub-particles), so the pictures are
// the same as drawing each frame alone.
void renderRange(const ParticleRecipe& recipe, FrameRange range, int width, int lastFrame)
{
    const ParticleRecipe r = resolvedRecipe(recipe, lastFrame);
    const int count = std::max(0, r.count);
    const int span = std::max(1, r.endFrame - r.startFrame + 1);
    const double q = 1.0 - std::clamp(r.drag, 0.0, 1.0);

    // Particles that stick to a floor or collider that stacks pile up (not in a looping effect, whose
    // piles would have to melt to tile).
    std::vector<Rest> rests;
    if (!r.loop && count > 0 && std::any_of(r.fields.begin(), r.fields.end(), stacks))
        rests = pilePlan(r, width, count, span);
    const bool births = r.subTrigger != ParticleRecipe::Trigger::None && !r.children.empty();

    // What one particle needs: the frames it is drawn in and its track (empty when it isn't drawn and
    // has no sub-particles).
    struct Plan {
        Particle p;
        const Rest* rest = nullptr;
        int from = 0, to = 0;
        std::vector<Motion> track;
    };
    // Stepping is most of the work and particles don't depend on each other, so a block of them is
    // stepped on several threads, then drawn in order on this one.
    constexpr int kBlock = 1024;
    std::vector<Plan> plans(size_t(std::min(count, kBlock)));
    std::vector<Motion> childTrack;
    for (int start = 0; start < count; start += kBlock) {
        const int n = std::min(kBlock, count - start);
        parallelFor(n, int64_t(n) * std::max(1, r.lifetime), [&](int j) {
            const int i = start + j;
            Plan& plan = plans[size_t(j)];
            plan.p = makeParticle(r, i, count, span);
            const Particle& p = plan.p;
            plan.rest = !rests.empty() && rests[size_t(i)].landed ? &rests[size_t(i)] : nullptr;

            // The frames it is drawn in, and the oldest age it is drawn at.
            int from = range.first, to = range.end(), oldest = p.life - 1;
            if (!r.loop) {
                from = std::max(from, p.spawn);
                if (plan.rest)
                    oldest = plan.rest->age; // it shows as it landed from then on, to the end
                else
                    to = std::min(to, p.spawn + p.life);
            }
            plan.from = from;
            plan.to = to;
            if (from < to || births)
                trackParticle(r, p, births ? p.life : std::max(oldest, 0), q, plan.track);
            else
                plan.track.clear();
        });
        for (int j = 0; j < n; ++j) {
            const int i = start + j;
            const Plan& plan = plans[size_t(j)];
            const Particle& p = plan.p;
            const Rest* rest = plan.rest;
            if (plan.track.empty())
                continue;
            if (!r.loop) {
                for (int frame = plan.from; frame < plan.to; ++frame) {
                    const int age = frame - p.spawn;
                    if (age < p.life || (rest && frame >= rest->frame))
                        drawParticle(range.at(frame), r, p, age, rest, plan.track);
                }
            } else {
                // Every period it is born again, so it may be alive more than once at a time.
                for (int frame = plan.from; frame < plan.to; ++frame)
                    for (int age = floorMod(frame - p.spawn, span); age < p.life; age += span)
                        drawParticle(range.at(frame), r, p, age, rest, plan.track);
            }
            if (births) {
                const Birth b = birthOf(r, p, rest != nullptr, plan.track[size_t(p.life)]);
                if (b.happens)
                    drawChildren(range, r, i, p, b, span, childTrack);
            }
        }
    }
}

} // namespace

ParticleRecipe resolvedRecipe(const ParticleRecipe& recipe, int lastFrame)
{
    ParticleRecipe r = recipe;
    if (r.emission == ParticleRecipe::Emission::Stream) {
        if (r.untilEnd && lastFrame >= 0)
            r.endFrame = std::max(lastFrame, r.startFrame);
        if (r.perFrame)
            r.count = std::max(1, int(std::lround(r.rate * std::max(1, r.endFrame - r.startFrame + 1))));
    }
    return r;
}

Image renderParticles(const ParticleRecipe& recipe, int frame, int width, int height, int lastFrame)
{
    std::vector<Image> outs;
    outs.emplace_back(width, height);
    outs[0].clear(0);
    renderRange(recipe, FrameRange{frame, outs}, width, lastFrame);
    return std::move(outs[0]);
}

void renderParticleLayer(const Document& doc, Layer& layer)
{
    if (!layer.particles)
        return;
    const int frames = doc.frameCount();
    layer.cels.assign(size_t(frames), nullptr);
    std::vector<Image> outs;
    outs.reserve(size_t(frames));
    for (int f = 0; f < frames; ++f) {
        outs.emplace_back(doc.width(), doc.height());
        outs.back().clear(0);
    }
    renderRange(*layer.particles, FrameRange{0, outs}, doc.width(), frames - 1);
    for (int f = 0; f < frames; ++f) {
        const Image& rgba = outs[size_t(f)];
        bool any = false;
        for (int y = 0; y < rgba.height() && !any; ++y)
            for (int x = 0; x < rgba.width(); ++x)
                if (rgba.pixel(x, y) >> 24) {
                    any = true;
                    break;
                }
        if (!any)
            continue;
        auto image = std::make_shared<Image>(convertToDocument(doc, rgba));
        if (f > 0 && layer.cels[size_t(f) - 1] && *layer.cels[size_t(f) - 1]->image == *image) {
            layer.cels[size_t(f)] = layer.cels[size_t(f) - 1]; // the same picture: linked
            continue;
        }
        auto cel = std::make_shared<CelData>();
        cel->image = std::move(image);
        layer.cels[size_t(f)] = std::move(cel);
    }
}

} // namespace core
