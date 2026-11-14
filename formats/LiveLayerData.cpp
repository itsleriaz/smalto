#include "formats/LiveLayerData.h"

#include "core/Compositor.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace core;

namespace formats {
namespace {

constexpr const char* kProperty = "smalto";
constexpr int32_t kVersion = 1;

// --- Aseprite's user data properties (user data chunk 0x2020, flag 4) -----------------------------------

enum Type : uint16_t {
    Null = 0, Bool, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Fixed, Float, Double,
    String, Point, Size, Rect, Vector, Nested, Uuid
};

struct Value;
using Fields = std::vector<std::pair<std::string, Value>>;

// A property value read losslessly: plain values keep their bytes, vectors and nested maps their parts.
struct Value {
    uint16_t type = Null;
    Bytes raw;                 // plain values (strings with their length)
    uint16_t elementType = 0;  // vectors; 0: every element carries its own type
    std::vector<Value> items;  // vectors
    Fields fields;             // nested maps
};

struct Map {
    uint32_t key = 0; // 0: the user's own properties
    Fields fields;
};

struct Cursor {
    const Bytes& data;
    size_t pos;
    bool ok = true;

    bool has(size_t n) { if (pos + n > data.size()) ok = false; return ok; }
    uint16_t word() { if (!has(2)) return 0; const uint16_t v = uint16_t(data[pos] | data[pos + 1] << 8); pos += 2; return v; }
    uint32_t dword()
    {
        if (!has(4))
            return 0;
        const uint32_t v = uint32_t(data[pos]) | uint32_t(data[pos + 1]) << 8 | uint32_t(data[pos + 2]) << 16
            | uint32_t(data[pos + 3]) << 24;
        pos += 4;
        return v;
    }
    Bytes take(size_t n)
    {
        if (!has(n))
            return {};
        Bytes out(data.begin() + std::ptrdiff_t(pos), data.begin() + std::ptrdiff_t(pos + n));
        pos += n;
        return out;
    }
    std::string string()
    {
        const uint16_t n = word();
        const Bytes b = take(n);
        return std::string(b.begin(), b.end());
    }
};

size_t plainSize(uint16_t type)
{
    switch (type) {
    case Null: return 0;
    case Bool: case Int8: case UInt8: return 1;
    case Int16: case UInt16: return 2;
    case Int32: case UInt32: case Fixed: case Float: return 4;
    case Int64: case UInt64: case Double: case Point: case Size: return 8;
    case Rect: case Uuid: return 16;
    default: return 0;
    }
}

Fields readFields(Cursor& in, int depth);

Value readValue(Cursor& in, uint16_t type, int depth)
{
    Value v;
    v.type = type;
    if (depth > 16) {
        in.ok = false;
        return v;
    }
    if (type == String) {
        const uint16_t n = in.word();
        v.raw = {uint8_t(n & 0xff), uint8_t(n >> 8)};
        const Bytes text = in.take(n);
        v.raw.insert(v.raw.end(), text.begin(), text.end());
    } else if (type == Vector) {
        const uint32_t n = in.dword();
        v.elementType = in.word();
        for (uint32_t i = 0; i < n && in.ok; ++i) {
            const uint16_t t = v.elementType ? v.elementType : in.word();
            v.items.push_back(readValue(in, t, depth + 1));
            if (!v.elementType)
                v.items.back().type = t;
        }
    } else if (type == Nested) {
        v.fields = readFields(in, depth + 1);
    } else if (type <= Uuid) {
        v.raw = in.take(plainSize(type));
    } else {
        in.ok = false; // a type we don't know has no known size
    }
    return v;
}

Fields readFields(Cursor& in, int depth)
{
    Fields out;
    const uint32_t n = in.dword();
    for (uint32_t i = 0; i < n && in.ok; ++i) {
        std::string name = in.string();
        const uint16_t type = in.word();
        out.emplace_back(std::move(name), readValue(in, type, depth));
    }
    return out;
}

void putWord(Bytes& out, uint16_t v) { out.push_back(uint8_t(v & 0xff)); out.push_back(uint8_t(v >> 8)); }
void putDword(Bytes& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); }
void putString(Bytes& out, const std::string& s)
{
    putWord(out, uint16_t(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void writeFields(Bytes& out, const Fields& fields);

void writeValue(Bytes& out, const Value& v)
{
    if (v.type == Vector) {
        putDword(out, uint32_t(v.items.size()));
        putWord(out, v.elementType);
        for (const Value& item : v.items) {
            if (!v.elementType)
                putWord(out, item.type);
            writeValue(out, item);
        }
    } else if (v.type == Nested) {
        writeFields(out, v.fields);
    } else {
        out.insert(out.end(), v.raw.begin(), v.raw.end());
    }
}

void writeFields(Bytes& out, const Fields& fields)
{
    putDword(out, uint32_t(fields.size()));
    for (const auto& [name, value] : fields) {
        putString(out, name);
        putWord(out, value.type);
        writeValue(out, value);
    }
}

// A user data chunk split into what comes before its properties and the property maps.
struct UserData {
    Bytes head; // flags (with the properties bit as it was), text and color
    std::vector<Map> maps;
    bool parsed = false;
};

UserData parseUserData(const Bytes& data)
{
    UserData u;
    if (data.empty()) {
        u.head = Bytes(4, 0);
        u.parsed = true;
        return u;
    }
    Cursor in{data, 0};
    const uint32_t flags = in.dword();
    if (flags & 1)
        in.string();
    if (flags & 2)
        in.take(4);
    if (!in.ok)
        return u;
    u.head.assign(data.begin(), data.begin() + std::ptrdiff_t(in.pos));
    if (flags & 4) {
        in.dword(); // size of the property maps
        const uint32_t count = in.dword();
        for (uint32_t i = 0; i < count && in.ok; ++i) {
            Map m;
            m.key = in.dword();
            m.fields = readFields(in, 0);
            u.maps.push_back(std::move(m));
        }
        if (!in.ok)
            return u;
    }
    u.parsed = true;
    return u;
}

Bytes serialize(const UserData& u)
{
    Bytes out = u.head;
    if (u.maps.empty()) {
        out[0] = uint8_t(out[0] & ~4);
        return out;
    }
    out[0] = uint8_t(out[0] | 4);
    Bytes maps;
    for (const Map& m : u.maps) {
        putDword(maps, m.key);
        writeFields(maps, m.fields);
    }
    putDword(out, uint32_t(maps.size() + 8)); // counts this field and the number of maps
    putDword(out, uint32_t(u.maps.size()));
    out.insert(out.end(), maps.begin(), maps.end());
    return out;
}

Map* userMap(UserData& u, bool create)
{
    for (Map& m : u.maps)
        if (m.key == 0)
            return &m;
    if (!create)
        return nullptr;
    u.maps.insert(u.maps.begin(), Map{});
    return &u.maps.front();
}

// --- the recipe as property values ------------------------------------------------------------------------

Value number(uint16_t type, uint64_t bits, size_t bytes)
{
    Value v;
    v.type = type;
    for (size_t i = 0; i < bytes; ++i)
        v.raw.push_back(uint8_t(bits >> (8 * i)));
    return v;
}
Value i32(int v) { return number(Int32, uint32_t(v), 4); }
Value u32(uint32_t v) { return number(UInt32, v, 4); }
Value u64(uint64_t v) { return number(UInt64, v, 8); }
Value flag(bool v) { return number(Bool, v ? 1 : 0, 1); }
Value real(double v)
{
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    return number(Double, bits, 8);
}

uint32_t packColor(Rgba c) { return uint32_t(c.r) << 24 | uint32_t(c.g) << 16 | uint32_t(c.b) << 8 | c.a; }
Rgba unpackColor(uint32_t v) { return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }

// Every setting of a recipe as nested properties; a sub-particle's recipe goes in "sub".
Value recipeFields(const ParticleRecipe& r, bool child)
{
    Value v;
    v.type = Nested;
    auto& f = v.fields;
    if (r.presetScale != 1.0)
        f.emplace_back("presetScale", real(r.presetScale));
    if (!r.preset.empty()) {
        Value preset;
        preset.type = String;
        preset.raw = {uint8_t(r.preset.size() & 0xff), uint8_t(r.preset.size() >> 8)};
        preset.raw.insert(preset.raw.end(), r.preset.begin(), r.preset.end());
        f.emplace_back("preset", std::move(preset));
    }
    f.emplace_back("emission", i32(int(r.emission)));
    f.emplace_back("count", i32(r.count));
    f.emplace_back("startFrame", i32(r.startFrame));
    f.emplace_back("endFrame", i32(r.endFrame));
    f.emplace_back("loop", flag(r.loop));
    f.emplace_back("perFrame", flag(r.perFrame));
    f.emplace_back("rate", real(r.rate));
    f.emplace_back("untilEnd", flag(r.untilEnd));
    f.emplace_back("seed", u32(r.seed));
    f.emplace_back("lifetime", i32(r.lifetime));
    f.emplace_back("lifetimeRandom", i32(r.lifetimeRandom));
    f.emplace_back("x", real(r.x));
    f.emplace_back("y", real(r.y));
    f.emplace_back("direction", real(r.direction));
    f.emplace_back("spread", real(r.spread));
    f.emplace_back("speed", real(r.speed));
    f.emplace_back("speedRandom", i32(r.speedRandom));
    f.emplace_back("gravityX", real(r.gravityX));
    f.emplace_back("gravityY", real(r.gravityY));
    f.emplace_back("drag", real(r.drag));
    f.emplace_back("sizeStart", real(r.sizeStart));
    f.emplace_back("sizeEnd", real(r.sizeEnd));
    f.emplace_back("sizeRandom", i32(r.sizeRandom));
    f.emplace_back("sizeEase", i32(int(r.sizeEase)));
    const auto curveValue = [](const EasingCurve& c) {
        Value v;
        v.type = Vector;
        v.elementType = Double;
        for (const double n : {c.x1, c.y1, c.x2, c.y2})
            v.items.push_back(real(n));
        return v;
    };
    if (r.sizeEase == ParticleRecipe::Ease::Custom)
        f.emplace_back("sizeCurve", curveValue(r.sizeCurve));
    if (r.opacityEase == ParticleRecipe::Ease::Custom)
        f.emplace_back("opacityCurve", curveValue(r.opacityCurve));
    f.emplace_back("shape", i32(int(r.shape)));
    f.emplace_back("colorMode", i32(int(r.colorMode)));
    f.emplace_back("opacityStart", i32(r.opacityStart));
    f.emplace_back("opacityEnd", i32(r.opacityEnd));
    f.emplace_back("opacityEase", i32(int(r.opacityEase)));
    f.emplace_back("fadePattern", i32(r.fadePattern));
    Value colors;
    colors.type = Vector;
    colors.elementType = UInt32;
    for (const Rgba& c : r.colors)
        colors.items.push_back(u32(packColor(c)));
    f.emplace_back("colors", std::move(colors));
    if (!r.fields.empty()) {
        Value fields;
        fields.type = Vector;
        fields.elementType = Nested;
        for (const ParticleField& field : r.fields) {
            Value item;
            item.type = Nested;
            item.fields.emplace_back("angle", real(field.angle));
            item.fields.emplace_back("bounciness", i32(field.bounciness));
            item.fields.emplace_back("friction", i32(field.friction));
            item.fields.emplace_back("stack", flag(field.stack));
            item.fields.emplace_back("mode", i32(field.mode));
            item.fields.emplace_back("falloff", i32(field.falloff));
            item.fields.emplace_back("radius", i32(field.radius));
            item.fields.emplace_back("scale", i32(field.scale));
            item.fields.emplace_back("strength", real(field.strength));
            item.fields.emplace_back("turbulence", i32(field.turbulence));
            item.fields.emplace_back("type", i32(int(field.type)));
            item.fields.emplace_back("x", real(field.x));
            item.fields.emplace_back("y", real(field.y));
            if (!field.points.empty()) { // a collider's corners: x, y, x, y, ...
                Value points;
                points.type = Vector;
                points.elementType = Double;
                for (const ParticlePoint& p : field.points) {
                    points.items.push_back(real(p.x));
                    points.items.push_back(real(p.y));
                }
                item.fields.emplace_back("points", std::move(points));
            }
            fields.items.push_back(std::move(item));
        }
        f.emplace_back("fields", std::move(fields));
    }
    if (!r.brush.empty()) {
        f.emplace_back("brushWidth", i32(r.brush.width));
        f.emplace_back("brushHeight", i32(r.brush.height));
        Value pixels;
        pixels.type = Vector;
        pixels.elementType = UInt32;
        for (const uint32_t p : r.brush.pixels)
            pixels.items.push_back(u32(p));
        f.emplace_back("brush", std::move(pixels));
    }
    f.emplace_back("source", i32(int(r.source)));
    f.emplace_back("sourceWidth", i32(r.sourceWidth));
    f.emplace_back("sourceHeight", i32(r.sourceHeight));
    f.emplace_back("sourceAngle", real(r.sourceAngle));
    f.emplace_back("sourceFill", flag(r.sourceFill));
    f.emplace_back("brushColors", flag(r.brushColors));
    f.emplace_back("angle", real(r.angle));
    f.emplace_back("angleRandom", i32(r.angleRandom));
    f.emplace_back("spin", real(r.spin));
    f.emplace_back("spinRandom", i32(r.spinRandom));
    f.emplace_back("faceMotion", flag(r.faceMotion));
    f.emplace_back("angleSnap", i32(r.angleSnap));
    if (!child) {
        f.emplace_back("subTrigger", i32(int(r.subTrigger)));
        f.emplace_back("subInherit", i32(r.subInherit));
        if (!r.children.empty())
            f.emplace_back("sub", recipeFields(r.children.front(), true));
    }
    return v;
}

Value recipeValue(const ParticleRecipe& r, uint64_t hash)
{
    Value v = recipeFields(r, false);
    v.fields.insert(v.fields.begin(), {"hash", u64(hash)});
    v.fields.insert(v.fields.begin(), {"kind", [] { Value s; s.type = String; s.raw = {8, 0, 'p', 'a', 'r', 't', 'i', 'c', 'l', 'e'}; return s; }()});
    v.fields.insert(v.fields.begin(), {"version", i32(kVersion)});
    return v;
}

Value text(const std::string& s)
{
    Value v;
    v.type = String;
    v.raw = {uint8_t(s.size() & 0xff), uint8_t(s.size() >> 8)};
    v.raw.insert(v.raw.end(), s.begin(), s.end());
    return v;
}

Value smearValue(const SmearRecipe& r)
{
    Value v;
    v.type = Nested;
    auto& f = v.fields;
    f.emplace_back("version", i32(kVersion));
    f.emplace_back("kind", text("smear"));
    f.emplace_back("pivotX", real(r.pivotX));
    f.emplace_back("pivotY", real(r.pivotY));
    f.emplace_back("trail", real(r.trail));
    f.emplace_back("behind", flag(r.behind));
    f.emplace_back("style", i32(int(r.style)));
    f.emplace_back("color", u32(packColor(r.color)));
    f.emplace_back("taper", real(r.taper));
    f.emplace_back("fade", real(r.fade));
    f.emplace_back("fadePattern", i32(r.fadePattern));
    f.emplace_back("outline", flag(r.outline));
    f.emplace_back("outlineColor", u32(packColor(r.outlineColor)));
    f.emplace_back("smoothPath", flag(r.smoothPath));
    f.emplace_back("facePath", flag(r.facePath));
    Value keys;
    keys.type = Vector;
    keys.elementType = Nested;
    for (const SmearKey& k : r.keys) {
        Value item;
        item.type = Nested;
        item.fields.emplace_back("frame", i32(k.frame));
        item.fields.emplace_back("ease", i32(int(k.ease)));
        if (k.ease == SmearKey::Ease::Custom) {
            Value curve;
            curve.type = Vector;
            curve.elementType = Double;
            for (const double n : {k.curve.x1, k.curve.y1, k.curve.x2, k.curve.y2})
                curve.items.push_back(real(n));
            item.fields.emplace_back("curve", std::move(curve));
        }
        item.fields.emplace_back("x", real(k.pose.x));
        item.fields.emplace_back("y", real(k.pose.y));
        item.fields.emplace_back("rotation", real(k.pose.rotation));
        item.fields.emplace_back("scaleX", real(k.pose.scaleX));
        item.fields.emplace_back("scaleY", real(k.pose.scaleY));
        item.fields.emplace_back("skewX", real(k.pose.skewX));
        item.fields.emplace_back("skewY", real(k.pose.skewY));
        keys.items.push_back(std::move(item));
    }
    f.emplace_back("keys", std::move(keys));
    return v;
}

const Value* find(const Fields& fields, const char* name)
{
    for (const auto& [n, v] : fields)
        if (n == name)
            return &v;
    return nullptr;
}

std::optional<double> numberOf(const Value& v)
{
    uint64_t bits = 0;
    for (size_t i = 0; i < v.raw.size() && i < 8; ++i)
        bits |= uint64_t(v.raw[i]) << (8 * i);
    switch (v.type) {
    case Bool: case UInt8: case UInt16: case UInt32: case UInt64: return double(bits);
    case Int8: return double(int8_t(bits));
    case Int16: return double(int16_t(bits));
    case Int32: return double(int32_t(bits));
    case Int64: return double(int64_t(bits));
    case Float: { float f; const uint32_t b = uint32_t(bits); std::memcpy(&f, &b, 4); return double(f); }
    case Double: { double d; std::memcpy(&d, &bits, 8); return d; }
    default: return std::nullopt;
    }
}

} // namespace

uint64_t celsHash(const Document& doc, const Layer& layer)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a
    const auto mix = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            h = (h ^ uint8_t(v >> (8 * i))) * 1099511628211ull;
    };
    mix(0xffffffffu);
    for (const CelRef& cel : layer.cels) {
        mix(0xfffffffeu); // a frame starts
        if (!cel || !cel->image || cel->image->isNull())
            continue;
        const Image& image = *cel->image;
        for (int y = 0; y < image.height(); ++y) {
            const int cy = y + cel->position.y;
            if (cy < 0 || cy >= doc.height())
                continue;
            for (int x = 0; x < image.width(); ++x) {
                const int cx = x + cel->position.x;
                if (cx < 0 || cx >= doc.width())
                    continue;
                const Rgba c = toRgba(doc, image, x, y);
                if (c.a == 0)
                    continue;
                mix(uint32_t(cy * doc.width() + cx));
                mix(packColor(c));
            }
        }
    }
    return h;
}

Bytes withStoredRecipe(Bytes userData, const ParticleRecipe& recipe, uint64_t hash)
{
    UserData u = parseUserData(userData);
    if (!u.parsed)
        return userData;
    Map* map = userMap(u, true);
    Value value = recipeValue(recipe, hash);
    for (auto& [name, v] : map->fields)
        if (name == kProperty) {
            v = std::move(value);
            return serialize(u);
        }
    map->fields.emplace_back(kProperty, std::move(value));
    // Aseprite keeps a map's properties sorted by name.
    std::sort(map->fields.begin(), map->fields.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return serialize(u);
}

Bytes withStoredSmear(Bytes userData, const SmearRecipe& recipe)
{
    UserData u = parseUserData(userData);
    if (!u.parsed)
        return userData;
    Map* map = userMap(u, true);
    Value value = smearValue(recipe);
    for (auto& [name, v] : map->fields)
        if (name == kProperty) {
            v = std::move(value);
            return serialize(u);
        }
    map->fields.emplace_back(kProperty, std::move(value));
    std::sort(map->fields.begin(), map->fields.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return serialize(u);
}

Bytes withoutStoredRecipe(Bytes userData)
{
    UserData u = parseUserData(userData);
    if (!u.parsed)
        return userData;
    Map* map = userMap(u, false);
    if (!map)
        return userData;
    const size_t before = map->fields.size();
    std::erase_if(map->fields, [](const auto& f) { return f.first == kProperty; });
    if (map->fields.size() == before)
        return userData;
    if (map->fields.empty())
        std::erase_if(u.maps, [](const Map& m) { return m.key == 0; });
    Bytes out = serialize(u);
    // User data with nothing left in it is no user data.
    return out.size() == 4 && out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0 ? Bytes{} : out;
}

// The settings in `f` (see recipeFields) into `r`, each clamped to what it may be; what is missing
// keeps its built-in value.
// A custom easing's curve, [x1, y1, x2, y2], kept as it was when missing or malformed.
static void readCurve(const Value* v, EasingCurve& curve)
{
    if (!v || v->type != Vector || v->items.size() != 4)
        return;
    double n[4];
    for (int i = 0; i < 4; ++i) {
        const auto x = numberOf(v->items[size_t(i)]);
        if (!x)
            return;
        n[i] = *x;
    }
    curve = {std::clamp(n[0], 0.0, 1.0), std::clamp(n[1], -1.0, 2.0), std::clamp(n[2], 0.0, 1.0), std::clamp(n[3], -1.0, 2.0)};
}

static void readRecipe(const Fields& f, ParticleRecipe& r, bool child)
{
    const auto get = [&](const char* name, auto& field, auto lo, auto hi) {
        if (const Value* v = find(f, name))
            if (const auto n = numberOf(*v))
                field = static_cast<std::remove_reference_t<decltype(field)>>(std::clamp(*n, double(lo), double(hi)));
    };
    int emission = int(r.emission), shape = int(r.shape), colorMode = int(r.colorMode), ease = int(r.sizeEase);
    int loop = r.loop;
    double seed = r.seed;
    get("emission", emission, 0, 1);
    get("count", r.count, 1, 5000);
    get("startFrame", r.startFrame, -5000, 100000);
    get("endFrame", r.endFrame, 0, 100000);
    get("loop", loop, 0, 1);
    int perFrame = 0, untilEnd = 0;
    get("perFrame", perFrame, 0, 1);
    get("rate", r.rate, 0.05, 500);
    get("untilEnd", untilEnd, 0, 1);
    r.perFrame = perFrame != 0;
    r.untilEnd = untilEnd != 0;
    get("seed", seed, 0, 4294967295.0);
    get("lifetime", r.lifetime, 1, 1000);
    get("lifetimeRandom", r.lifetimeRandom, 0, 100);
    get("x", r.x, -1e6, 1e6);
    get("y", r.y, -1e6, 1e6);
    get("direction", r.direction, -360, 360);
    get("spread", r.spread, 0, 360);
    get("speed", r.speed, 0, 200);
    get("speedRandom", r.speedRandom, 0, 100);
    get("gravityX", r.gravityX, -20, 20);
    get("gravityY", r.gravityY, -20, 20);
    get("drag", r.drag, 0, 1);
    get("sizeStart", r.sizeStart, 0, 256);
    get("sizeEnd", r.sizeEnd, 0, 256);
    get("sizeRandom", r.sizeRandom, 0, 100);
    get("sizeEase", ease, 0, 4);
    get("shape", shape, 0, 4);
    get("colorMode", colorMode, 0, 1);
    int opacityEase = int(r.opacityEase);
    get("opacityStart", r.opacityStart, 0, 100);
    get("opacityEnd", r.opacityEnd, 0, 100);
    get("opacityEase", opacityEase, 0, 4);
    readCurve(find(f, "sizeCurve"), r.sizeCurve);
    readCurve(find(f, "opacityCurve"), r.opacityCurve);
    get("fadePattern", r.fadePattern, 0, 4);
    r.opacityEase = ParticleRecipe::Ease(opacityEase);
    r.emission = ParticleRecipe::Emission(emission);
    r.loop = loop != 0;
    r.seed = uint32_t(seed);
    r.shape = ParticleRecipe::Shape(shape);
    r.colorMode = ParticleRecipe::ColorMode(colorMode);
    r.sizeEase = ParticleRecipe::Ease(ease);
    if (r.endFrame < r.startFrame)
        r.endFrame = r.startFrame;

    if (const Value* preset = find(f, "preset"); preset && preset->type == String && preset->raw.size() >= 2)
        r.preset.assign(preset->raw.begin() + 2, preset->raw.end());
    if (const Value* colors = find(f, "colors"); colors && colors->type == Vector) {
        std::vector<Rgba> list;
        for (const Value& item : colors->items)
            if (const auto n = numberOf(item))
                list.push_back(unpackColor(uint32_t(uint64_t(*n))));
        if (!list.empty())
            r.colors = std::move(list);
    }
    if (const Value* fields = find(f, "fields"); fields && fields->type == Vector) {
        for (const Value& item : fields->items) {
            if (item.type != Nested || r.fields.size() >= 8)
                continue;
            ParticleField field;
            const auto read = [&](const char* name, auto& target, double lo, double hi) {
                if (const Value* v = find(item.fields, name))
                    if (const auto n = numberOf(*v))
                        target = static_cast<std::remove_reference_t<decltype(target)>>(std::clamp(*n, lo, hi));
            };
            read("x", field.x, -4096, 4096);
            read("y", field.y, -4096, 4096);
            read("strength", field.strength, -10, 10);
            read("radius", field.radius, 0, 4096);
            read("falloff", field.falloff, 0, 1);
            read("angle", field.angle, -360, 360);
            read("turbulence", field.turbulence, 0, 100);
            read("scale", field.scale, 1, 512);
            read("mode", field.mode, 0, 2);
            read("bounciness", field.bounciness, 0, 100);
            read("friction", field.friction, 0, 100);
            int stack = 0;
            read("stack", stack, 0, 1);
            field.stack = stack != 0;
            int type = 0;
            read("type", type, 0, 4);
            field.type = ParticleField::Type(type);
            if (const Value* points = find(item.fields, "points"); points && points->type == Vector) {
                for (size_t i = 0; i + 1 < points->items.size() && field.points.size() < 32; i += 2) {
                    const auto x = numberOf(points->items[i]), y = numberOf(points->items[i + 1]);
                    if (x && y)
                        field.points.push_back({std::clamp(*x, -8192.0, 8192.0), std::clamp(*y, -8192.0, 8192.0)});
                }
            }
            if (field.type == ParticleField::Type::Collider && field.points.size() < 2)
                continue; // a collider needs a line
            r.fields.push_back(field);
        }
    }
    int brushWidth = 0, brushHeight = 0;
    get("brushWidth", brushWidth, 0, 128);
    get("brushHeight", brushHeight, 0, 128);
    if (const Value* brush = find(f, "brush"); brush && brush->type == Vector && brushWidth > 0 && brushHeight > 0
        && brush->items.size() == size_t(brushWidth) * size_t(brushHeight)) {
        r.brush.width = brushWidth;
        r.brush.height = brushHeight;
        for (const Value& item : brush->items)
            r.brush.pixels.push_back(uint32_t(uint64_t(numberOf(item).value_or(0))));
    }
    get("presetScale", r.presetScale, 0.1, 10);
    int source = int(r.source), sourceFill = r.sourceFill;
    get("source", source, 0, 3);
    get("sourceWidth", r.sourceWidth, 0, 4096);
    get("sourceHeight", r.sourceHeight, 0, 4096);
    get("sourceAngle", r.sourceAngle, -360, 360);
    get("sourceFill", sourceFill, 0, 1);
    r.source = ParticleRecipe::Source(source);
    r.sourceFill = sourceFill != 0;
    int brushColors = 0, faceMotion = 0;
    get("brushColors", brushColors, 0, 1);
    get("angle", r.angle, -360, 360);
    get("angleRandom", r.angleRandom, 0, 180);
    get("spin", r.spin, -360, 360);
    get("spinRandom", r.spinRandom, 0, 100);
    get("faceMotion", faceMotion, 0, 1);
    get("angleSnap", r.angleSnap, 0, 3);
    r.brushColors = brushColors != 0;
    r.faceMotion = faceMotion != 0;
    int trigger = 0;
    get("subTrigger", trigger, 0, 2);
    get("subInherit", r.subInherit, 0, 100);
    if (!child) {
        r.subTrigger = ParticleRecipe::Trigger(trigger);
        if (const Value* sub = find(f, "sub"); sub && sub->type == Nested) {
            ParticleRecipe c;
            readRecipe(sub->fields, c, true);
            r.children = {c};
        }
    }
}

std::optional<StoredRecipe> storedRecipeIn(const Bytes& userData)
{
    UserData u = parseUserData(userData);
    if (!u.parsed)
        return std::nullopt;
    const Map* map = userMap(u, false);
    const Value* stored = map ? find(map->fields, kProperty) : nullptr;
    if (!stored || stored->type != Nested)
        return std::nullopt;
    const Fields& f = stored->fields;
    const auto version = find(f, "version");
    const auto kind = find(f, "kind");
    if (!version || !kind || kind->raw.size() < 10 || std::string(kind->raw.begin() + 2, kind->raw.end()) != "particle")
        return std::nullopt;
    if (numberOf(*version).value_or(0) > kVersion)
        return std::nullopt; // from a newer Smalto: leave it alone

    StoredRecipe out;
    readRecipe(f, out.recipe, false);
    if (const Value* h = find(f, "hash"); h && h->type == UInt64) {
        uint64_t bits = 0;
        for (size_t i = 0; i < h->raw.size() && i < 8; ++i)
            bits |= uint64_t(h->raw[i]) << (8 * i);
        out.hash = bits;
    }
    return out;
}

std::optional<SmearRecipe> storedSmearIn(const Bytes& userData)
{
    UserData u = parseUserData(userData);
    if (!u.parsed)
        return std::nullopt;
    const Map* map = userMap(u, false);
    const Value* stored = map ? find(map->fields, kProperty) : nullptr;
    if (!stored || stored->type != Nested)
        return std::nullopt;
    const Fields& f = stored->fields;
    const Value* version = find(f, "version");
    const Value* kind = find(f, "kind");
    if (!version || !kind || kind->raw.size() < 2 || std::string(kind->raw.begin() + 2, kind->raw.end()) != "smear")
        return std::nullopt;
    if (numberOf(*version).value_or(0) > kVersion)
        return std::nullopt; // from a newer Smalto: leave it alone
    const auto read = [](const Fields& in, const char* name, auto& target, double lo, double hi) {
        if (const Value* v = find(in, name))
            if (const auto n = numberOf(*v))
                target = static_cast<std::remove_reference_t<decltype(target)>>(std::clamp(*n, lo, hi));
    };
    SmearRecipe r;
    read(f, "pivotX", r.pivotX, -1e6, 1e6);
    read(f, "pivotY", r.pivotY, -1e6, 1e6);
    read(f, "trail", r.trail, 0, 1);
    int behind = 1, style = 0, outline = 0;
    double color = packColor(r.color), outlineColor = packColor(r.outlineColor);
    read(f, "behind", behind, 0, 1);
    read(f, "style", style, 0, 1);
    read(f, "color", color, 0, 4294967295.0);
    read(f, "taper", r.taper, 0, 1);
    read(f, "fade", r.fade, 0, 1);
    read(f, "fadePattern", r.fadePattern, 0, 4);
    read(f, "outline", outline, 0, 1);
    read(f, "outlineColor", outlineColor, 0, 4294967295.0);
    r.behind = behind != 0;
    r.style = SmearRecipe::Style(style);
    r.color = unpackColor(uint32_t(color));
    r.outline = outline != 0;
    r.outlineColor = unpackColor(uint32_t(outlineColor));
    int smoothPath = 0, facePath = 0;
    read(f, "smoothPath", smoothPath, 0, 1);
    read(f, "facePath", facePath, 0, 1);
    r.smoothPath = smoothPath != 0;
    r.facePath = facePath != 0;
    if (const Value* keys = find(f, "keys"); keys && keys->type == Vector)
        for (const Value& item : keys->items) {
            if (item.type != Nested || r.keys.size() >= 1000)
                continue;
            SmearKey k;
            int ease = 0;
            read(item.fields, "frame", k.frame, -100000, 100000);
            read(item.fields, "ease", ease, 0, 4);
            k.ease = SmearKey::Ease(ease);
            readCurve(find(item.fields, "curve"), k.curve);
            read(item.fields, "x", k.pose.x, -1e6, 1e6);
            read(item.fields, "y", k.pose.y, -1e6, 1e6);
            read(item.fields, "rotation", k.pose.rotation, -1e6, 1e6);
            read(item.fields, "scaleX", k.pose.scaleX, -1000, 1000);
            read(item.fields, "scaleY", k.pose.scaleY, -1000, 1000);
            read(item.fields, "skewX", k.pose.skewX, -89, 89);
            read(item.fields, "skewY", k.pose.skewY, -89, 89);
            if (r.keys.empty() || k.frame > r.keys.back().frame) // sorted, one per frame
                r.keys.push_back(k);
        }
    return r;
}

void restoreLiveLayers(Document& doc)
{
    doc.forEachLayer([&](Layer& layer) {
        if (layer.kind == LayerKind::Group && !layer.userData.empty()) {
            layer.smear = storedSmearIn(layer.userData);
            return;
        }
        if (layer.kind != LayerKind::Image || layer.userData.empty())
            return;
        const auto stored = storedRecipeIn(layer.userData);
        if (!stored)
            return;
        if (stored->hash == celsHash(doc, layer))
            layer.particles = stored->recipe;
        else
            layer.staleRecipe = stored->recipe;
    });
}

} // namespace formats
