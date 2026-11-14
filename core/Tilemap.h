#pragma once

#include "core/Document.h"
#include "core/Image.h"

namespace core {

// A tile as a tilemap cell shows it, given the cell's flip bits
// (kTileFlipX / Y / Diagonal: diagonal swaps x and y first), and back.
Image orientTile(const Image& tile, uint32_t flips);
Image unorientTile(const Image& shown, uint32_t flips);

// The tilemap `map` (Tilemap32) drawn as pixels in `format`, one tile per
// cell (`transparent` for empty cells and tiles the tileset hasn't got).
Image flattenTilemap(const Image& map, const Tileset& set, PixelFormat format, uint32_t transparent);

// True when every pixel is `transparent`.
bool isBlank(const Image& image, uint32_t transparent);

// An image cel as a tilemap cel of `set`, on a grid whose lines pass
// through `gridOrigin`: each cell becomes a tile, identical ones shared
// (new ones added to `set`), blank cells the empty tile (0, which `set`
// must already have).
CelRef tilemapFromCel(const Document& doc, const CelData& cel, Tileset& set, Point gridOrigin);
// A tilemap cel of `set` as an image cel (the tiles drawn as pixels).
CelRef celFromTilemap(const Document& doc, const CelData& cel, const Tileset& set);

// --- tileset images ----------------------------------------------------------

// How many tiles of tileWidth x tileHeight fit a sheet of width x height
// pixels, `margin` in from every edge and `spacing` between neighbours.
struct SheetGrid {
    int columns = 0;
    int rows = 0;
    int count() const { return columns * rows; }
};
SheetGrid sheetGrid(int width, int height, int tileWidth, int tileHeight, int margin, int spacing);
// The sheet cut into its tiles, row by row (same format as the sheet).
std::vector<Image> sliceSheet(const Image& sheet, int tileWidth, int tileHeight, int margin, int spacing);

// A guess at the tile size of a sheet with no margin or spacing, from where
// the picture changes most sharply: the size whose grid lines sit on those
// edges. 0 x 0 when nothing stands out (tiles that run into each other, a
// single tile, a flat image). Width and height are found separately.
struct TileSizeGuess {
    int width = 0;
    int height = 0;
    bool found() const { return width > 0 && height > 0; }
};
TileSizeGuess detectTileSize(const Image& sheet);

// What importing a sheet's tiles into `set` adds: the tiles to append after
// its existing ones, and the tilemap (Tilemap32, columns x rows) that lays them
// out as the sheet shows them. `slices` are the sheet's tiles in the document's
// format, row by row, `transparent` its transparent pixel value. A blank tile
// (with `skipEmpty`, or `merge`) is the empty tile 0 instead of a new one; with
// `merge`, a tile equal to one the tileset has or an earlier one reuses it.
struct SheetImport {
    std::vector<std::shared_ptr<Image>> added;
    Image map;
};
SheetImport importSheetTiles(const Tileset& set, const std::vector<Image>& slices, int columns, int rows,
                             uint32_t transparent, bool skipEmpty, bool merge);

// --- tile management ---------------------------------------------------------

// What an edit of a tileset's tiles does: the tileset after it, and for each
// old tile index the new one (0 = the empty tile: cells showing a tile that
// went become empty). A tilemap cel is brought along with `remapTilemap`.
struct TileEdit {
    Tileset after;
    std::vector<uint32_t> map;
    bool changes = false; // false: nothing to do (after == before)
};

// The tilemap (Tilemap32) with each cell's tile index sent through `map` (its
// flip bits stay; indices past the map's end become empty).
Image remapTilemap(const Image& tilemap, const std::vector<uint32_t>& map);

// `set` without tile `index` (not 0).
TileEdit deleteTile(const Tileset& set, size_t index);
// Tile `from` moved to place `to` (neither 0), the ones between shifting over.
TileEdit moveTile(const Tileset& set, size_t from, size_t to);
// Tiles no cell uses (`used[i]`: tile i is used) removed; tile 0 stays.
TileEdit removeUnusedTiles(const Tileset& set, const std::vector<bool>& used);
// Tiles whose pixels equal an earlier tile's merged into it.
TileEdit mergeDuplicateTiles(const Tileset& set);

// How many tiles `removeUnusedTiles` would remove / `mergeDuplicateTiles` would merge.
size_t countUnusedTiles(const Tileset& set, const std::vector<bool>& used);
size_t countDuplicateTiles(const Tileset& set);

} // namespace core
