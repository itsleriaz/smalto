#include "formats/TilesetExport.h"

#include "core/Compositor.h"
#include "formats/Export.h"
#include "formats/FileIO.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>

using namespace core;

namespace formats {

namespace {

std::string escape(const std::string& s)
{
    std::string out;
    for (char c : s)
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    return out;
}

std::vector<uint8_t> bytes(const std::string& s)
{
    return {s.begin(), s.end()};
}

std::string csvRows(const MapLayer& layer, const char* lineEnd)
{
    std::string out;
    for (int y = 0; y < layer.height; ++y) {
        for (int x = 0; x < layer.width; ++x) {
            out += std::to_string(layer.gids[size_t(y) * size_t(layer.width) + size_t(x)]);
            if (x + 1 < layer.width || y + 1 < layer.height)
                out += ',';
        }
        out += lineEnd;
    }
    return out;
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    for (char c : s)
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    return out;
}

// Every layer padded to the cells the whole map covers.
struct PaddedMap {
    int width = 0;
    int height = 0;
    std::vector<MapLayer> layers; // each width x height, x = y = 0
};

PaddedMap padLayers(const std::vector<MapLayer>& layers)
{
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (const MapLayer& l : layers) {
        minX = std::min(minX, l.x);
        minY = std::min(minY, l.y);
        maxX = std::max(maxX, l.x + l.width);
        maxY = std::max(maxY, l.y + l.height);
    }
    if (layers.empty())
        minX = minY = maxX = maxY = 0;
    PaddedMap map;
    map.width = maxX - minX;
    map.height = maxY - minY;
    for (const MapLayer& l : layers) {
        MapLayer padded;
        padded.name = l.name;
        padded.visible = l.visible;
        padded.opacity = l.opacity;
        padded.width = map.width;
        padded.height = map.height;
        padded.gids.assign(size_t(map.width) * size_t(map.height), 0);
        for (int y = 0; y < l.height; ++y)
            for (int x = 0; x < l.width; ++x)
                padded.gids[size_t(y + l.y - minY) * size_t(map.width) + size_t(x + l.x - minX)] =
                    l.gids[size_t(y) * size_t(l.width) + size_t(x)];
        map.layers.push_back(std::move(padded));
    }
    return map;
}

std::string safeName(const std::string& name)
{
    std::string out;
    for (char c : name)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "layer" : out;
}

} // namespace

TilesetSheet tilesetSheetLayout(int tiles, int tileWidth, int tileHeight, int columns, int margin, int spacing)
{
    TilesetSheet s;
    s.tiles = std::max(0, tiles);
    margin = std::max(0, margin);
    spacing = std::max(0, spacing);
    s.columns = std::clamp(columns, 1, std::max(1, s.tiles));
    s.rows = s.tiles == 0 ? 0 : (s.tiles + s.columns - 1) / s.columns;
    s.width = s.tiles == 0 ? 0 : 2 * margin + s.columns * tileWidth + (s.columns - 1) * spacing;
    s.height = s.tiles == 0 ? 0 : 2 * margin + s.rows * tileHeight + (s.rows - 1) * spacing;
    return s;
}

Image tilesetSheetImage(const Document& doc, const Tileset& set, int columns, int margin, int spacing)
{
    const int count = int(set.tiles.size()) - 1;
    const TilesetSheet s = tilesetSheetLayout(count, set.tileWidth, set.tileHeight, columns, margin, spacing);
    Image out(std::max(1, s.width), std::max(1, s.height));
    out.clear(0);
    margin = std::max(0, margin);
    spacing = std::max(0, spacing);
    for (int i = 0; i < s.tiles; ++i) {
        const Image& tile = *set.tiles[size_t(i) + 1];
        const int ox = margin + (i % s.columns) * (set.tileWidth + spacing);
        const int oy = margin + (i / s.columns) * (set.tileHeight + spacing);
        for (int y = 0; y < tile.height(); ++y)
            for (int x = 0; x < tile.width(); ++x)
                out.setPixel(ox + x, oy + y, pack(toRgba(doc, tile, x, y)));
    }
    return out;
}

MapLayer mapLayerFromCel(const Layer& layer, const CelData& cel, int tileWidth, int tileHeight)
{
    MapLayer m;
    m.name = layer.name;
    m.visible = layer.visible;
    m.opacity = layer.opacity;
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    m.x = floorDiv(cel.position.x, std::max(1, tileWidth));
    m.y = floorDiv(cel.position.y, std::max(1, tileHeight));
    if (!cel.image)
        return m;
    m.width = cel.image->width();
    m.height = cel.image->height();
    m.gids.resize(size_t(m.width) * size_t(m.height));
    for (int y = 0; y < m.height; ++y)
        for (int x = 0; x < m.width; ++x) {
            const uint32_t v = cel.image->pixel(x, y);
            const uint32_t id = v & kTileIdMask;
            // Flags are Tiled's: horizontal 0x80000000, vertical 0x40000000, diagonal 0x20000000.
            m.gids[size_t(y) * size_t(m.width) + size_t(x)] = id == 0 ? 0 : v;
        }
    return m;
}

std::string tiledTilesetXml(const Tileset& set, const TilesetSheet& sheet, int margin, int spacing,
                            const std::string& imageFile)
{
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s += "<tileset version=\"1.10\" tiledversion=\"1.10.2\" name=\"" + escape(set.name) + "\" tilewidth=\""
        + std::to_string(set.tileWidth) + "\" tileheight=\"" + std::to_string(set.tileHeight) + "\" tilecount=\""
        + std::to_string(sheet.tiles) + "\" columns=\"" + std::to_string(sheet.columns) + "\"";
    if (spacing > 0)
        s += " spacing=\"" + std::to_string(spacing) + "\"";
    if (margin > 0)
        s += " margin=\"" + std::to_string(margin) + "\"";
    s += ">\n <image source=\"" + escape(imageFile) + "\" width=\"" + std::to_string(sheet.width) + "\" height=\""
        + std::to_string(sheet.height) + "\"/>\n</tileset>\n";
    return s;
}

void offsetGids(MapLayer& layer, uint32_t firstGid)
{
    const uint32_t offset = firstGid > 0 ? firstGid - 1 : 0;
    for (uint32_t& g : layer.gids)
        if ((g & kTileIdMask) != 0)
            g = (g & ~kTileIdMask) | ((g & kTileIdMask) + offset);
}

std::string tiledMapXml(const Tileset& set, const std::string& tilesetFile, const std::vector<MapLayer>& layers)
{
    return tiledMapXml(set.tileWidth, set.tileHeight, {{tilesetFile, 1}}, layers);
}

std::string tiledMapXml(int tileWidth, int tileHeight, const std::vector<MapTilesetRef>& tilesets,
                        const std::vector<MapLayer>& layers)
{
    // The map covers every layer; each layer is padded to it.
    const PaddedMap map = padLayers(layers);
    const int w = map.width, h = map.height;
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s += "<map version=\"1.10\" tiledversion=\"1.10.2\" orientation=\"orthogonal\" renderorder=\"right-down\" width=\""
        + std::to_string(w) + "\" height=\"" + std::to_string(h) + "\" tilewidth=\"" + std::to_string(tileWidth)
        + "\" tileheight=\"" + std::to_string(tileHeight) + "\" infinite=\"0\" nextlayerid=\""
        + std::to_string(layers.size() + 1) + "\" nextobjectid=\"1\">\n";
    for (const MapTilesetRef& t : tilesets)
        s += " <tileset firstgid=\"" + std::to_string(t.firstGid) + "\" source=\"" + escape(t.file) + "\"/>\n";
    int id = 1;
    for (const MapLayer& l : map.layers) {
        s += " <layer id=\"" + std::to_string(id++) + "\" name=\"" + escape(l.name) + "\" width=\"" + std::to_string(w)
            + "\" height=\"" + std::to_string(h) + "\"";
        if (!l.visible)
            s += " visible=\"0\"";
        if (l.opacity < 255)
            s += " opacity=\"" + std::to_string(double(l.opacity) / 255.0).substr(0, 5) + "\"";
        s += ">\n  <data encoding=\"csv\">\n";
        s += csvRows(l, "\n");
        s += "</data>\n </layer>\n";
    }
    s += "</map>\n";
    return s;
}

std::string godotTileSet(const std::vector<GodotAtlas>& atlases, int tileWidth, int tileHeight,
                         const std::string& resFolder)
{
    const auto quoted = [](const std::string& text) {
        std::string out = "\"";
        for (char c : text)
            out += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
        return out + "\"";
    };
    std::string folder = resFolder.empty() ? "res://" : resFolder;
    if (folder.rfind("res://", 0) != 0)
        folder = "res://" + folder;
    if (folder.back() != '/')
        folder += '/';
    const auto vec = [](int x, int y) { return "Vector2i(" + std::to_string(x) + ", " + std::to_string(y) + ")"; };
    const size_t resources = atlases.size() * 2; // a texture and an atlas source each
    std::string s = "[gd_resource type=\"TileSet\" load_steps=" + std::to_string(resources + 1) + " format=3]\n\n";
    std::vector<std::string> textureIds, sourceIds;
    for (size_t i = 0; i < atlases.size(); ++i) {
        const std::string name = safeName(std::filesystem::path(atlases[i].imageFile).stem().string());
        textureIds.push_back(std::to_string(i + 1) + "_" + name);
        sourceIds.push_back("TileSetAtlasSource_" + name + (i ? "_" + std::to_string(i + 1) : std::string()));
        s += "[ext_resource type=\"Texture2D\" path=" + quoted(folder + atlases[i].imageFile) + " id=\"" + textureIds[i]
            + "\"]\n";
    }
    s += "\n";
    for (size_t i = 0; i < atlases.size(); ++i) {
        const GodotAtlas& a = atlases[i];
        s += "[sub_resource type=\"TileSetAtlasSource\" id=\"" + sourceIds[i] + "\"]\n";
        s += "texture = ExtResource(\"" + textureIds[i] + "\")\n";
        if (a.margin > 0)
            s += "margins = " + vec(a.margin, a.margin) + "\n";
        if (a.spacing > 0)
            s += "separation = " + vec(a.spacing, a.spacing) + "\n";
        s += "texture_region_size = " + vec(a.set->tileWidth, a.set->tileHeight) + "\n";
        for (int t = 0; t < a.sheet.tiles; ++t)
            s += std::to_string(t % a.sheet.columns) + ":" + std::to_string(t / a.sheet.columns) + "/0 = 0\n";
        s += "\n";
    }
    s += "[resource]\ntile_size = " + vec(tileWidth, tileHeight) + "\n";
    for (size_t i = 0; i < atlases.size(); ++i)
        s += "sources/" + std::to_string(i) + " = SubResource(\"" + sourceIds[i] + "\")\n";
    return s;
}

std::string tiledTilesetJson(const Tileset& set, const TilesetSheet& sheet, int margin, int spacing,
                             const std::string& imageFile)
{
    std::string s = "{\n";
    s += " \"columns\": " + std::to_string(sheet.columns) + ",\n";
    s += " \"image\": \"" + jsonEscape(imageFile) + "\",\n";
    s += " \"imageheight\": " + std::to_string(sheet.height) + ",\n";
    s += " \"imagewidth\": " + std::to_string(sheet.width) + ",\n";
    s += " \"margin\": " + std::to_string(std::max(0, margin)) + ",\n";
    s += " \"name\": \"" + jsonEscape(set.name) + "\",\n";
    s += " \"spacing\": " + std::to_string(std::max(0, spacing)) + ",\n";
    s += " \"tilecount\": " + std::to_string(sheet.tiles) + ",\n";
    s += " \"tiledversion\": \"1.10.2\",\n";
    s += " \"tileheight\": " + std::to_string(set.tileHeight) + ",\n";
    s += " \"tilewidth\": " + std::to_string(set.tileWidth) + ",\n";
    s += " \"type\": \"tileset\",\n";
    s += " \"version\": \"1.10\"\n}\n";
    return s;
}

std::string tiledMapJson(const Tileset& set, const std::string& tilesetFile, const std::vector<MapLayer>& layers)
{
    return tiledMapJson(set.tileWidth, set.tileHeight, {{tilesetFile, 1}}, layers);
}

std::string tiledMapJson(int tileWidth, int tileHeight, const std::vector<MapTilesetRef>& tilesets,
                         const std::vector<MapLayer>& layers)
{
    const PaddedMap map = padLayers(layers);
    std::string s = "{\n";
    s += " \"compressionlevel\": -1,\n";
    s += " \"height\": " + std::to_string(map.height) + ",\n";
    s += " \"infinite\": false,\n";
    s += " \"layers\": [\n";
    int id = 1;
    for (size_t i = 0; i < map.layers.size(); ++i) {
        const MapLayer& l = map.layers[i];
        s += "  {\n   \"data\": [";
        for (size_t k = 0; k < l.gids.size(); ++k) {
            if (k % size_t(std::max(1, map.width)) == 0)
                s += "\n    ";
            s += std::to_string(l.gids[k]);
            if (k + 1 < l.gids.size())
                s += ",";
        }
        s += "\n   ],\n";
        s += "   \"height\": " + std::to_string(map.height) + ",\n";
        s += "   \"id\": " + std::to_string(id++) + ",\n";
        s += "   \"name\": \"" + jsonEscape(l.name) + "\",\n";
        s += "   \"opacity\": " + std::to_string(double(l.opacity) / 255.0).substr(0, 5) + ",\n";
        s += "   \"type\": \"tilelayer\",\n";
        s += std::string("   \"visible\": ") + (l.visible ? "true" : "false") + ",\n";
        s += "   \"width\": " + std::to_string(map.width) + ",\n";
        s += "   \"x\": 0,\n   \"y\": 0\n  }";
        s += i + 1 < map.layers.size() ? ",\n" : "\n";
    }
    s += " ],\n";
    s += " \"nextlayerid\": " + std::to_string(map.layers.size() + 1) + ",\n";
    s += " \"nextobjectid\": 1,\n";
    s += " \"orientation\": \"orthogonal\",\n";
    s += " \"renderorder\": \"right-down\",\n";
    s += " \"tiledversion\": \"1.10.2\",\n";
    s += " \"tileheight\": " + std::to_string(tileHeight) + ",\n";
    s += " \"tilesets\": [";
    for (size_t i = 0; i < tilesets.size(); ++i)
        s += std::string(i ? ", " : "") + "{\"firstgid\": " + std::to_string(tilesets[i].firstGid) + ", \"source\": \""
            + jsonEscape(tilesets[i].file) + "\"}";
    s += "],\n";
    s += " \"tilewidth\": " + std::to_string(tileWidth) + ",\n";
    s += " \"type\": \"map\",\n";
    s += " \"version\": \"1.10\",\n";
    s += " \"width\": " + std::to_string(map.width) + "\n}\n";
    return s;
}

std::string mapLayerCsv(const MapLayer& layer)
{
    return csvRows(layer, "\n");
}

bool exportTileset(const Document& doc, size_t tilesetIndex, int frame, const std::filesystem::path& path,
                   const TilesetExportOptions& options, std::string* error)
{
    const auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    if (tilesetIndex >= doc.tilesets.size())
        return fail("no such tileset");
    const Tileset& set = doc.tilesets[tilesetIndex];
    if (set.tiles.size() < 2)
        return fail("the tileset has no tiles");
    const auto with = [&](const std::filesystem::path& stem, const char* ext) {
        std::filesystem::path p = stem;
        p.replace_extension();
        p.replace_extension(ext);
        return p;
    };
    const auto writeTileset = [&](const Tileset& s, const std::filesystem::path& stem, bool xml, bool json,
                                  std::string* pngName) {
        const std::filesystem::path png = with(stem, ".png");
        const TilesetSheet sheet = tilesetSheetLayout(int(s.tiles.size()) - 1, s.tileWidth, s.tileHeight,
                                                      options.columns, options.margin, options.spacing);
        if (!writeFileAtomically(png, encodePng(tilesetSheetImage(doc, s, options.columns, options.margin, options.spacing)),
                                 error))
            return false;
        if (pngName)
            *pngName = png.filename().string();
        if (xml
            && !writeFileAtomically(with(stem, ".tsx"),
                                    bytes(tiledTilesetXml(s, sheet, options.margin, options.spacing, png.filename().string())),
                                    error))
            return false;
        if (json
            && !writeFileAtomically(with(stem, ".tsj"),
                                    bytes(tiledTilesetJson(s, sheet, options.margin, options.spacing, png.filename().string())),
                                    error))
            return false;
        return true;
    };
    // The chosen file's tileset: the PNG, and the tileset files the options (and a map) ask for.
    if (!writeTileset(set, path, options.tsx || options.tmx, options.tsj || options.tmj, nullptr))
        return false;
    if (!options.tmx && !options.tmj && !options.csv && !options.tres)
        return true;

    // The tilemap layers with tiles on this frame: this tileset's, and (for the maps) those of the
    // other tilesets of the same tile size, which a map can hold next to it.
    struct Found {
        const Layer* layer;
        const CelData* cel;
        size_t tileset;
    };
    std::vector<Found> found;
    doc.forEachLayer([&](const Layer& layer) {
        if (layer.kind != LayerKind::Tilemap || layer.tilesetIndex >= doc.tilesets.size())
            return;
        if (frame < 0 || frame >= int(layer.cels.size()) || !layer.cels[size_t(frame)])
            return;
        found.push_back({&layer, layer.cels[size_t(frame)].get(), layer.tilesetIndex});
    });
    std::vector<MapLayer> own;
    for (const Found& f : found)
        if (f.tileset == tilesetIndex)
            own.push_back(mapLayerFromCel(*f.layer, *f.cel, set.tileWidth, set.tileHeight));
    if (own.empty() && (options.tmx || options.tmj || options.csv))
        return fail("no tilemap layer of this tileset has tiles on this frame");

    if (options.tmx || options.tmj || options.tres) {
        // The tilesets in the map: this one first (its first tile is 1), then the others in the
        // order of their layers, each with its own PNG and tileset files named after it.
        std::vector<size_t> order{tilesetIndex};
        for (const Found& f : found) {
            const Tileset& t = doc.tilesets[f.tileset];
            if (std::find(order.begin(), order.end(), f.tileset) == order.end() && t.tiles.size() >= 2
                && t.tileWidth == set.tileWidth && t.tileHeight == set.tileHeight)
                order.push_back(f.tileset);
        }
        std::vector<std::string> names{path.stem().string()};
        std::vector<MapTilesetRef> xmlRefs, jsonRefs;
        std::vector<GodotAtlas> atlases;
        std::vector<uint32_t> firstGids(doc.tilesets.size(), 0);
        uint32_t next = 1;
        for (size_t k = 0; k < order.size(); ++k) {
            const Tileset& t = doc.tilesets[order[k]];
            firstGids[order[k]] = next;
            std::filesystem::path stem = path;
            if (k > 0) {
                // A name of its own, kept clear of the ones already used.
                std::string name = safeName(t.name);
                for (int n = 2; std::find(names.begin(), names.end(), name) != names.end(); ++n)
                    name = safeName(t.name) + "_" + std::to_string(n);
                names.push_back(name);
                stem = path.parent_path() / name;
                if (!writeTileset(t, stem, options.tmx, options.tmj, nullptr))
                    return false;
            }
            atlases.push_back({&t, with(stem, ".png").filename().string(),
                               tilesetSheetLayout(int(t.tiles.size()) - 1, t.tileWidth, t.tileHeight, options.columns,
                                                  options.margin, options.spacing),
                               options.margin, options.spacing});
            xmlRefs.push_back({with(stem, ".tsx").filename().string(), next});
            jsonRefs.push_back({with(stem, ".tsj").filename().string(), next});
            next += uint32_t(t.tiles.size() - 1);
        }
        if (options.tres
            && !writeFileAtomically(with(path, ".tres"),
                                    bytes(godotTileSet(atlases, set.tileWidth, set.tileHeight, options.godotFolder)), error))
            return false;
        std::vector<MapLayer> mapLayers;
        for (const Found& f : found) {
            if (firstGids[f.tileset] == 0)
                continue; // another tile size, or nothing to draw: left out
            MapLayer l = mapLayerFromCel(*f.layer, *f.cel, set.tileWidth, set.tileHeight);
            offsetGids(l, firstGids[f.tileset]);
            mapLayers.push_back(std::move(l));
        }
        if ((options.tmx || options.tmj) && mapLayers.empty())
            return fail("no tilemap layer has tiles on this frame");
        if (options.tmx
            && !writeFileAtomically(with(path, ".tmx"), bytes(tiledMapXml(set.tileWidth, set.tileHeight, xmlRefs, mapLayers)),
                                    error))
            return false;
        if (options.tmj
            && !writeFileAtomically(with(path, ".tmj"), bytes(tiledMapJson(set.tileWidth, set.tileHeight, jsonRefs, mapLayers)),
                                    error))
            return false;
    }
    if (options.csv)
        for (const MapLayer& l : own) {
            std::filesystem::path p = path;
            p.replace_extension();
            p += "_" + safeName(l.name) + ".csv";
            if (!writeFileAtomically(p, bytes(mapLayerCsv(l)), error))
                return false;
        }
    return true;
}

} // namespace formats
