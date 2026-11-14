#include "core/Compositor.h"
#include "core/Raster.h"
#include "core/Smear.h"
#include "core/Tilemap.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <vector>

namespace core {

Rgba toRgba(const Document& doc, const Image& image, int x, int y, bool background)
{
    switch (image.format()) {
    case PixelFormat::Rgba8:
        return unpack(image.pixel(x, y));
    case PixelFormat::Indexed8: {
        const int index = int(image.pixel(x, y));
        return index == doc.transparentIndex && !background ? Rgba{} : doc.palette.color(index);
    }
    case PixelFormat::Gray16: {
        const uint32_t v = image.pixel(x, y);
        const uint8_t g = uint8_t(v);
        return {g, g, g, uint8_t(v >> 8)};
    }
    case PixelFormat::Tilemap32:
        return {}; // TODO: tilemaps
    }
    return {};
}

uint32_t fromRgba(const Document& doc, Rgba c)
{
    return fromRgba(doc.colorMode(), doc.palette, doc.transparentIndex, c);
}

uint32_t fromRgba(ColorMode mode, const Palette& palette, int transparentIndex, Rgba c, bool background)
{
    switch (mode) {
    case ColorMode::Rgba:
        return c.a == 0 ? 0 : pack(c);
    case ColorMode::Grayscale:
        if (c.a == 0)
            return 0;
        return uint32_t((c.r * 30 + c.g * 59 + c.b * 11 + 50) / 100) | uint32_t(c.a) << 8;
    case ColorMode::Indexed: {
        if (c.a == 0)
            return uint32_t(transparentIndex);
        int best = -1;
        int bestDistance = 0;
        for (int i = 0; i < palette.size(); ++i) {
            if (i == transparentIndex && !background)
                continue;
            const Rgba p = palette.color(i);
            const int dr = p.r - c.r, dg = p.g - c.g, db = p.b - c.b, da = p.a - c.a;
            const int d = dr * dr + dg * dg + db * db + da * da; // equal for all when the palette is opaque
            if (best < 0 || d < bestDistance) {
                best = i;
                bestDistance = d;
            }
        }
        return uint32_t(best < 0 ? transparentIndex : best);
    }
    }
    return 0;
}

Image convertToDocument(const Document& doc, const Image& rgba)
{
    assert(rgba.format() == PixelFormat::Rgba8);
    if (doc.pixelFormat() == PixelFormat::Rgba8)
        return rgba;
    Image out(rgba.width(), rgba.height(), doc.pixelFormat());
    std::unordered_map<uint32_t, uint32_t> cache; // nearest-color search is slow
    for (int y = 0; y < rgba.height(); ++y)
        for (int x = 0; x < rgba.width(); ++x) {
            const uint32_t v = rgba.pixel(x, y);
            auto it = cache.find(v);
            if (it == cache.end())
                it = cache.emplace(v, fromRgba(doc, unpack(v))).first;
            out.setPixel(x, y, it->second);
        }
    return out;
}

namespace {

// The color an index of an indexed image stands for (what toRgba gives for it).
Rgba toRgba(const Document& doc, int index, bool background)
{
    return index == doc.transparentIndex && !background ? Rgba{} : doc.palette.color(index);
}

// Blends `src` (positioned at srcPos in document space) onto dst, whose
// pixel (0, 0) is at dstOrigin in document space.
void compose(const Document& doc, const Image& src, Point srcPos, Image& dst, Point dstOrigin,
             Rect area, int opacity, BlendMode mode, bool background = false)
{
    const Rect r = Rect{srcPos.x, srcPos.y, src.width(), src.height()}.intersected(area);
    if (r.isEmpty() || opacity == 0)
        return;
    const bool opaqueNormal = mode == BlendMode::Normal && opacity == 255;
    const auto put = [&](uint32_t* out, Rgba s) {
        if (s.a == 0)
            return;
        if (opaqueNormal && s.a == 255)
            *out = pack(s);
        else
            *out = pack(blend(unpack(*out), s, opacity, mode));
    };
    const int sx = r.x - srcPos.x;
    const int dx = r.x - dstOrigin.x;
    if (src.format() == PixelFormat::Rgba8) {
        for (int y = r.y; y < r.bottom(); ++y) {
            const auto* in = reinterpret_cast<const uint32_t*>(src.row(y - srcPos.y)) + sx;
            auto* out = reinterpret_cast<uint32_t*>(dst.row(y - dstOrigin.y)) + dx;
            for (int i = 0; i < r.w; ++i) {
                const uint32_t v = in[i];
                if ((v >> 24) == 0)
                    continue; // transparent: the common case in sparse layers
                if (opaqueNormal && (v >> 24) == 255)
                    out[i] = v;
                else
                    put(out + i, unpack(v));
            }
        }
        return;
    }
    if (src.format() == PixelFormat::Indexed8) {
        // One palette lookup per index instead of one per pixel.
        Rgba lut[256];
        for (int i = 0; i < 256; ++i)
            lut[i] = toRgba(doc, i, background);
        for (int y = r.y; y < r.bottom(); ++y) {
            const uint8_t* in = src.row(y - srcPos.y) + sx;
            auto* out = reinterpret_cast<uint32_t*>(dst.row(y - dstOrigin.y)) + dx;
            for (int i = 0; i < r.w; ++i)
                put(out + i, lut[in[i]]);
        }
        return;
    }
    for (int y = r.y; y < r.bottom(); ++y) {
        auto* out = reinterpret_cast<uint32_t*>(dst.row(y - dstOrigin.y)) + dx;
        for (int i = 0; i < r.w; ++i)
            put(out + i, toRgba(doc, src, sx + i, y - srcPos.y, background));
    }
}

void renderLayer(const Document& doc, const Layer& layer, int frame, Image& dst, Point origin,
                 Rect area, bool asEdited);

int zIndex(const Layer& layer, int frame)
{
    const CelRef cel = layer.hasCels() ? layer.cel(frame) : nullptr;
    return cel ? cel->zIndex : 0;
}

// Renders a group's children bottom to top. A cel's z-index moves it that
// many places up or down among its siblings; on a tie the lower z-index
// goes first.
void renderChildren(const Document& doc, const Layer& group, int frame, Image& dst, Point origin,
                    Rect area, bool asEdited = false)
{
    const auto& children = group.children;
    const bool reorder = std::any_of(children.begin(), children.end(),
                                     [&](const auto& c) { return zIndex(*c, frame) != 0; });
    if (!reorder) {
        for (const auto& child : children)
            renderLayer(doc, *child, frame, dst, origin, area, asEdited);
        return;
    }
    struct Item {
        int order;
        int z;
        const Layer* layer;
    };
    std::vector<Item> items;
    for (size_t i = 0; i < children.size(); ++i) {
        const int z = zIndex(*children[i], frame);
        items.push_back({int(i) + z, z, children[i].get()});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.order != b.order ? a.order < b.order : a.z < b.z;
    });
    for (const Item& item : items)
        renderLayer(doc, *item.layer, frame, dst, origin, area, asEdited);
}

// Each cell of the tilemap cel draws its tile from the layer's tileset.
void renderTilemap(const Document& doc, const Layer& layer, const CelData& cel, Image& dst, Point origin, Rect area)
{
    if (layer.tilesetIndex >= doc.tilesets.size())
        return;
    const Tileset& set = doc.tilesets[layer.tilesetIndex];
    const Image& map = *cel.image;
    const int opacity = mul255(cel.opacity, layer.opacity);
    for (int cy = 0; cy < map.height(); ++cy)
        for (int cx = 0; cx < map.width(); ++cx) {
            const uint32_t v = map.pixel(cx, cy);
            const uint32_t id = v & kTileIdMask;
            if (id == 0 || id >= set.tiles.size() || !set.tiles[id])
                continue; // the empty tile, or one the tileset hasn't got
            const Point at{cel.position.x + cx * set.tileWidth, cel.position.y + cy * set.tileHeight};
            const Rect cell = Rect{at.x, at.y, set.tileWidth, set.tileHeight}.intersected(area);
            if (cell.isEmpty())
                continue;
            const uint32_t flips = v & ~kTileIdMask;
            if (flips)
                compose(doc, orientTile(*set.tiles[id], flips), at, dst, origin, cell, opacity, layer.blend);
            else
                compose(doc, *set.tiles[id], at, dst, origin, cell, opacity, layer.blend);
        }
}

// A Smear group's frame: its children over the whole canvas (the posed art may come from anywhere),
// posed and smeared.
Image renderSmearGroup(const Document& doc, const Layer& group, int frame)
{
    Image art(doc.width(), doc.height());
    art.clear(0);
    renderChildren(doc, group, frame, art, {0, 0}, doc.bounds());
    return renderSmear(*group.smear, art, frame);
}

void renderLayer(const Document& doc, const Layer& layer, int frame, Image& dst, Point origin,
                 Rect area, bool asEdited)
{
    if (!layer.visible)
        return;

    if (layer.isSmear() && !(asEdited && layer.smearAsDrawn)) {
        compose(doc, renderSmearGroup(doc, layer, frame), {0, 0}, dst, origin, area, layer.opacity, layer.blend);
        return;
    }
    if (layer.kind == LayerKind::Group) {
        if (layer.blend == BlendMode::Normal && layer.opacity == 255) {
            renderChildren(doc, layer, frame, dst, origin, area, asEdited);
            return;
        }
        // Isolated group: render children separately, then blend as one.
        Image tmp(area.w, area.h);
        const Point tmpOrigin{area.x, area.y};
        renderChildren(doc, layer, frame, tmp, tmpOrigin, area, asEdited);
        compose(doc, tmp, tmpOrigin, dst, origin, area, layer.opacity, layer.blend);
        return;
    }

    const CelRef cel = layer.cel(frame);
    if (!cel || !cel->image)
        return;
    if (layer.kind == LayerKind::Tilemap) {
        renderTilemap(doc, layer, *cel, dst, origin, area);
        return;
    }
    if (layer.kind != LayerKind::Image)
        return;
    compose(doc, *cel->image, cel->position, dst, origin, area, mul255(cel->opacity, layer.opacity),
            layer.blend, layer.isBackground());
}

} // namespace

void renderOnionSkin(const Document& doc, int frame, const OnionSkin& onion, Image& dst, Rect area, Image& scratch)
{
    assert(dst.format() == PixelFormat::Rgba8 && scratch.format() == PixelFormat::Rgba8);
    area = area.intersected(doc.bounds()).intersected(dst.bounds()).intersected(scratch.bounds());
    if (area.isEmpty())
        return;
    dst.fillRect(area, 0);
    const int opacity = std::clamp(onion.opacity, 0, 255);
    if (opacity == 0)
        return;

    const bool loop = onion.loopFrom >= 0 && onion.loopTo > onion.loopFrom && onion.loopTo < doc.frameCount()
                      && frame >= onion.loopFrom && frame <= onion.loopTo;
    auto layer = [&](int f, int distance, int count, Rgba tint) {
        if (loop) { // past an end of the tag, on from its other end
            const int length = onion.loopTo - onion.loopFrom + 1;
            f = onion.loopFrom + ((f - onion.loopFrom) % length + length) % length;
            if (f == frame)
                return; // the tag is shorter than the distance: never the frame itself
        }
        if (f < 0 || f >= doc.frameCount())
            return;
        // Nearest frame at full onion opacity, fading linearly with distance.
        const int alpha = opacity * (count - distance + 1) / count;
        renderFrame(doc, f, scratch, area);
        for (int y = area.y; y < area.bottom(); ++y) {
            const auto* src = reinterpret_cast<const uint32_t*>(scratch.row(y));
            auto* out = reinterpret_cast<uint32_t*>(dst.row(y));
            for (int x = area.x; x < area.right(); ++x) {
                Rgba s = unpack(src[x]);
                if (s.a == 0)
                    continue;
                if (onion.tint)
                    s = {uint8_t((s.r + tint.r) / 2), uint8_t((s.g + tint.g) / 2), uint8_t((s.b + tint.b) / 2), s.a};
                s.a = uint8_t(mul255(s.a, alpha));
                out[x] = pack(blend(unpack(out[x]), s, 255, BlendMode::Normal));
            }
        }
    };
    const int before = std::max(0, onion.before);
    const int after = std::max(0, onion.after);
    for (int d = std::max(before, after); d >= 1; --d) {
        if (d <= before)
            layer(frame - d, d, before, Rgba{255, 70, 70, 255});
        if (d <= after)
            layer(frame + d, d, after, Rgba{70, 140, 255, 255});
    }
}

void renderLayerContent(const Document& doc, const Layer& layer, int frame, Image& dst, Rect area)
{
    assert(dst.format() == PixelFormat::Rgba8);
    area = area.intersected(doc.bounds()).intersected(dst.bounds());
    if (area.isEmpty())
        return;
    dst.fillRect(area, 0);
    if (layer.isSmear()) {
        compose(doc, renderSmearGroup(doc, layer, frame), {0, 0}, dst, {0, 0}, area, 255, BlendMode::Normal);
    } else if (layer.kind == LayerKind::Group) {
        renderChildren(doc, layer, frame, dst, {0, 0}, area);
    } else if (layer.kind == LayerKind::Tilemap) {
        const CelRef cel = layer.cel(frame);
        if (cel && cel->image)
            renderTilemap(doc, layer, *cel, dst, {0, 0}, area);
    }
}

void renderSmearArt(const Document& doc, const Layer& group, int frame, Image& dst)
{
    assert(dst.format() == PixelFormat::Rgba8);
    dst.clear(0);
    renderChildren(doc, group, frame, dst, {0, 0}, doc.bounds().intersected(dst.bounds()));
}

std::unique_ptr<Layer> bakeSmearGroup(const Document& doc, const Layer& group)
{
    auto layer = doc.makeImageLayer(group.name);
    layer->visible = group.visible;
    layer->opacity = group.opacity;
    layer->blend = group.blend;
    layer->cels.assign(size_t(doc.frameCount()), nullptr);
    for (int f = 0; f < doc.frameCount(); ++f) {
        const Image rgba = renderSmearGroup(doc, group, f);
        if (opaqueBounds(rgba).isEmpty())
            continue;
        auto image = std::make_shared<Image>(convertToDocument(doc, rgba));
        if (f > 0 && layer->cels[size_t(f) - 1] && *layer->cels[size_t(f) - 1]->image == *image) {
            layer->cels[size_t(f)] = layer->cels[size_t(f) - 1];
            continue;
        }
        auto cel = std::make_shared<CelData>();
        cel->image = std::move(image);
        layer->cels[size_t(f)] = std::move(cel);
    }
    return layer;
}

void renderFrame(const Document& doc, int frame, Image& dst, Rect area, bool asEdited)
{
    assert(dst.format() == PixelFormat::Rgba8);
    area = area.intersected(doc.bounds()).intersected(dst.bounds());
    if (area.isEmpty())
        return;
    dst.fillRect(area, 0);
    renderChildren(doc, doc.root(), frame, dst, {0, 0}, area, asEdited);
}

Rect contentBounds(const Document& doc)
{
    Rect bounds;
    Image frame(doc.width(), doc.height());
    for (int f = 0; f < doc.frameCount(); ++f) {
        frame.clear();
        renderFrame(doc, f, frame, {0, 0, doc.width(), doc.height()});
        bounds = bounds.united(opaqueBounds(frame));
    }
    return bounds;
}

} // namespace core
