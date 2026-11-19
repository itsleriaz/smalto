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
using core::kMaxImageSize;
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

// --- Sprite sheets -------------------------------------------------------------------------------

// How a sprite sheet is cut: the first tile is the box x, y, width x height; the rest follow it
// `padX` / `padY` apart, `columns` x `rows` in all. Tiles reach past the sheet's edge only when the
// counts say so (what is missing is transparent).
enum class SheetOrder { Rows, Columns }; // the order the tiles become frames in
struct SheetSlice {
    int x = 0;
    int y = 0;
    int width = 16;
    int height = 16;
    int padX = 0;
    int padY = 0;
    int columns = 1;
    int rows = 1;
    SheetOrder order = SheetOrder::Rows;
};

// The columns and rows that fit a sheet of this size from the box's corner on; with `partial`, a
// tile cut off by the right / bottom edge counts too. Zero when the box is empty or outside.
struct SheetFit {
    int columns = 0;
    int rows = 0;
};
SheetFit fitSheetSlice(int sheetWidth, int sheetHeight, const SheetSlice& slice, bool partial);

// The tile boxes in frame order (columns x rows of them, none if either is below 1).
std::vector<core::Rect> sheetSliceCells(const SheetSlice& slice);

// The tiles as RGBA8 images of width x height, in frame order.
std::vector<core::Image> sliceSheet(const core::Image& sheet, const SheetSlice& slice);

// --- Folders of animations -----------------------------------------------------------------------

// One animation found in a folder: the frames of a numbered sequence (walk-1.png, walk-2.png, ...) or
// a single image, under the name its tag gets.
struct ImageClip {
    std::string name;
    std::vector<std::filesystem::path> files; // in frame order
};

// The animations in a folder and its subfolders: files that share a folder and a name but for the
// number at its end make one clip (in numeric order), any other image is a clip of one file. A clip
// without a name of its own ("1.png", "2.png") is named for its folder. Names are unique; clips come
// sorted by name (numbers in natural order).
std::vector<ImageClip> findImageClips(const std::filesystem::path& folder);

// RGBA document of the clips' frames one after another (a GIF adds all its frames, with their
// delays), a tag for each clip over its frames. Same canvas and palette rules as loadImageDocument.
std::unique_ptr<core::Document> loadClipsDocument(const std::vector<ImageClip>& clips,
                                                  std::string* error = nullptr);

// RGBA document with one layer and one frame per image (the canvas fits the largest), the palette of
// the images' colors when there are at most 256. Durations are in ms.
std::unique_ptr<core::Document> documentFromImages(std::vector<ImageFrame> frames);

} // namespace formats
