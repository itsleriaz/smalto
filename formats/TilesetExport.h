#pragma once

#include "core/Document.h"

#include <filesystem>
#include <string>
#include <vector>

// Tilesets and tilemaps for game engines: the tileset as one PNG (tile 0, the
// empty tile, left out), a Tiled tileset (.tsx), a Tiled map (.tmx) and CSV.
// Our tile index is Tiled's GID with a first GID of 1, and the flip bits are
// Tiled's flags.
namespace formats {

struct TilesetSheet {
    int columns = 0;
    int rows = 0;
    int width = 0;  // pixels
    int height = 0;
    int tiles = 0;  // drawn: the tileset's without the empty one
};

// How `tiles` tiles sit in `columns` columns with `margin` around and
// `spacing` between (columns is clamped to 1..tiles).
TilesetSheet tilesetSheetLayout(int tiles, int tileWidth, int tileHeight, int columns, int margin, int spacing);
// The tiles 1.. drawn in that layout, as RGBA.
core::Image tilesetSheetImage(const core::Document& doc, const core::Tileset& set, int columns, int margin, int spacing);

// One tilemap layer cut to the cells it covers.
struct MapLayer {
    std::string name;
    bool visible = true;
    int opacity = 255;
    int x = 0; // the cell of its first column, in the map
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> gids; // width * height, row by row; 0 = empty
};

// A tilemap cel as a layer of GIDs. `cellsX/Y`: where the cel's first cell is
// (its pixel position over the tile size, rounded down).
MapLayer mapLayerFromCel(const core::Layer& layer, const core::CelData& cel, int tileWidth, int tileHeight);

// A tileset a map uses: the file that describes it and the number of its first tile.
struct MapTilesetRef {
    std::string file;
    uint32_t firstGid = 1;
};
// The layer's tile numbers moved to a tileset whose first tile is `firstGid` (the flip
// flags stay; the empty cells too).
void offsetGids(MapLayer& layer, uint32_t firstGid);

std::string tiledTilesetXml(const core::Tileset& set, const TilesetSheet& sheet, int margin, int spacing,
                            const std::string& imageFile);
std::string tiledMapXml(const core::Tileset& set, const std::string& tilesetFile,
                        const std::vector<MapLayer>& layers);
// A map of several tilesets (of this tile size); the layers' numbers already count from each one's first tile.
std::string tiledMapXml(int tileWidth, int tileHeight, const std::vector<MapTilesetRef>& tilesets,
                        const std::vector<MapLayer>& layers);
// The same as Tiled's JSON formats (.tsj and .tmj): many engines and Godot's Tiled
// importers read these more easily than XML.
std::string tiledTilesetJson(const core::Tileset& set, const TilesetSheet& sheet, int margin, int spacing,
                             const std::string& imageFile);
std::string tiledMapJson(const core::Tileset& set, const std::string& tilesetFile,
                         const std::vector<MapLayer>& layers);
std::string tiledMapJson(int tileWidth, int tileHeight, const std::vector<MapTilesetRef>& tilesets,
                         const std::vector<MapLayer>& layers);
std::string mapLayerCsv(const MapLayer& layer);

// One atlas of a Godot TileSet: a tileset as the PNG it was written to.
struct GodotAtlas {
    const core::Tileset* set = nullptr;
    std::string imageFile; // the PNG's file name
    TilesetSheet sheet;
    int margin = 0;
    int spacing = 0;
};
// A Godot 4 TileSet resource (.tres): one atlas source per tileset, in order, each with the
// tiles the sheet holds (tile i at its column and row). `resFolder` is where the PNGs sit in
// the Godot project ("res://", "res://art/tiles/"); the TileMap cells then name a source and a
// tile's coordinates, so nothing needs offsetting.
std::string godotTileSet(const std::vector<GodotAtlas>& atlases, int tileWidth, int tileHeight,
                         const std::string& resFolder);

struct TilesetExportOptions {
    int columns = 8;
    int margin = 0;
    int spacing = 0;
    bool tsx = true;  // Tiled tileset, XML
    bool tmx = false; // Tiled map, XML (needs the tileset file)
    bool tsj = false; // Tiled tileset, JSON
    bool tmj = false; // Tiled map, JSON (needs its tileset file)
    bool csv = false;
    bool tres = false;           // Godot 4 TileSet
    std::string godotFolder;     // where the PNGs will be in the Godot project ("" = res://)
};

// Writes `<path stem>.png` and, as chosen, `.tsx`, `.tmx`, `.tsj`, `.tmj`, `.tres` (every tilemap layer
// of tileset `tilesetIndex` with a cel on `frame`) and `<stem>_<layer>.csv`.
bool exportTileset(const core::Document& doc, size_t tilesetIndex, int frame, const std::filesystem::path& path,
                   const TilesetExportOptions& options, std::string* error);

} // namespace formats
