#include "core/Tilemap.h"

#include "core/Color.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <utility>

namespace core {

namespace {

// Where shown pixel (x, y) of a w x h cell comes from in the tile.
std::pair<int, int> tileSource(int x, int y, int w, int h, uint32_t flips)
{
    int sx = (flips & kTileFlipX) ? w - 1 - x : x;
    int sy = (flips & kTileFlipY) ? h - 1 - y : y;
    if (flips & kTileFlipDiagonal)
        std::swap(sx, sy);
    return {sx, sy};
}

} // namespace

Image orientTile(const Image& tile, uint32_t flips)
{
    const bool diagonal = flips & kTileFlipDiagonal;
    const int w = diagonal ? tile.height() : tile.width(), h = diagonal ? tile.width() : tile.height();
    Image out(w, h, tile.format());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto [sx, sy] = tileSource(x, y, w, h, flips);
            out.setPixel(x, y, tile.pixel(sx, sy));
        }
    return out;
}

Image unorientTile(const Image& shown, uint32_t flips)
{
    const bool diagonal = flips & kTileFlipDiagonal;
    Image out(diagonal ? shown.height() : shown.width(), diagonal ? shown.width() : shown.height(), shown.format());
    for (int y = 0; y < shown.height(); ++y)
        for (int x = 0; x < shown.width(); ++x) {
            const auto [sx, sy] = tileSource(x, y, shown.width(), shown.height(), flips);
            out.setPixel(sx, sy, shown.pixel(x, y));
        }
    return out;
}

Image flattenTilemap(const Image& map, const Tileset& set, PixelFormat format, uint32_t transparent)
{
    Image out(map.width() * set.tileWidth, map.height() * set.tileHeight, format);
    out.clear(transparent);
    for (int cy = 0; cy < map.height(); ++cy)
        for (int cx = 0; cx < map.width(); ++cx) {
            const uint32_t v = map.pixel(cx, cy);
            const uint32_t id = v & kTileIdMask;
            if (id == 0 || id >= set.tiles.size() || !set.tiles[id])
                continue;
            const uint32_t flips = v & ~kTileIdMask;
            out.blit(flips ? orientTile(*set.tiles[id], flips) : *set.tiles[id], {cx * set.tileWidth, cy * set.tileHeight});
        }
    return out;
}

namespace {

uint32_t transparentOf(const Document& doc)
{
    return doc.colorMode() == ColorMode::Indexed ? uint32_t(doc.transparentIndex) : 0;
}

int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

} // namespace

CelRef tilemapFromCel(const Document& doc, const CelData& cel, Tileset& set, Point gridOrigin)
{
    const int tw = set.tileWidth, th = set.tileHeight;
    const uint32_t blank = transparentOf(doc);
    const Image& image = *cel.image;
    const Point p = cel.position;
    // Cells covering the cel's image.
    const int x0 = floorDiv(p.x - gridOrigin.x, tw), y0 = floorDiv(p.y - gridOrigin.y, th);
    const int x1 = floorDiv(p.x + image.width() - 1 - gridOrigin.x, tw) + 1;
    const int y1 = floorDiv(p.y + image.height() - 1 - gridOrigin.y, th) + 1;
    auto out = std::make_shared<CelData>();
    out->opacity = cel.opacity;
    out->zIndex = cel.zIndex;
    out->userData = cel.userData;
    out->position = {gridOrigin.x + x0 * tw, gridOrigin.y + y0 * th};
    out->image = std::make_shared<Image>(std::max(1, x1 - x0), std::max(1, y1 - y0), PixelFormat::Tilemap32);
    out->image->clear(0);
    for (int cy = 0; cy < out->image->height(); ++cy)
        for (int cx = 0; cx < out->image->width(); ++cx) {
            // The cell's pixels, from the cel image (blank outside it).
            Image tile(tw, th, image.format());
            tile.clear(blank);
            tile.blit(image, {p.x - (out->position.x + cx * tw), p.y - (out->position.y + cy * th)});
            if (isBlank(tile, blank))
                continue;
            uint32_t id = 0;
            for (uint32_t k = 1; k < set.tiles.size() && !id; ++k)
                if (set.tiles[k] && *set.tiles[k] == tile)
                    id = k;
            if (!id) {
                set.tiles.push_back(std::make_shared<Image>(std::move(tile)));
                id = uint32_t(set.tiles.size() - 1);
            }
            out->image->setPixel(cx, cy, id);
        }
    return out;
}

CelRef celFromTilemap(const Document& doc, const CelData& cel, const Tileset& set)
{
    auto out = std::make_shared<CelData>();
    out->opacity = cel.opacity;
    out->zIndex = cel.zIndex;
    out->userData = cel.userData;
    out->position = cel.position;
    out->image = std::make_shared<Image>(flattenTilemap(*cel.image, set, doc.pixelFormat(), transparentOf(doc)));
    return out;
}

bool isBlank(const Image& image, uint32_t transparent)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) != transparent)
                return false;
    return true;
}

Image remapTilemap(const Image& tilemap, const std::vector<uint32_t>& map)
{
    Image out(tilemap.width(), tilemap.height(), tilemap.format());
    for (int y = 0; y < tilemap.height(); ++y)
        for (int x = 0; x < tilemap.width(); ++x) {
            const uint32_t v = tilemap.pixel(x, y);
            const uint32_t id = v & kTileIdMask;
            const uint32_t to = id < map.size() ? map[id] : 0;
            // An empty cell stays as it is; a cell whose tile went is empty (no flips).
            out.setPixel(x, y, to == 0 ? 0u : (v & ~kTileIdMask) | to);
        }
    return out;
}

namespace {

// Keeps the tiles `keep[i]` (tile 0 always), in order, and sends each old
// index to `target[i]`: another old index (merging) or itself.
TileEdit compactTiles(const Tileset& set, const std::vector<bool>& keep, const std::vector<size_t>& target)
{
    TileEdit edit;
    edit.after = set;
    edit.after.tiles.clear();
    std::vector<uint32_t> rank(set.tiles.size(), 0);
    for (size_t i = 0; i < set.tiles.size(); ++i)
        if (i == 0 || keep[i]) {
            rank[i] = uint32_t(edit.after.tiles.size());
            edit.after.tiles.push_back(set.tiles[i]);
        }
    edit.map.assign(set.tiles.size(), 0);
    for (size_t i = 0; i < set.tiles.size(); ++i)
        edit.map[i] = i == 0 ? 0 : (keep[i] ? rank[i] : rank[target[i]]);
    edit.changes = edit.after.tiles.size() != set.tiles.size();
    return edit;
}

} // namespace

TileEdit deleteTile(const Tileset& set, size_t index)
{
    if (index == 0 || index >= set.tiles.size())
        return {set, {}, false};
    std::vector<bool> keep(set.tiles.size(), true);
    keep[index] = false;
    std::vector<size_t> target(set.tiles.size(), 0); // a deleted tile's cells become empty
    for (size_t i = 0; i < target.size(); ++i)
        target[i] = i;
    target[index] = 0;
    return compactTiles(set, keep, target);
}

TileEdit moveTile(const Tileset& set, size_t from, size_t to)
{
    const size_t n = set.tiles.size();
    if (from == 0 || to == 0 || from >= n || to >= n || from == to)
        return {set, {}, false};
    TileEdit edit;
    edit.after = set;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
        order[i] = i;
    order.erase(order.begin() + std::ptrdiff_t(from));
    order.insert(order.begin() + std::ptrdiff_t(to), from);
    edit.map.assign(n, 0);
    for (size_t place = 0; place < n; ++place) {
        edit.after.tiles[place] = set.tiles[order[place]];
        edit.map[order[place]] = uint32_t(place);
    }
    edit.changes = true;
    return edit;
}

TileEdit removeUnusedTiles(const Tileset& set, const std::vector<bool>& used)
{
    std::vector<bool> keep(set.tiles.size(), false);
    std::vector<size_t> target(set.tiles.size(), 0);
    for (size_t i = 1; i < set.tiles.size(); ++i)
        keep[i] = i < used.size() && used[i];
    return compactTiles(set, keep, target);
}

TileEdit mergeDuplicateTiles(const Tileset& set)
{
    std::vector<bool> keep(set.tiles.size(), true);
    std::vector<size_t> target(set.tiles.size(), 0);
    for (size_t j = 1; j < set.tiles.size(); ++j) {
        target[j] = j;
        for (size_t i = 1; i < j; ++i)
            if (keep[i] && set.tiles[i] && set.tiles[j] && *set.tiles[i] == *set.tiles[j]) {
                keep[j] = false;
                target[j] = i;
                break;
            }
    }
    return compactTiles(set, keep, target);
}

size_t countUnusedTiles(const Tileset& set, const std::vector<bool>& used)
{
    size_t n = 0;
    for (size_t i = 1; i < set.tiles.size(); ++i)
        n += i >= used.size() || !used[i];
    return n;
}

size_t countDuplicateTiles(const Tileset& set)
{
    return set.tiles.size() - mergeDuplicateTiles(set).after.tiles.size();
}

namespace {

// How much the picture changes across each vertical line (x between x - 1 and x),
// summed over the rows; `horizontal` does the same for horizontal lines.
std::vector<double> edgeStrength(const Image& sheet, bool horizontal)
{
    const int length = horizontal ? sheet.height() : sheet.width();
    const int across = horizontal ? sheet.width() : sheet.height();
    std::vector<double> edge(size_t(std::max(length, 1)), 0.0);
    for (int i = 1; i < length; ++i) {
        double sum = 0;
        for (int j = 0; j < across; ++j) {
            const uint32_t a = horizontal ? sheet.pixel(j, i - 1) : sheet.pixel(i - 1, j);
            const uint32_t b = horizontal ? sheet.pixel(j, i) : sheet.pixel(i, j);
            const Rgba p = unpack(a), q = unpack(b);
            if (p.a == 0 && q.a == 0)
                continue; // two empty pixels are the same, whatever their color
            sum += std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b) + std::abs(p.a - q.a);
        }
        edge[size_t(i)] = sum / std::max(across, 1);
    }
    return edge;
}

// The size t (a divisor of `length`, at least 4, at most half of it) whose grid
// lines carry the strongest edges compared with everything between them; 0 if
// none stands out.
int bestPeriod(const std::vector<double>& edge, int length)
{
    constexpr double kEps = 1.0; // a flat image has no edges to speak of
    double best = 0;
    int bestSize = 0;
    for (int t = 4; t <= length / 2; ++t) {
        if (length % t != 0)
            continue;
        double on = 0, off = 0;
        int onCount = 0, offCount = 0;
        for (int x = 1; x < length; ++x) {
            if (x % t == 0) {
                on += edge[size_t(x)];
                ++onCount;
            } else {
                off += edge[size_t(x)];
                ++offCount;
            }
        }
        if (onCount == 0 || offCount == 0)
            continue;
        const double score = (on / onCount + kEps) / (off / offCount + kEps);
        if (score > best * 1.02) { // a clear win, so the same score keeps the smaller size
            best = score;
            bestSize = t;
        }
    }
    return best >= 1.5 ? bestSize : 0;
}

} // namespace

TileSizeGuess detectTileSize(const Image& sheet)
{
    TileSizeGuess g;
    if (sheet.isNull() || sheet.format() != PixelFormat::Rgba8)
        return g;
    g.width = bestPeriod(edgeStrength(sheet, false), sheet.width());
    g.height = bestPeriod(edgeStrength(sheet, true), sheet.height());
    // A sheet of one row (or column) has no edges across it to find: the tile is as
    // tall as the sheet, or, when the sheet is taller, square like the other side.
    const auto otherSide = [](int found, int length) {
        if (length == found || (length < found))
            return length;
        return length % found == 0 ? found : 0;
    };
    if (g.width > 0 && g.height == 0)
        g.height = otherSide(g.width, sheet.height());
    else if (g.height > 0 && g.width == 0)
        g.width = otherSide(g.height, sheet.width());
    if (g.width == 0 || g.height == 0)
        return {};
    return g;
}

SheetImport importSheetTiles(const Tileset& set, const std::vector<Image>& slices, int columns, int rows,
                             uint32_t transparent, bool skipEmpty, bool merge)
{
    SheetImport out;
    out.map = Image(std::max(columns, 1), std::max(rows, 1), PixelFormat::Tilemap32);
    out.map.clear(0);
    // Tiles by a hash of their bytes, for `merge`: the ones the tileset has, then the new ones.
    const auto hashOf = [](const Image& image) {
        uint64_t h = 1469598103934665603ull;
        const uint8_t* p = image.data();
        for (size_t i = 0, n = size_t(image.stride()) * size_t(image.height()); i < n; ++i)
            h = (h ^ p[i]) * 1099511628211ull;
        return h;
    };
    std::unordered_multimap<uint64_t, uint32_t> seen;
    const auto tileAt = [&](uint32_t index) -> const Image* {
        if (index < set.tiles.size())
            return set.tiles[index].get();
        return out.added[index - set.tiles.size()].get();
    };
    if (merge)
        for (uint32_t i = 1; i < set.tiles.size(); ++i)
            if (set.tiles[i] && !isBlank(*set.tiles[i], transparent))
                seen.emplace(hashOf(*set.tiles[i]), i);
    for (size_t i = 0; i < slices.size() && int(i) < columns * rows; ++i) {
        const Image& slice = slices[i];
        uint32_t id = 0;
        const bool blank = isBlank(slice, transparent);
        if (blank && (skipEmpty || merge)) {
            id = 0;
        } else {
            bool reused = false;
            if (merge) {
                const auto range = seen.equal_range(hashOf(slice));
                for (auto it = range.first; it != range.second; ++it)
                    if (const Image* t = tileAt(it->second); t && *t == slice) {
                        id = it->second;
                        reused = true;
                        break;
                    }
            }
            if (!reused) {
                id = uint32_t(set.tiles.size() + out.added.size());
                out.added.push_back(std::make_shared<Image>(slice));
                if (merge)
                    seen.emplace(hashOf(slice), id);
            }
        }
        out.map.setPixel(int(i) % columns, int(i) / columns, id);
    }
    return out;
}

SheetGrid sheetGrid(int width, int height, int tileWidth, int tileHeight, int margin, int spacing)
{
    SheetGrid g;
    if (tileWidth < 1 || tileHeight < 1)
        return g;
    margin = std::max(0, margin);
    spacing = std::max(0, spacing);
    const int usableW = width - 2 * margin, usableH = height - 2 * margin;
    if (usableW < tileWidth || usableH < tileHeight)
        return g;
    g.columns = (usableW + spacing) / (tileWidth + spacing);
    g.rows = (usableH + spacing) / (tileHeight + spacing);
    return g;
}

std::vector<Image> sliceSheet(const Image& sheet, int tileWidth, int tileHeight, int margin, int spacing)
{
    std::vector<Image> out;
    const SheetGrid g = sheetGrid(sheet.width(), sheet.height(), tileWidth, tileHeight, margin, spacing);
    margin = std::max(0, margin);
    spacing = std::max(0, spacing);
    for (int row = 0; row < g.rows; ++row)
        for (int col = 0; col < g.columns; ++col)
            out.push_back(sheet.copy({margin + col * (tileWidth + spacing), margin + row * (tileHeight + spacing),
                                      tileWidth, tileHeight}));
    return out;
}

} // namespace core
