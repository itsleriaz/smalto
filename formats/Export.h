#pragma once

#include "core/Document.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Image and animation export: PNG, animated GIF and sprite sheets.
namespace formats {

struct ExportOptions {
    int fromFrame = 0;
    int toFrame = -1; // inclusive; -1 = last frame
    int scale = 1;    // integer nearest-neighbor upscale, 1..64
    // PNG and sprite sheet images: an indexed PNG (a palette, with alpha in
    // tRNS) when there are at most 256 different colors, otherwise RGBA.
    bool indexedPng = false;
};

enum class SheetLayout {
    Horizontal,
    Vertical,
    Grid,
    TagRows, // one row per tag, in tag order (ignores the frame range)
};

struct SheetOptions : ExportOptions {
    SheetLayout layout = SheetLayout::Horizontal;
    int columns = 0; // grid only; 0 = about square
    int padding = 0; // pixels between frames
};

// Composited frame, scaled. Always RGBA8.
core::Image renderScaled(const core::Document& doc, int frame, int scale);

// PNG (RGBA, 8 bits per channel) of an RGBA8 image.
std::vector<uint8_t> encodePng(const core::Image& image);

// The same as an indexed PNG (1, 2, 4 or 8 bits per pixel, by the number of
// colors) when the image has at most 256 different ones (all fully
// transparent pixels count as one); otherwise encodePng's RGBA.
std::vector<uint8_t> encodePngIndexed(const core::Image& image);

// Looping animated GIF of the frame range. Semi-transparent pixels become
// opaque from alpha 128 up. One palette for all frames while they use at
// most 255 colors together; past that each frame gets its own (reduced by
// median cut only if that frame alone has more than 255).
std::vector<uint8_t> encodeGif(const core::Document& doc, const ExportOptions& options);

struct SpriteSheet {
    core::Image image;
    std::string json; // Aseprite-style "array" JSON: frames + meta.frameTags and meta.slices
};
// Frames of each sheet row, top to bottom. With TagRows and no tags it
// falls back to one row of the range.
std::vector<std::vector<int>> sheetRows(const core::Document& doc, const SheetOptions& options);

SpriteSheet buildSpriteSheet(const core::Document& doc, const SheetOptions& options,
                             const std::string& imageFileName);

// File writers. All return false and set `error` on failure.
//
// exportPng writes one file for a single frame; for a range it writes
// NAME-1.png, NAME-2.png, ... (numbers padded to the same width) next to `path`.
bool exportPng(const core::Document& doc, const ExportOptions& options, const std::filesystem::path& path,
               std::string* error = nullptr);
bool exportGif(const core::Document& doc, const ExportOptions& options, const std::filesystem::path& path,
               std::string* error = nullptr);
// Writes the sheet to `path` (.png) and its JSON next to it (.json).
bool exportSpriteSheet(const core::Document& doc, const SheetOptions& options, const std::filesystem::path& path,
                       std::string* error = nullptr);

// Output files exportPng would write, for UI confirmation of overwrites.
std::vector<std::filesystem::path> pngExportPaths(const core::Document& doc, const ExportOptions& options,
                                                  const std::filesystem::path& path);

} // namespace formats
