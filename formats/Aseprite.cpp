#include "formats/Aseprite.h"
#include "formats/LiveLayerData.h"
#include "formats/FileIO.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <unordered_map>

using namespace core;

namespace formats {

namespace {

// Chunk types from the spec.
enum : uint16_t {
    kOldPalette4 = 0x0004,
    kOldPalette11 = 0x0011,
    kLayerChunk = 0x2004,
    kCelChunk = 0x2005,
    kCelExtraChunk = 0x2006,
    kColorProfileChunk = 0x2007,
    kTagsChunk = 0x2018,
    kPaletteChunk = 0x2019,
    kUserDataChunk = 0x2020,
    kSliceChunk = 0x2022,
    kTilesetChunk = 0x2023,
    // Smalto's own (a number the spec leaves unused; other programs skip chunks they don't know):
    // a version word, then the drawing symmetry's crossing point (twice its x and y, two dwords).
    kSmaltoChunk = 0x7a01,
};

constexpr uint16_t kFileMagic = 0xA5E0;
constexpr uint16_t kFrameMagic = 0xF1FA;
constexpr int kHeaderSize = 128;
constexpr int kChunkHeaderSize = 6;

// Header flags.
constexpr uint32_t kLayerOpacityValid = 1;
constexpr uint32_t kGroupOpacityValid = 2;
constexpr uint32_t kLayerUuids = 4;

// Layer flags we model; everything else goes to Layer::extraFlags.
constexpr uint16_t kLayerVisible = 1;
constexpr uint16_t kLayerEditable = 2;

// 32-bit tilemap masks as Aseprite writes them (tile id, x flip, y flip,
// diagonal flip).
constexpr uint32_t kTileMasks[4] = {0x1fffffff, 0x80000000, 0x40000000, 0x20000000};

// User data's color (see below), if it has one.
std::optional<Rgba> userDataColor(const Bytes& data);

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------- reading

class Reader {
public:
    Reader(const uint8_t* data, size_t size)
        : m_data(data)
        , m_size(size)
    {
    }

    size_t pos() const { return m_pos; }
    size_t remaining() const { return m_size - m_pos; }
    void seek(size_t pos)
    {
        if (pos > m_size)
            throw FormatError("unexpected end of file");
        m_pos = pos;
    }
    void skip(size_t n) { seek(m_pos + n); }

    const uint8_t* bytes(size_t n)
    {
        need(n);
        const uint8_t* p = m_data + m_pos;
        m_pos += n;
        return p;
    }

    uint8_t byte() { return *bytes(1); }
    uint16_t word()
    {
        const uint8_t* p = bytes(2);
        return uint16_t(p[0] | p[1] << 8);
    }
    int16_t shortValue() { return int16_t(word()); }
    uint32_t dword()
    {
        const uint8_t* p = bytes(4);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    std::string string()
    {
        const uint16_t n = word();
        const uint8_t* p = bytes(n);
        return std::string(reinterpret_cast<const char*>(p), n);
    }

private:
    void need(size_t n) const
    {
        if (n > m_size - m_pos)
            throw FormatError("unexpected end of file");
    }

    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

std::vector<uint8_t> inflate(const uint8_t* src, size_t srcSize, size_t expected)
{
    std::vector<uint8_t> out(expected);
    if (expected == 0)
        return out;
    uLongf outSize = uLongf(expected);
    const int rc = uncompress(out.data(), &outSize, src, uLong(srcSize));
    if (rc != Z_OK || outSize != expected)
        throw FormatError("corrupt compressed cel data");
    return out;
}

Image readPixels(const uint8_t* src, int w, int h, PixelFormat format)
{
    Image image(w, h, format);
    std::memcpy(image.data(), src, size_t(w) * size_t(h) * size_t(image.bpp()));
    return image;
}

class AsepriteReader {
public:
    AsepriteReader(std::span<const uint8_t> data)
        : m_in(data.data(), data.size())
    {
    }

    std::unique_ptr<Document> read()
    {
        readHeader();
        for (int f = 0; f < m_frameCount; ++f)
            readFrame(f);
        if (!m_hasNewPalette && !m_oldPalette.entries.empty())
            m_doc->palette = std::move(m_oldPalette);
        return std::move(m_doc);
    }

private:
    void readHeader()
    {
        if (m_in.remaining() < kHeaderSize)
            throw FormatError("not an Aseprite file");
        m_in.dword(); // file size
        if (m_in.word() != kFileMagic)
            throw FormatError("not an Aseprite file");
        m_frameCount = m_in.word();
        const int width = m_in.word();
        const int height = m_in.word();
        const int depth = m_in.word();
        m_flags = m_in.dword();
        m_in.word();  // speed (deprecated)
        m_in.skip(8); // reserved
        const int transparentIndex = m_in.byte();
        m_in.skip(3);
        m_in.word(); // number of colors
        const int pixelW = m_in.byte();
        const int pixelH = m_in.byte();
        const int gridX = m_in.shortValue();
        const int gridY = m_in.shortValue();
        const int gridW = m_in.word();
        const int gridH = m_in.word();
        m_in.seek(kHeaderSize);

        ColorMode mode;
        switch (depth) {
        case 32: mode = ColorMode::Rgba; break;
        case 16: mode = ColorMode::Grayscale; break;
        case 8: mode = ColorMode::Indexed; break;
        default: throw FormatError("unsupported color depth " + std::to_string(depth));
        }
        if (width <= 0 || height <= 0 || m_frameCount <= 0)
            throw FormatError("invalid sprite size");

        m_doc = std::make_unique<Document>(width, height, mode);
        for (int f = 1; f < m_frameCount; ++f)
            m_doc->insertFrame(f, Frame{});
        m_doc->transparentIndex = transparentIndex;
        m_doc->pixelRatio = {pixelW ? pixelW : 1, pixelH ? pixelH : 1};
        m_doc->grid = {gridX, gridY, gridW, gridH};
    }

    void readFrame(int f)
    {
        const size_t frameStart = m_in.pos();
        const uint32_t frameBytes = m_in.dword();
        if (m_in.word() != kFrameMagic)
            throw FormatError("bad frame header");
        const uint16_t oldCount = m_in.word();
        const int duration = m_in.word();
        m_in.skip(2);
        const uint32_t newCount = m_in.dword();
        const uint32_t count = newCount ? newCount : oldCount;
        m_doc->frame(f).durationMs = duration;

        const size_t frameEnd = frameStart + frameBytes;
        m_target = Target::None;
        m_seenLayersOrCels = false;
        for (uint32_t i = 0; i < count && m_in.pos() + kChunkHeaderSize <= frameEnd; ++i) {
            const size_t start = m_in.pos();
            const uint32_t size = m_in.dword();
            const uint16_t type = m_in.word();
            if (size < kChunkHeaderSize || start + size > frameEnd)
                throw FormatError("bad chunk size");
            Reader chunk(m_in.bytes(size - kChunkHeaderSize), size - kChunkHeaderSize);
            readChunk(f, type, chunk, start, size);
            m_in.seek(start + size);
        }
        m_in.seek(frameEnd);
    }

    void readChunk(int f, uint16_t type, Reader& in, size_t start, uint32_t size)
    {
        switch (type) {
        case kLayerChunk:
            if (f == 0) {
                readLayer(in);
                return;
            }
            break;
        case kCelChunk:
            if (readCel(f, in))
                return;
            break;
        case kCelExtraChunk:
            if (m_target == Target::Cel) {
                m_cel->extraData = rest(in);
                return; // keep m_target: user data may follow
            }
            break;
        case kTagsChunk:
            if (f == 0 && m_doc->tags.empty()) {
                readTags(in);
                return;
            }
            break;
        case kPaletteChunk:
            if (f == 0 && !m_hasNewPalette) {
                readPalette(in);
                m_target = Target::Sprite;
                return;
            }
            break;
        case kOldPalette4:
        case kOldPalette11:
            if (f == 0) {
                readOldPalette(in, type == kOldPalette11);
                m_target = Target::Sprite;
                return;
            }
            break;
        case kUserDataChunk:
            if (attachUserData(rest(in)))
                return;
            break;
        case kSliceChunk:
            readSlice(in);
            return;
        case kSmaltoChunk:
            if (f == 0 && size == kChunkHeaderSize + 10 && in.word() == 1) {
                const int x = int(in.dword()), y = int(in.dword());
                m_doc->symmetryCenter2 = Point{x, y};
                return;
            }
            break;
        case kTilesetChunk:
            if (readTileset(in))
                return;
            break;
        default:
            break;
        }
        keepOpaque(f, type, start, size);
    }

    static Bytes rest(Reader& in)
    {
        const size_t n = in.remaining();
        const uint8_t* p = in.bytes(n);
        return Bytes(p, p + n);
    }

    void readLayer(Reader& in)
    {
        m_seenLayersOrCels = true;
        const uint16_t flags = in.word();
        const uint16_t type = in.word();
        const int level = in.word();
        in.word(); // default width
        in.word(); // default height
        const uint16_t blend = in.word();
        const int opacity = in.byte();
        in.skip(3);
        std::string name = in.string();

        std::unique_ptr<Layer> layer;
        switch (type) {
        case 0: layer = m_doc->makeImageLayer(std::move(name)); break;
        case 1: layer = m_doc->makeGroupLayer(std::move(name)); break;
        case 2:
            layer = m_doc->makeImageLayer(std::move(name));
            layer->kind = LayerKind::Tilemap;
            layer->tilesetIndex = in.dword();
            break;
        default: throw FormatError("unknown layer type " + std::to_string(type));
        }
        if (m_flags & kLayerUuids) {
            const uint8_t* p = in.bytes(16);
            layer->uuid.assign(p, p + 16);
        }

        layer->visible = flags & kLayerVisible;
        layer->locked = !(flags & kLayerEditable);
        layer->extraFlags = flags & ~(kLayerVisible | kLayerEditable);
        const bool group = type == 1;
        const bool valid = (m_flags & kLayerOpacityValid) && (!group || (m_flags & kGroupOpacityValid));
        layer->opacity = valid ? opacity : 255;
        layer->blend = valid && blend < kBlendModeCount ? BlendMode(blend) : BlendMode::Normal;

        // Child level: parent is the most recent group one level up.
        if (level > int(m_groups.size()))
            throw FormatError("bad layer hierarchy");
        m_groups.resize(size_t(level));
        Layer& parent = level == 0 ? m_doc->root() : *m_groups.back();
        Layer* raw = m_doc->insertLayer(parent, int(parent.children.size()), std::move(layer));
        if (group)
            m_groups.push_back(raw);
        m_layers.push_back(raw);
        m_target = Target::Layer;
        m_layer = raw;
    }

    bool readCel(int f, Reader& in)
    {
        m_seenLayersOrCels = true;
        const int layerIndex = in.word();
        if (layerIndex >= int(m_layers.size()) || !m_layers[size_t(layerIndex)]->hasCels())
            return false;
        Layer& layer = *m_layers[size_t(layerIndex)];

        const int x = in.shortValue();
        const int y = in.shortValue();
        const int opacity = in.byte();
        const uint16_t type = in.word();
        const int zIndex = in.shortValue();
        in.skip(5);

        CelRef cel;
        if (type == 1) {
            const int linked = in.word();
            if (linked >= f || !layer.cels[size_t(linked)])
                throw FormatError("linked cel points to a missing cel");
            cel = layer.cels[size_t(linked)];
        } else {
            cel = std::make_shared<CelData>();
            cel->position = {x, y};
            cel->opacity = opacity;
            cel->zIndex = zIndex;
            const int w = in.word();
            const int h = in.word();
            switch (type) {
            case 0: { // raw
                const PixelFormat format = m_doc->pixelFormat();
                cel->image = std::make_shared<Image>(
                    readPixels(in.bytes(size_t(w) * h * bytesPerPixel(format)), w, h, format));
                break;
            }
            case 2: { // compressed image
                const PixelFormat format = m_doc->pixelFormat();
                const size_t n = in.remaining();
                const auto pixels = inflate(in.bytes(n), n, size_t(w) * h * bytesPerPixel(format));
                cel->image = std::make_shared<Image>(readPixels(pixels.data(), w, h, format));
                break;
            }
            case 3: { // compressed tilemap
                const int bits = in.word();
                uint32_t masks[4];
                for (auto& m : masks)
                    m = in.dword();
                in.skip(10);
                if (bits != 32 || !std::equal(masks, masks + 4, kTileMasks))
                    throw FormatError("unsupported tilemap encoding");
                const size_t n = in.remaining();
                const auto tiles = inflate(in.bytes(n), n, size_t(w) * h * 4);
                cel->image = std::make_shared<Image>(readPixels(tiles.data(), w, h, PixelFormat::Tilemap32));
                break;
            }
            default:
                throw FormatError("unknown cel type " + std::to_string(type));
            }
        }
        layer.cels[size_t(f)] = cel;
        m_target = Target::Cel;
        m_cel = cel.get();
        return true;
    }

    void readTags(Reader& in)
    {
        const int count = in.word();
        in.skip(8);
        for (int i = 0; i < count; ++i) {
            Tag tag;
            tag.from = in.word();
            tag.to = in.word();
            const int dir = in.byte();
            tag.direction = dir <= 3 ? AniDirection(dir) : AniDirection::Forward;
            tag.repeat = in.word();
            in.skip(6);
            const uint8_t* rgb = in.bytes(3);
            tag.color = {rgb[0], rgb[1], rgb[2], 255};
            in.skip(1);
            tag.name = in.string();
            m_doc->tags.push_back(std::move(tag));
        }
        m_target = Target::Tags;
        m_nextTag = 0;
    }

    void readSlice(Reader& in)
    {
        Slice slice;
        const uint32_t keys = in.dword();
        const uint32_t flags = in.dword();
        in.skip(4);
        slice.name = in.string();
        slice.nineSlice = flags & 1;
        slice.hasPivot = flags & 2;
        if (keys > 65536)
            throw FormatError("bad slice");
        const auto value = [&] { return int(int32_t(in.dword())); };
        for (uint32_t i = 0; i < keys; ++i) {
            SliceKey key;
            key.frame = value();
            key.bounds.x = value();
            key.bounds.y = value();
            key.bounds.w = value();
            key.bounds.h = value();
            if (slice.nineSlice) {
                key.center.x = value();
                key.center.y = value();
                key.center.w = value();
                key.center.h = value();
            }
            if (slice.hasPivot) {
                key.pivot.x = value();
                key.pivot.y = value();
            }
            slice.keys.push_back(key);
        }
        std::sort(slice.keys.begin(), slice.keys.end(), [](const SliceKey& a, const SliceKey& b) {
            return a.frame < b.frame;
        });
        m_doc->slices.push_back(std::move(slice));
        m_target = Target::Slice;
    }

    // False (kept as an opaque chunk) for what isn't modeled: tiles in an
    // external file only.
    bool readTileset(Reader& in)
    {
        const uint32_t id = in.dword();
        const uint32_t flags = in.dword();
        const uint32_t count = in.dword();
        const int w = in.word(), h = in.word();
        const int base = int16_t(in.word());
        in.skip(14);
        if (!(flags & 2) || id > 4096 || count > 65536 || w < 1 || h < 1 || w > 4096 || h > 4096)
            return false;
        Tileset set;
        set.flags = flags;
        set.tileWidth = w;
        set.tileHeight = h;
        set.baseIndex = base;
        set.name = in.string();
        if (flags & 1) {
            const uint8_t* ext = in.bytes(8);
            set.external.assign(ext, ext + 8);
        }
        const uint32_t size = in.dword();
        const PixelFormat format = m_doc->pixelFormat();
        const auto pixels = inflate(in.bytes(size), size, size_t(w) * h * count * bytesPerPixel(format));
        const Image all = readPixels(pixels.data(), w, h * int(count), format);
        for (uint32_t i = 0; i < count; ++i)
            set.tiles.push_back(std::make_shared<Image>(all.copy({0, int(i) * h, w, h})));
        if (m_doc->tilesets.size() <= id)
            m_doc->tilesets.resize(id + 1);
        m_doc->tilesets[id] = std::move(set);
        m_tileset = int(id);
        m_target = Target::Tileset;
        return true;
    }

    void readPalette(Reader& in)
    {
        const uint32_t size = in.dword();
        const uint32_t first = in.dword();
        const uint32_t last = in.dword();
        in.skip(8);
        if (size > 65536 || last < first || last >= size)
            throw FormatError("bad palette");
        auto& entries = m_doc->palette.entries;
        entries.resize(size);
        for (uint32_t i = first; i <= last; ++i) {
            const uint16_t flags = in.word();
            const uint8_t* c = in.bytes(4);
            entries[i].color = {c[0], c[1], c[2], c[3]};
            if (flags & 1)
                entries[i].name = in.string();
        }
        m_hasNewPalette = true;
    }

    void readOldPalette(Reader& in, bool sixBit)
    {
        auto& entries = m_oldPalette.entries;
        const int packets = in.word();
        int index = 0;
        for (int p = 0; p < packets; ++p) {
            index += in.byte();
            int n = in.byte();
            if (n == 0)
                n = 256;
            for (int i = 0; i < n; ++i, ++index) {
                const uint8_t* c = in.bytes(3);
                auto scale = [&](uint8_t v) { return sixBit ? uint8_t(v << 2 | v >> 4) : v; };
                if (index >= int(entries.size()))
                    entries.resize(size_t(index) + 1, {Rgba{0, 0, 0, 255}, {}});
                entries[size_t(index)].color = {scale(c[0]), scale(c[1]), scale(c[2]), 255};
            }
        }
    }

    // User data belongs to the chunk before it. Returns false if it should be
    // kept as an opaque chunk instead.
    bool attachUserData(Bytes data)
    {
        switch (m_target) {
        case Target::Sprite:
            m_doc->userData = std::move(data);
            m_target = Target::None;
            return true;
        case Target::Layer:
            m_layer->userData = std::move(data);
            m_target = Target::None;
            return true;
        case Target::Cel:
            m_cel->userData = std::move(data);
            m_target = Target::None;
            return true;
        case Target::Tileset: // the tileset's, then each tile's: all kept, in order
            m_doc->tilesets[size_t(m_tileset)].userData.push_back(std::move(data));
            return true;
        case Target::Slice:
            if (const auto color = userDataColor(data))
                m_doc->slices.back().color = *color;
            m_doc->slices.back().userData = std::move(data);
            m_target = Target::None;
            return true;
        case Target::Tags:
            if (m_nextTag < int(m_doc->tags.size())) {
                m_doc->tags[size_t(m_nextTag++)].userData = std::move(data);
                return true;
            }
            return false;
        default:
            return false; // opaque: bundled with the opaque chunk before it
        }
    }

    void keepOpaque(int f, uint16_t type, size_t start, uint32_t size)
    {
        // User data after an opaque chunk (slice, tileset, ...) stays with it.
        const bool append = type == kUserDataChunk && m_target == Target::Opaque;
        if (!append)
            m_doc->opaqueChunks.push_back({f, !m_seenLayersOrCels, 0, {}});
        OpaqueChunks& opaque = m_doc->opaqueChunks.back();
        const size_t save = m_in.pos();
        m_in.seek(start);
        const uint8_t* p = m_in.bytes(size);
        m_in.seek(save);
        opaque.bytes.insert(opaque.bytes.end(), p, p + size);
        ++opaque.count;
        m_target = Target::Opaque;
    }

    enum class Target { None, Sprite, Layer, Cel, Tags, Slice, Tileset, Opaque };
    int m_tileset = -1;

    Reader m_in;
    std::unique_ptr<Document> m_doc;
    int m_frameCount = 0;
    uint32_t m_flags = 0;
    std::vector<Layer*> m_layers; // file order
    std::vector<Layer*> m_groups; // open group per child level
    Palette m_oldPalette;
    bool m_hasNewPalette = false;
    bool m_seenLayersOrCels = false;

    Target m_target = Target::None;
    Layer* m_layer = nullptr;
    CelData* m_cel = nullptr;
    int m_nextTag = 0;
};

// ---------------------------------------------------------------- writing

class Writer {
public:
    std::vector<uint8_t> out;

    size_t pos() const { return out.size(); }
    void byte(int v) { out.push_back(uint8_t(v)); }
    void word(int v)
    {
        byte(v & 0xff);
        byte((v >> 8) & 0xff);
    }
    void dword(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            byte(int(v >> (8 * i)) & 0xff);
    }
    void zeros(size_t n) { out.insert(out.end(), n, 0); }
    void bytes(const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); }
    void bytes(const Bytes& b) { bytes(b.data(), b.size()); }
    void string(const std::string& s)
    {
        const size_t n = std::min<size_t>(s.size(), 0xffff);
        word(int(n));
        bytes(reinterpret_cast<const uint8_t*>(s.data()), n);
    }
    void patchDword(size_t at, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out[at + size_t(i)] = uint8_t(v >> (8 * i));
    }
};

std::vector<uint8_t> deflate(const uint8_t* src, size_t size)
{
    uLongf outSize = compressBound(uLong(size));
    std::vector<uint8_t> out(outSize);
    if (compress2(out.data(), &outSize, src, uLong(size), Z_DEFAULT_COMPRESSION) != Z_OK)
        throw FormatError("compression failed");
    out.resize(outSize);
    return out;
}

bool isTransparent(uint32_t v, PixelFormat format, int transparentIndex)
{
    switch (format) {
    case PixelFormat::Rgba8: return (v >> 24) == 0;
    case PixelFormat::Gray16: return (v >> 8) == 0;
    case PixelFormat::Indexed8: return int(v) == transparentIndex;
    case PixelFormat::Tilemap32: return v == 0;
    }
    return false;
}

// Bounds of the non-transparent pixels (empty if none).
Rect opaqueBounds(const Image& image, int transparentIndex)
{
    int l = image.width(), t = image.height(), r = -1, b = -1;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (!isTransparent(image.pixel(x, y), image.format(), transparentIndex)) {
                l = std::min(l, x);
                r = std::max(r, x);
                t = std::min(t, y);
                b = std::max(b, y);
            }
    return r < 0 ? Rect{} : Rect{l, t, r - l + 1, b - t + 1};
}

// Where user data's color starts (after its flags and text), if it has one.
std::optional<size_t> userDataColorAt(const Bytes& data)
{
    if (data.size() < 4)
        return std::nullopt;
    const uint32_t flags = uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16
        | uint32_t(data[3]) << 24;
    if (!(flags & 2))
        return std::nullopt;
    size_t pos = 4;
    if (flags & 1) {
        if (pos + 2 > data.size())
            return std::nullopt;
        pos += 2 + size_t(data[pos] | data[pos + 1] << 8);
    }
    if (pos + 4 > data.size())
        return std::nullopt;
    return pos;
}

std::optional<Rgba> userDataColor(const Bytes& data)
{
    const auto at = userDataColorAt(data);
    if (!at)
        return std::nullopt;
    return Rgba{data[*at], data[*at + 1], data[*at + 2], data[*at + 3]};
}

// User data with this color: replaced if it has one, added (after the text,
// before any properties) if it doesn't; empty data becomes just the color.
Bytes withAddedColor(Bytes data, Rgba color)
{
    if (const auto at = userDataColorAt(data)) {
        data[*at] = color.r;
        data[*at + 1] = color.g;
        data[*at + 2] = color.b;
        data[*at + 3] = color.a;
        return data;
    }
    if (data.size() < 4)
        data = Bytes(4, 0);
    size_t pos = 4;
    if (data[0] & 1) {
        if (pos + 2 > data.size())
            return data;
        pos += 2 + size_t(data[pos] | data[pos + 1] << 8);
        if (pos > data.size())
            return data;
    }
    data[0] |= 2;
    const uint8_t c[4] = {color.r, color.g, color.b, color.a};
    data.insert(data.begin() + std::ptrdiff_t(pos), c, c + 4);
    return data;
}

// User data with its color (if it has one) replaced, so a color edited in
// Smalto wins over the one stored in the preserved chunk.
Bytes withColor(Bytes data, Rgba color)
{
    if (data.size() < 4)
        return data;
    const uint32_t flags = uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16
        | uint32_t(data[3]) << 24;
    if (!(flags & 2))
        return data;
    size_t pos = 4;
    if (flags & 1) { // text first
        if (pos + 2 > data.size())
            return data;
        pos += 2 + size_t(data[pos] | data[pos + 1] << 8);
    }
    if (pos + 4 > data.size())
        return data;
    data[pos] = color.r;
    data[pos + 1] = color.g;
    data[pos + 2] = color.b;
    data[pos + 3] = color.a;
    return data;
}

class AsepriteWriter {
public:
    explicit AsepriteWriter(const Document& doc)
        : m_doc(doc)
    {
        doc.forEachLayer([&](const Layer& layer) { m_layers.push_back(&layer); });
        for (const Layer* layer : m_layers)
            m_anyUuid = m_anyUuid || layer->uuid.size() == 16;
    }

    std::vector<uint8_t> write()
    {
        writeHeader();
        for (int f = 0; f < m_doc.frameCount(); ++f)
            writeFrame(f);
        m_out.patchDword(0, uint32_t(m_out.pos()));
        return std::move(m_out.out);
    }

private:
    void writeHeader()
    {
        const Palette& pal = m_doc.palette;
        m_out.dword(0); // file size, patched
        m_out.word(kFileMagic);
        m_out.word(m_doc.frameCount());
        m_out.word(m_doc.width());
        m_out.word(m_doc.height());
        switch (m_doc.colorMode()) {
        case ColorMode::Rgba: m_out.word(32); break;
        case ColorMode::Grayscale: m_out.word(16); break;
        case ColorMode::Indexed: m_out.word(8); break;
        }
        m_out.dword(kLayerOpacityValid | kGroupOpacityValid | (m_anyUuid ? kLayerUuids : 0));
        m_out.word(std::clamp(m_doc.frame(0).durationMs, 0, 0xffff));
        m_out.zeros(8);
        m_out.byte(m_doc.transparentIndex);
        m_out.zeros(3);
        m_out.word(std::min(pal.size(), 0xffff));
        m_out.byte(m_doc.pixelRatio.x);
        m_out.byte(m_doc.pixelRatio.y);
        m_out.word(m_doc.grid.x);
        m_out.word(m_doc.grid.y);
        m_out.word(m_doc.grid.w);
        m_out.word(m_doc.grid.h);
        m_out.zeros(84);
    }

    void writeFrame(int f)
    {
        const size_t start = m_out.pos();
        m_out.dword(0); // frame size, patched
        m_out.word(kFrameMagic);
        const size_t oldCountAt = m_out.pos();
        m_out.word(0);
        m_out.word(std::clamp(m_doc.frame(f).durationMs, 1, 0xffff));
        m_out.zeros(2);
        const size_t newCountAt = m_out.pos();
        m_out.dword(0);
        m_chunks = 0;

        writeOpaque(f, true);
        if (f == 0) {
            if (!hasOpaque(kColorProfileChunk))
                writeSrgbProfile();
            writePalette();
            if (m_doc.symmetryCenter2) {
                const size_t c = beginChunk(kSmaltoChunk);
                m_out.word(1);
                m_out.dword(uint32_t(m_doc.symmetryCenter2->x));
                m_out.dword(uint32_t(m_doc.symmetryCenter2->y));
                endChunk(c);
            }
            if (!m_doc.userData.empty())
                writeUserData(m_doc.userData);
            for (size_t id = 0; id < m_doc.tilesets.size(); ++id)
                if (!m_doc.tilesets[id].tiles.empty())
                    writeTileset(uint32_t(id), m_doc.tilesets[id]);
            for (const Layer* layer : m_layers)
                writeLayer(*layer);
            if (!m_doc.tags.empty())
                writeTags();
            for (const Slice& slice : m_doc.slices)
                writeSlice(slice);
        }
        for (size_t i = 0; i < m_layers.size(); ++i)
            if (m_layers[i]->hasCels())
                writeCel(f, int(i));
        writeOpaque(f, false);

        m_out.patchDword(start, uint32_t(m_out.pos() - start));
        m_out.out[oldCountAt] = uint8_t(std::min(m_chunks, 0xffff));
        m_out.out[oldCountAt + 1] = uint8_t(std::min(m_chunks, 0xffff) >> 8);
        m_out.patchDword(newCountAt, uint32_t(m_chunks));
    }

    size_t beginChunk(uint16_t type)
    {
        const size_t start = m_out.pos();
        m_out.dword(0);
        m_out.word(type);
        ++m_chunks;
        return start;
    }
    void endChunk(size_t start) { m_out.patchDword(start, uint32_t(m_out.pos() - start)); }

    bool hasOpaque(uint16_t type) const
    {
        for (const auto& o : m_doc.opaqueChunks)
            for (size_t p = 0; p + kChunkHeaderSize <= o.bytes.size();) {
                const uint32_t size = uint32_t(o.bytes[p]) | uint32_t(o.bytes[p + 1]) << 8
                    | uint32_t(o.bytes[p + 2]) << 16 | uint32_t(o.bytes[p + 3]) << 24;
                if ((o.bytes[p + 4] | o.bytes[p + 5] << 8) == type)
                    return true;
                if (size < kChunkHeaderSize)
                    break;
                p += size;
            }
        return false;
    }

    void writeOpaque(int f, bool before)
    {
        const int last = m_doc.frameCount() - 1;
        for (const auto& o : m_doc.opaqueChunks) {
            if (std::min(o.frame, last) != f || o.beforeLayers != before)
                continue;
            m_out.bytes(o.bytes);
            m_chunks += o.count;
        }
    }

    void writeSrgbProfile()
    {
        const size_t c = beginChunk(kColorProfileChunk);
        m_out.word(1); // sRGB
        m_out.word(0);
        m_out.dword(0); // gamma
        m_out.zeros(8);
        endChunk(c);
    }

    void writePalette()
    {
        std::vector<PaletteEntry> entries = m_doc.palette.entries;
        if (entries.empty())
            entries.push_back({Rgba{0, 0, 0, 255}, {}});

        const size_t c = beginChunk(kPaletteChunk);
        m_out.dword(uint32_t(entries.size()));
        m_out.dword(0);
        m_out.dword(uint32_t(entries.size() - 1));
        m_out.zeros(8);
        for (const auto& e : entries) {
            m_out.word(e.name.empty() ? 0 : 1);
            m_out.byte(e.color.r);
            m_out.byte(e.color.g);
            m_out.byte(e.color.b);
            m_out.byte(e.color.a);
            if (!e.name.empty())
                m_out.string(e.name);
        }
        endChunk(c);

        // Old palette chunk for older readers, when it can represent the palette.
        const bool opaque = std::all_of(entries.begin(), entries.end(),
                                        [](const PaletteEntry& e) { return e.color.a == 255; });
        if (entries.size() <= 256 && opaque) {
            const size_t old = beginChunk(kOldPalette4);
            m_out.word(1);
            m_out.byte(0);
            m_out.byte(entries.size() == 256 ? 0 : int(entries.size()));
            for (const auto& e : entries) {
                m_out.byte(e.color.r);
                m_out.byte(e.color.g);
                m_out.byte(e.color.b);
            }
            endChunk(old);
        }
    }

    void writeTileset(uint32_t id, const Tileset& set)
    {
        const size_t c = beginChunk(kTilesetChunk);
        m_out.dword(id);
        m_out.dword(set.flags | 2u);
        m_out.dword(uint32_t(set.tiles.size()));
        m_out.word(set.tileWidth);
        m_out.word(set.tileHeight);
        m_out.word(uint16_t(int16_t(set.baseIndex)));
        m_out.zeros(14);
        m_out.string(set.name);
        if (set.flags & 1) {
            Bytes ext = set.external;
            ext.resize(8, 0);
            m_out.bytes(ext);
        }
        // All tiles stacked top to bottom, one image.
        Image all(set.tileWidth, set.tileHeight * int(set.tiles.size()), m_doc.pixelFormat());
        all.clear(0);
        for (size_t i = 0; i < set.tiles.size(); ++i)
            if (set.tiles[i])
                all.blit(*set.tiles[i], {0, int(i) * set.tileHeight});
        const Bytes packed = deflate(all.data(), size_t(all.stride()) * all.height());
        m_out.dword(uint32_t(packed.size()));
        m_out.bytes(packed);
        endChunk(c);
        for (const Bytes& data : set.userData)
            writeUserData(data);
    }

    void writeSlice(const Slice& slice)
    {
        const size_t c = beginChunk(kSliceChunk);
        m_out.dword(uint32_t(slice.keys.size()));
        m_out.dword((slice.nineSlice ? 1u : 0u) | (slice.hasPivot ? 2u : 0u));
        m_out.dword(0);
        m_out.string(slice.name);
        const auto value = [&](int v) { m_out.dword(uint32_t(int32_t(v))); };
        for (const SliceKey& k : slice.keys) {
            value(k.frame);
            value(k.bounds.x);
            value(k.bounds.y);
            value(k.bounds.w);
            value(k.bounds.h);
            if (slice.nineSlice) {
                value(k.center.x);
                value(k.center.y);
                value(k.center.w);
                value(k.center.h);
            }
            if (slice.hasPivot) {
                value(k.pivot.x);
                value(k.pivot.y);
            }
        }
        endChunk(c);
        // Its color lives in the user data after it.
        if (!slice.userData.empty() || slice.color != Slice{}.color)
            writeUserData(withAddedColor(slice.userData, slice.color));
    }

    void writeUserData(const Bytes& data)
    {
        const size_t c = beginChunk(kUserDataChunk);
        m_out.bytes(data);
        endChunk(c);
    }

    void writeLayer(const Layer& layer)
    {
        int level = 0;
        for (const Layer* p = layer.parent; p && p != &m_doc.root(); p = p->parent)
            ++level;

        const size_t c = beginChunk(kLayerChunk);
        m_out.word((layer.visible ? kLayerVisible : 0) | (layer.locked ? 0 : kLayerEditable)
                   | layer.extraFlags);
        m_out.word(layer.kind == LayerKind::Group ? 1 : layer.kind == LayerKind::Tilemap ? 2 : 0);
        m_out.word(level);
        m_out.word(0); // default width
        m_out.word(0); // default height
        m_out.word(int(layer.blend));
        m_out.byte(layer.opacity);
        m_out.zeros(3);
        m_out.string(layer.name);
        if (layer.kind == LayerKind::Tilemap)
            m_out.dword(layer.tilesetIndex);
        if (m_anyUuid) {
            if (layer.uuid.size() == 16)
                m_out.bytes(layer.uuid);
            else
                writeRandomUuid();
        }
        endChunk(c);
        // A live layer carries its recipe and the hash of the cels it rendered.
        const Bytes userData = layer.isLive() ? withStoredRecipe(layer.userData, *layer.particles, celsHash(m_doc, layer))
                               : layer.isSmear() ? withStoredSmear(layer.userData, *layer.smear)
                                                 : layer.userData;
        if (!userData.empty())
            writeUserData(userData);
    }

    void writeRandomUuid()
    {
        static std::mt19937 rng{std::random_device{}()};
        uint8_t u[16];
        for (auto& b : u)
            b = uint8_t(rng());
        u[6] = uint8_t((u[6] & 0x0f) | 0x40); // version 4
        u[8] = uint8_t((u[8] & 0x3f) | 0x80);
        m_out.bytes(u, 16);
    }

    void writeTags()
    {
        const size_t c = beginChunk(kTagsChunk);
        m_out.word(int(m_doc.tags.size()));
        m_out.zeros(8);
        for (const Tag& tag : m_doc.tags) {
            m_out.word(tag.from);
            m_out.word(tag.to);
            m_out.byte(int(tag.direction));
            m_out.word(tag.repeat);
            m_out.zeros(6);
            m_out.byte(tag.color.r);
            m_out.byte(tag.color.g);
            m_out.byte(tag.color.b);
            m_out.byte(0);
            m_out.string(tag.name);
        }
        endChunk(c);
        // One user data chunk per tag; newer readers take the tag color from it.
        for (const Tag& tag : m_doc.tags) {
            if (!tag.userData.empty()) {
                writeUserData(withColor(tag.userData, tag.color));
                continue;
            }
            const size_t u = beginChunk(kUserDataChunk);
            m_out.dword(2); // has color
            m_out.byte(tag.color.r);
            m_out.byte(tag.color.g);
            m_out.byte(tag.color.b);
            m_out.byte(tag.color.a);
            endChunk(u);
        }
    }

    void writeCel(int f, int layerIndex)
    {
        const Layer& layer = *m_layers[size_t(layerIndex)];
        const CelRef& cel = layer.cel(f);
        if (!cel || !cel->image || cel->image->isNull())
            return;

        auto& firstUse = m_linked[&layer];
        if (auto it = firstUse.find(cel.get()); it != firstUse.end()) {
            writeCelHeader(layerIndex, *cel, it->second.pos, 1);
            m_out.word(it->second.frame);
            endChunk(m_celChunk);
            return;
        }

        const Image& image = *cel->image;
        const bool trim = layer.kind != LayerKind::Tilemap && !layer.isBackground();
        Rect area = image.bounds();
        if (trim) {
            area = opaqueBounds(image, m_doc.transparentIndex);
            if (area.isEmpty()) {
                if (cel->userData.empty() && cel->extraData.empty())
                    return; // fully transparent, nothing to store
                area = {0, 0, 1, 1};
            }
        }
        const Point pos{cel->position.x + area.x, cel->position.y + area.y};
        firstUse.emplace(cel.get(), FirstUse{f, pos});

        const Image pixels = area == image.bounds() ? image : image.copy(area);
        const bool tilemap = image.format() == PixelFormat::Tilemap32;
        writeCelHeader(layerIndex, *cel, pos, tilemap ? 3 : 2);
        m_out.word(pixels.width());
        m_out.word(pixels.height());
        if (tilemap) {
            m_out.word(32);
            for (uint32_t m : kTileMasks)
                m_out.dword(m);
            m_out.zeros(10);
        }
        m_out.bytes(deflate(pixels.data(), size_t(pixels.stride()) * pixels.height()));
        endChunk(m_celChunk);

        if (!cel->extraData.empty()) {
            const size_t c = beginChunk(kCelExtraChunk);
            m_out.bytes(cel->extraData);
            endChunk(c);
        }
        if (!cel->userData.empty())
            writeUserData(cel->userData);
    }

    void writeCelHeader(int layerIndex, const CelData& cel, Point pos, int type)
    {
        m_celChunk = beginChunk(kCelChunk);
        m_out.word(layerIndex);
        m_out.word(pos.x);
        m_out.word(pos.y);
        m_out.byte(cel.opacity);
        m_out.word(type);
        m_out.word(cel.zIndex);
        m_out.zeros(5);
    }

    const Document& m_doc;
    Writer m_out;
    std::vector<const Layer*> m_layers; // file order
    bool m_anyUuid = false;
    int m_chunks = 0;
    size_t m_celChunk = 0;
    struct FirstUse {
        int frame;
        Point pos;
    };
    std::unordered_map<const Layer*, std::unordered_map<const CelData*, FirstUse>> m_linked;
};

} // namespace

std::unique_ptr<Document> readAseprite(std::span<const uint8_t> data, std::string* error)
{
    try {
        auto doc = AsepriteReader(data).read();
        if (doc)
            restoreLiveLayers(*doc);
        return doc;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return nullptr;
    }
}

std::unique_ptr<Document> loadAseprite(const std::filesystem::path& path, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error)
            *error = "cannot open file";
        return nullptr;
    }
    const std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return readAseprite(data, error);
}

std::vector<uint8_t> writeAseprite(const Document& doc)
{
    return AsepriteWriter(doc).write();
}

bool saveAseprite(const Document& doc, const std::filesystem::path& path, std::string* error)
{
    std::vector<uint8_t> data;
    try {
        data = writeAseprite(doc);
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return false;
    }
    return writeFileAtomically(path, data, error);
}

} // namespace formats
