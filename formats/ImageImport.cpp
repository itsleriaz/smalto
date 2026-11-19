#include "formats/ImageImport.h"

#include "core/Transform.h"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_set>

using namespace core;

namespace formats {

namespace {

bool fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
    return false;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Splits "walk-12" into ("walk-", "12"); digits empty if none.
std::pair<std::string, std::string> splitNumber(const std::string& stem)
{
    size_t i = stem.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(stem[i - 1])))
        --i;
    return {stem.substr(0, i), stem.substr(i)};
}

} // namespace

namespace {

// Clears the color of fully transparent pixels.
Image rgbaImage(const stbi_uc* pixels, int w, int h)
{
    Image image(w, h);
    std::memcpy(image.data(), pixels, size_t(w) * size_t(h) * 4);
    auto* p = reinterpret_cast<uint32_t*>(image.data());
    for (size_t i = 0, n = size_t(w) * size_t(h); i < n; ++i)
        if ((p[i] >> 24) == 0)
            p[i] = 0;
    return image;
}

bool tooLarge(int w, int h, std::string* error)
{
    if (w <= kMaxImageSize && h <= kMaxImageSize)
        return false;
    return !fail(error, "image is larger than " + std::to_string(kMaxImageSize) + " x " +
                            std::to_string(kMaxImageSize));
}

} // namespace

int jpegOrientation(std::span<const uint8_t> data)
{
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return 1;
    size_t at = 2;
    while (at + 4 <= data.size()) {
        if (data[at] != 0xFF) // out of step with the markers
            return 1;
        const uint8_t marker = data[at + 1];
        if (marker == 0xFF) { // padding
            ++at;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) // the picture starts: no EXIF before it
            return 1;
        const size_t length = size_t(data[at + 2]) << 8 | data[at + 3];
        if (length < 2 || at + 2 + length > data.size())
            return 1;
        const uint8_t* seg = &data[at + 4];
        const size_t size = length - 2;
        // APP1: "Exif\0\0", then a TIFF header and its first directory.
        if (marker == 0xE1 && size >= 14 && std::memcmp(seg, "Exif\0\0", 6) == 0) {
            const uint8_t* tiff = seg + 6;
            const size_t tiffSize = size - 6;
            const bool little = tiff[0] == 'I' && tiff[1] == 'I';
            if (!little && !(tiff[0] == 'M' && tiff[1] == 'M'))
                return 1;
            const auto u16 = [&](size_t o) -> unsigned {
                return o + 2 <= tiffSize ? (little ? tiff[o] | tiff[o + 1] << 8 : tiff[o] << 8 | tiff[o + 1]) : 0u;
            };
            const auto u32 = [&](size_t o) -> size_t {
                return o + 4 <= tiffSize ? (little ? size_t(u16(o)) | size_t(u16(o + 2)) << 16
                                                   : size_t(u16(o)) << 16 | size_t(u16(o + 2)))
                                         : 0u;
            };
            size_t ifd = u32(4);
            const unsigned entries = u16(ifd);
            for (unsigned i = 0; i < entries && ifd + 2 + (i + 1) * 12 <= tiffSize; ++i) {
                const size_t e = ifd + 2 + i * 12;
                if (u16(e) == 0x0112) { // Orientation, a SHORT
                    const unsigned v = u16(e + 8);
                    return v >= 1 && v <= 8 ? int(v) : 1;
                }
            }
            return 1;
        }
        at += 2 + length;
    }
    return 1;
}

Image orientedImage(const Image& image, int orientation)
{
    switch (orientation) {
    case 2: return flippedHorizontally(image);
    case 3: return rotated90(rotated90(image, true), true);
    case 4: return flippedVertically(image);
    case 5: return flippedHorizontally(rotated90(image, true)); // transposed
    case 6: return rotated90(image, true);
    case 7: return rotated90(flippedHorizontally(image), true); // transverse
    case 8: return rotated90(image, false);
    default: return image;
    }
}

namespace {

bool isGif(std::span<const uint8_t> data)
{
    return data.size() >= 6 && std::memcmp(data.data(), "GIF8", 4) == 0;
}

std::optional<std::vector<ImageFrame>> decodeGif(std::span<const uint8_t> data, std::string* error)
{
    // The logical screen size comes first: refuse big ones before decoding every frame.
    if (data.size() >= 10 && tooLarge(data[6] | data[7] << 8, data[8] | data[9] << 8, error))
        return std::nullopt;
    int* delays = nullptr;
    int w = 0, h = 0, count = 0, channels = 0;
    stbi_uc* pixels = stbi_load_gif_from_memory(data.data(), int(std::min<size_t>(data.size(), INT32_MAX)),
                                                &delays, &w, &h, &count, &channels, 4);
    if (!pixels) {
        fail(error, std::string("not a readable GIF (") + stbi_failure_reason() + ")");
        return std::nullopt;
    }
    std::optional<std::vector<ImageFrame>> frames;
    if (!tooLarge(w, h, error)) {
        frames.emplace();
        const size_t bytes = size_t(w) * size_t(h) * 4;
        for (int i = 0; i < count; ++i) {
            const int delay = delays ? delays[i] : 0;
            frames->push_back({rgbaImage(pixels + bytes * size_t(i), w, h),
                               delay < kGifMinDelayMs ? kGifDefaultDelayMs : delay});
        }
    }
    stbi_image_free(pixels);
    std::free(delays); // stb allocates with malloc
    if (frames && frames->empty()) {
        fail(error, "the GIF has no frames");
        return std::nullopt;
    }
    return frames;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

void putLe32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = uint8_t(v >> (8 * i));
}

// stb_image misreads BMPs with the 56-byte V3 info header (what Aseprite
// writes): it skips the header's color masks and reads pixels as masks. The
// same file with a V4 header (the V3 fields plus 52 bytes of color space
// info, zero here) reads right. Empty if `data` isn't such a BMP.
std::vector<uint8_t> bmpV3AsV4(std::span<const uint8_t> data)
{
    constexpr size_t kFileHeader = 14, kV3 = 56, kV4 = 108, kGrow = kV4 - kV3;
    if (data.size() < kFileHeader + kV3 || data[0] != 'B' || data[1] != 'M' || le32(&data[14]) != kV3)
        return {};
    std::vector<uint8_t> out(data.begin(), data.begin() + kFileHeader + kV3);
    out.resize(out.size() + kGrow, 0);
    out.insert(out.end(), data.begin() + kFileHeader + kV3, data.end());
    putLe32(&out[2], uint32_t(out.size()));
    putLe32(&out[10], le32(&data[10]) + uint32_t(kGrow)); // pixel data offset
    putLe32(&out[14], uint32_t(kV4));
    return out;
}

std::optional<std::vector<uint8_t>> readAll(const std::filesystem::path& path, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail(error, "cannot open file");
        return std::nullopt;
    }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

bool isImageExtension(std::string_view extension)
{
    if (!extension.empty() && extension.front() == '.')
        extension.remove_prefix(1);
    const std::string ext = lower(std::string(extension));
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "gif";
}

std::optional<std::vector<ImageFrame>> decodeImageFrames(std::span<const uint8_t> data, std::string* error)
{
    if (isGif(data))
        return decodeGif(data, error);
    auto image = decodeImage(data, error);
    if (!image)
        return std::nullopt;
    std::vector<ImageFrame> frames;
    frames.push_back({std::move(*image), 100});
    return frames;
}

std::optional<std::vector<ImageFrame>> loadImageFrames(const std::filesystem::path& path, std::string* error)
{
    const auto data = readAll(path, error);
    if (!data)
        return std::nullopt;
    return decodeImageFrames(*data, error);
}

std::optional<Image> decodeImage(std::span<const uint8_t> data, std::string* error)
{
    // stb reads a GIF's first frame by itself, but without the size check.
    if (isGif(data) && data.size() >= 10 && tooLarge(data[6] | data[7] << 8, data[8] | data[9] << 8, error))
        return std::nullopt;
    const std::vector<uint8_t> fixedBmp = bmpV3AsV4(data);
    if (!fixedBmp.empty())
        data = fixedBmp;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), int(std::min<size_t>(data.size(), INT32_MAX)), &w, &h,
                                            &channels, 4);
    if (!pixels) {
        fail(error, std::string("not a readable image (") + stbi_failure_reason() + ")");
        return std::nullopt;
    }
    std::optional<Image> image;
    if (!tooLarge(w, h, error))
        image = rgbaImage(pixels, w, h);
    stbi_image_free(pixels);
    // A JPEG from a camera or a phone is stored sideways with a note saying how to turn it.
    if (image) {
        if (const int orientation = jpegOrientation(data); orientation > 1)
            image = orientedImage(*image, orientation);
    }
    return image;
}

std::optional<Image> loadImage(const std::filesystem::path& path, std::string* error)
{
    const auto data = readAll(path, error);
    if (!data)
        return std::nullopt;
    return decodeImage(*data, error);
}

std::vector<std::filesystem::path> findImageSequence(const std::filesystem::path& file)
{
    const auto [prefix, digits] = splitNumber(file.stem().string());
    if (digits.empty())
        return {file};
    const std::string ext = lower(file.extension().string());

    std::vector<std::pair<std::filesystem::path, std::string>> found; // path, number
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(file.parent_path().empty() ? "." : file.parent_path(), ec)) {
        if (!entry.is_regular_file(ec) || !isImageExtension(entry.path().extension().string()))
            continue;
        const auto [p, d] = splitNumber(entry.path().stem().string());
        if (p == prefix && !d.empty())
            found.push_back({file.parent_path() / entry.path().filename(), d});
    }
    // One file for each number: where several types share it, the one `file` has, else the first.
    const auto number = [](const std::string& s) { return s.substr(std::min(s.find_first_not_of('0'), s.size())); };
    std::sort(found.begin(), found.end(), [&](const auto& a, const auto& b) {
        const bool sa = lower(a.first.extension().string()) == ext, sb = lower(b.first.extension().string()) == ext;
        return sa != sb ? sa : a.first < b.first;
    });
    std::vector<std::pair<std::filesystem::path, std::string>> unique;
    for (const auto& item : found) {
        const bool seen = std::any_of(unique.begin(), unique.end(),
                                      [&](const auto& u) { return number(u.second) == number(item.second); });
        if (!seen)
            unique.push_back(item);
    }
    found = std::move(unique);
    if (found.size() < 2)
        return {file};
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        // Numeric order (compare without leading zeros), then by name.
        auto strip = [](const std::string& s) { return s.substr(std::min(s.find_first_not_of('0'), s.size())); };
        const std::string x = strip(a.second), y = strip(b.second);
        if (x.size() != y.size())
            return x.size() < y.size();
        return x != y ? x < y : a.first < b.first;
    });
    std::vector<std::filesystem::path> out;
    for (const auto& [path, number] : found)
        out.push_back(path);
    return out;
}

std::unique_ptr<Document> documentFromImages(std::vector<ImageFrame> frames)
{
    int w = 1, h = 1;
    for (const ImageFrame& frame : frames) {
        w = std::max(w, frame.image.width());
        h = std::max(h, frame.image.height());
    }
    auto doc = std::make_unique<Document>(w, h);
    for (int f = 1; f < int(frames.size()); ++f)
        doc->insertFrame(f, Frame{});
    Layer* layer = doc->insertLayer(doc->root(), 0, doc->makeImageLayer("Layer 1"));

    std::vector<uint32_t> colors;
    std::unordered_set<uint32_t> seen;
    for (size_t f = 0; f < frames.size(); ++f) {
        doc->frame(int(f)).durationMs = frames[f].durationMs;
        const Image& img = frames[f].image;
        for (int y = 0; y < img.height() && colors.size() <= 256; ++y)
            for (int x = 0; x < img.width() && colors.size() <= 256; ++x)
                if (const uint32_t v = img.pixel(x, y); (v >> 24) != 0 && seen.insert(v).second)
                    colors.push_back(v);
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(std::move(frames[f].image));
        layer->cels[f] = cel;
    }
    if (colors.size() <= 256) {
        for (uint32_t c : colors)
            doc->palette.entries.push_back({unpack(c), {}});
    }
    if (doc->palette.size() == 0)
        doc->palette = Palette::defaultPalette();
    return doc;
}

std::unique_ptr<Document> loadImageDocument(const std::vector<std::filesystem::path>& files, std::string* error)
{
    if (files.empty()) {
        fail(error, "no files");
        return nullptr;
    }
    std::vector<ImageFrame> frames;
    for (const auto& path : files) {
        std::string message;
        auto loaded = loadImageFrames(path, &message);
        if (!loaded) {
            fail(error, path.filename().string() + ": " + message);
            return nullptr;
        }
        for (ImageFrame& frame : *loaded)
            frames.push_back(std::move(frame));
    }
    return documentFromImages(std::move(frames));
}

std::unique_ptr<Document> loadClipsDocument(const std::vector<ImageClip>& clips, std::string* error)
{
    std::vector<ImageFrame> frames;
    std::vector<Tag> tags;
    // The colors "New Tag" cycles through, so imported tags look like ones made by hand.
    static constexpr uint32_t kTagColors[] = {0x6e8bff, 0xff6b6b, 0x51cf66, 0xfcc419, 0xcc5de8, 0x22b8cf};
    for (const ImageClip& clip : clips) {
        const int first = int(frames.size());
        for (const auto& path : clip.files) {
            std::string message;
            auto loaded = loadImageFrames(path, &message);
            if (!loaded) {
                fail(error, path.filename().string() + ": " + message);
                return nullptr;
            }
            for (ImageFrame& frame : *loaded)
                frames.push_back(std::move(frame));
        }
        if (int(frames.size()) > first) {
            Tag tag;
            tag.name = clip.name;
            tag.from = first;
            tag.to = int(frames.size()) - 1;
            tag.color = Rgba::fromHex(kTagColors[tags.size() % std::size(kTagColors)]);
            tags.push_back(std::move(tag));
        }
    }
    if (frames.empty()) {
        fail(error, "no images");
        return nullptr;
    }
    auto doc = documentFromImages(std::move(frames));
    doc->tags = std::move(tags);
    return doc;
}

SheetFit fitSheetSlice(int sheetWidth, int sheetHeight, const SheetSlice& s, bool partial)
{
    if (s.width < 1 || s.height < 1 || s.x < 0 || s.y < 0 || s.x >= sheetWidth || s.y >= sheetHeight)
        return {};
    // Tile k starts at k * (size + pad). It fits whole when it ends inside the sheet; it counts as a
    // partial tile when it merely starts inside.
    const auto count = [&](int room, int size, int pad) {
        if (partial)
            return (room + size + pad - 1) / (size + pad);
        return room >= size ? (room - size) / (size + pad) + 1 : 0;
    };
    return {count(sheetWidth - s.x, s.width, s.padX), count(sheetHeight - s.y, s.height, s.padY)};
}

std::vector<Rect> sheetSliceCells(const SheetSlice& s)
{
    std::vector<Rect> out;
    if (s.columns < 1 || s.rows < 1 || s.width < 1 || s.height < 1)
        return out;
    out.reserve(size_t(s.columns) * size_t(s.rows));
    const auto cell = [&](int col, int row) {
        return Rect{s.x + col * (s.width + s.padX), s.y + row * (s.height + s.padY), s.width, s.height};
    };
    if (s.order == SheetOrder::Rows) {
        for (int row = 0; row < s.rows; ++row)
            for (int col = 0; col < s.columns; ++col)
                out.push_back(cell(col, row));
    } else {
        for (int col = 0; col < s.columns; ++col)
            for (int row = 0; row < s.rows; ++row)
                out.push_back(cell(col, row));
    }
    return out;
}

std::vector<Image> sliceSheet(const Image& sheet, const SheetSlice& slice)
{
    std::vector<Image> out;
    for (const Rect& cell : sheetSliceCells(slice)) {
        Image tile(cell.w, cell.h);
        tile.clear(0);
        // blit clips: what lies outside the sheet stays transparent.
        const Rect src = Rect{cell.x, cell.y, cell.w, cell.h};
        const Image part = sheet.copy(src);
        if (!part.isNull())
            tile.blit(part, {std::max(0, -cell.x), std::max(0, -cell.y)});
        out.push_back(std::move(tile));
    }
    return out;
}

namespace {

// Compares two names with their digit runs as numbers ("a2" before "a10").
bool naturalLess(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
            size_t ie = i, je = j;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie])))
                ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je])))
                ++je;
            auto strip = [](const std::string& s, size_t from, size_t to) {
                while (from + 1 < to && s[from] == '0')
                    ++from;
                return s.substr(from, to - from);
            };
            const std::string p = strip(a, i, ie), q = strip(b, j, je);
            if (p.size() != q.size())
                return p.size() < q.size();
            if (p != q)
                return p < q;
            i = ie;
            j = je;
        } else {
            const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[j]));
            if (ca != cb)
                return ca < cb;
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

std::string trimSeparators(std::string s)
{
    const auto sep = [](char c) { return c == '-' || c == '_' || c == ' ' || c == '.'; };
    while (!s.empty() && sep(s.back()))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && sep(s[i]))
        ++i;
    return s.substr(i);
}

} // namespace

std::vector<ImageClip> findImageClips(const std::filesystem::path& folder)
{
    struct Entry {
        std::filesystem::path path;
        std::string number; // digits at the end of the stem, may be empty
    };
    struct Group {
        std::filesystem::path dir;
        std::string prefix;
        std::vector<Entry> entries;
    };
    std::vector<Group> groups;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        if (!entry.is_regular_file(ec) || !isImageExtension(entry.path().extension().string()))
            continue;
        const std::string stem = entry.path().stem().string();
        if (!stem.empty() && stem[0] == '.')
            continue; // hidden files (macOS "._name.png" resource forks)
        auto [prefix, digits] = splitNumber(stem);
        // A file with no number is its own clip: give it its whole name so that "idle.png" and
        // "jump.png" don't meet.
        if (digits.empty())
            prefix = stem;
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) {
            return g.dir == entry.path().parent_path() && g.prefix == prefix;
        });
        if (found == groups.end()) {
            groups.push_back({entry.path().parent_path(), prefix, {}});
            found = groups.end() - 1;
        }
        found->entries.push_back({entry.path(), digits});
    }

    const auto stripZeros = [](const std::string& s) { return s.substr(std::min(s.find_first_not_of('0'), s.size())); };
    std::vector<ImageClip> clips;
    for (Group& g : groups) {
        // One file for each number (png before jpg ... by name when several types share one).
        std::sort(g.entries.begin(), g.entries.end(), [&](const Entry& a, const Entry& b) {
            const std::string x = stripZeros(a.number), y = stripZeros(b.number);
            if (x.size() != y.size())
                return x.size() < y.size();
            return x != y ? x < y : a.path < b.path;
        });
        ImageClip clip;
        for (size_t i = 0; i < g.entries.size(); ++i) {
            if (i > 0 && !g.entries[i].number.empty()
                && stripZeros(g.entries[i].number) == stripZeros(g.entries[i - 1].number))
                continue;
            clip.files.push_back(g.entries[i].path);
        }
        std::string name = trimSeparators(g.prefix);
        if (name.empty()) {
            name = g.dir.filename().string();
            if (name.empty())
                name = folder.filename().string();
            if (name.empty())
                name = "Animation";
        }
        clip.name = name;
        clips.push_back(std::move(clip));
    }
    std::sort(clips.begin(), clips.end(), [](const ImageClip& a, const ImageClip& b) {
        if (a.name != b.name)
            return naturalLess(a.name, b.name);
        // Same name: the one nearer the top keeps it when names are made unique below.
        const auto depth = [](const ImageClip& c) { return std::distance(c.files.front().begin(), c.files.front().end()); };
        return depth(a) != depth(b) ? depth(a) < depth(b) : a.files.front() < b.files.front();
    });
    // Unique names: the same name in two folders gets the folder's name in front, then a number.
    for (size_t i = 0; i < clips.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (clips[i].name != clips[j].name)
                continue;
            const std::string parent = clips[i].files.front().parent_path().filename().string();
            std::string candidate = parent.empty() ? clips[i].name : parent + "_" + clips[i].name;
            const auto taken = [&](const std::string& n) {
                return std::any_of(clips.begin(), clips.begin() + std::ptrdiff_t(i),
                                   [&](const ImageClip& c) { return c.name == n; });
            };
            for (int n = 2; taken(candidate); ++n)
                candidate = clips[i].name + "_" + std::to_string(n);
            clips[i].name = candidate;
            break;
        }
    }
    return clips;
}

} // namespace formats
