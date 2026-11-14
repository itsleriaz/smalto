#pragma once

#include "core/Palette.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Palette files.
namespace formats {

// Most colors a palette may hold (an indexed sprite's limit).
constexpr int kMaxPaletteSize = 256;

struct PaletteFile {
    core::Palette palette;
    std::string name; // from the file when it has one (GIMP "Name:")
};

// GIMP palette (.gpl): "GIMP Palette", optional "Name:" / "Columns:" /
// "Channels: RGBA" (Aseprite's extension, a fourth alpha column), "#"
// comments, then "r g b [name]" per line.
std::optional<PaletteFile> readGpl(std::string_view text, std::string* error = nullptr);
// Alpha is written (as "Channels: RGBA") only when some entry isn't opaque.
std::string writeGpl(const core::Palette& palette, std::string_view name);

// Lospec-style .hex: one RRGGBB (or RRGGBBAA) per line.
std::optional<PaletteFile> readHex(std::string_view text, std::string* error = nullptr);
std::string writeHex(const core::Palette& palette);

// JASC palette (.pal, Paint Shop Pro): "JASC-PAL", "0100", the count, then
// "r g b" per line. Also read: the RIFF .pal Windows writes ("PAL " /
// "data": version, count, then r g b flags per entry). Alpha isn't stored.
std::optional<PaletteFile> readPal(std::span<const uint8_t> data, std::string* error = nullptr);
std::string writePal(const core::Palette& palette);

// Photoshop color table (.act): up to 256 r g b triples, padded to 768
// bytes; a 4-byte trailer (count, then the transparent index, 16-bit big
// endian, 0xFFFF for none) says how many are used. Without it all 256 are.
// The transparent index becomes a fully transparent entry.
std::optional<PaletteFile> readAct(std::span<const uint8_t> data, std::string* error = nullptr);
std::vector<uint8_t> writeAct(const core::Palette& palette);

// A palette from file contents, picked by extension (lower case, with dot):
// .gpl, .hex, .pal, .act, .aseprite / .ase (the sprite's palette), or an image (.png,
// .gif, .bmp, .tga, .jpg: the first frame's distinct colors in reading
// order, fully transparent pixels skipped; at most 256).
std::optional<PaletteFile> readPalette(std::span<const uint8_t> data, std::string_view extension,
                                       std::string* error = nullptr);
// .gpl, .hex or .pal contents by extension; nullopt for other extensions
// (use writeAct for .act, which is binary).
std::optional<std::string> writePalette(const core::Palette& palette, std::string_view name,
                                        std::string_view extension);

} // namespace formats
