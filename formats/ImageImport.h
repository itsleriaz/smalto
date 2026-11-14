#pragma once

#include "core/Document.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Image import: PNG, JPEG, BMP, TGA and GIF (all frames).
namespace formats {

// Largest image we turn into a document or layer (matches the editor's limit).
constexpr int kMaxImageSize = 4096;
// GIF frames shorter than this play at kGifDefaultDelayMs, as browsers do.
constexpr int kGifMinDelayMs = 20;
constexpr int kGifDefaultDelayMs = 100;

// True for the extensions we open as images: "png", "jpg", "jpeg", "bmp",
// "tga", "gif" (any case, with or without the dot).
bool isImageExtension(std::string_view extension);

struct ImageFrame {
    core::Image image; // RGBA8, straight alpha
    int durationMs = 100;
};

// Every frame of an image as RGBA8: a GIF gives each frame as shown (with
// earlier frames under it, as the file says) and its delay; other formats
// give one frame of 100 ms. The format is found from the data.
std::optional<std::vector<ImageFrame>> decodeImageFrames(std::span<const uint8_t> data,
                                                         std::string* error = nullptr);
std::optional<std::vector<ImageFrame>> loadImageFrames(const std::filesystem::path& path,
                                                       std::string* error = nullptr);

// The first frame only.
std::optional<core::Image> decodeImage(std::span<const uint8_t> data, std::string* error = nullptr);
std::optional<core::Image> loadImage(const std::filesystem::path& path, std::string* error = nullptr);

// The orientation a JPEG says its pixels are stored in (EXIF, 1..8; 1 when there is none or it isn't a
// JPEG): 1 as stored, 2 mirrored, 3 turned 180, 4 flipped, 5 transposed, 6 to be turned 90 clockwise,
// 7 transverse, 8 to be turned 90 counterclockwise.
int jpegOrientation(std::span<const uint8_t> data);
// `image` upright, given such an orientation.
core::Image orientedImage(const core::Image& image, int orientation);

// The numbered sequence `file` belongs to, in numeric order: for walk-2.png that is every
// walk-<digits> next to it, whatever its image type (png, jpg, ...; where one number has several
// types, the one `file` has wins). Just {file} if there is none.
std::vector<std::filesystem::path> findImageSequence(const std::filesystem::path& file);

// RGBA document with one layer and the frames of every file in order (a GIF
// adds all of its frames, with their delays; the canvas fits the largest
// image). The palette holds the images' colors when there are at most 256 of
// them, else the default palette.
std::unique_ptr<core::Document> loadImageDocument(const std::vector<std::filesystem::path>& files,
                                                  std::string* error = nullptr);

} // namespace formats
