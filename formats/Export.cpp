#include "formats/Export.h"

#include "core/Compositor.h"
#include "core/PaletteTools.h"
#include "formats/FileIO.h"

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

using namespace core;

namespace formats {

namespace {

struct Range {
    int from;
    int to; // inclusive
    int count() const { return to - from + 1; }
};

Range frameRange(const Document& doc, const ExportOptions& options)
{
    const int last = doc.frameCount() - 1;
    const int from = std::clamp(options.fromFrame, 0, last);
    const int to = options.toFrame < 0 ? last : std::clamp(options.toFrame, from, last);
    return {from, to};
}

int clampScale(int scale) { return std::clamp(scale, 1, 64); }

void put16(std::vector<uint8_t>& out, int v)
{
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}

void put32be(std::vector<uint8_t>& out, uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(uint8_t(v >> shift));
}

// ---------------------------------------------------------------- PNG

void pngChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
{
    put32be(out, uint32_t(data.size()));
    const size_t typeAt = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong crc = crc32(crc32(0, nullptr, 0), out.data() + typeAt, uInt(4 + data.size()));
    put32be(out, uint32_t(crc));
}

// ---------------------------------------------------------------- GIF

class LzwWriter {
public:
    LzwWriter(std::vector<uint8_t>& out, int minCodeSize)
        : m_out(out)
        , m_minCodeSize(minCodeSize)
    {
    }

    void encode(const std::vector<uint8_t>& indices)
    {
        m_out.push_back(uint8_t(m_minCodeSize));
        const int clear = 1 << m_minCodeSize;
        const int end = clear + 1;
        reset();
        emit(clear);
        int prefix = indices.empty() ? -1 : indices[0];
        for (size_t i = 1; i < indices.size(); ++i) {
            const uint32_t key = uint32_t(prefix) << 8 | indices[i];
            if (auto it = m_dict.find(key); it != m_dict.end()) {
                prefix = it->second;
                continue;
            }
            emit(prefix);
            if (m_next < 4096) {
                m_dict.emplace(key, m_next++);
                if (m_next > (1 << m_codeSize) && m_codeSize < 12)
                    ++m_codeSize;
            } else {
                emit(clear);
                reset();
            }
            prefix = indices[i];
        }
        if (prefix >= 0)
            emit(prefix);
        emit(end);
        if (m_bitCount > 0)
            m_block.push_back(uint8_t(m_bits));
        flushBlock();
        m_out.push_back(0); // block terminator
    }

private:
    void reset()
    {
        m_dict.clear();
        m_next = (1 << m_minCodeSize) + 2;
        m_codeSize = m_minCodeSize + 1;
    }

    void emit(int code)
    {
        m_bits |= uint32_t(code) << m_bitCount;
        m_bitCount += m_codeSize;
        while (m_bitCount >= 8) {
            m_block.push_back(uint8_t(m_bits));
            m_bits >>= 8;
            m_bitCount -= 8;
            if (m_block.size() == 255)
                flushBlock();
        }
    }

    void flushBlock()
    {
        if (m_block.empty())
            return;
        m_out.push_back(uint8_t(m_block.size()));
        m_out.insert(m_out.end(), m_block.begin(), m_block.end());
        m_block.clear();
    }

    std::vector<uint8_t>& m_out;
    int m_minCodeSize;
    int m_codeSize = 0;
    int m_next = 0;
    std::unordered_map<uint32_t, int> m_dict;
    std::vector<uint8_t> m_block;
    uint32_t m_bits = 0;
    int m_bitCount = 0;
};

bool isOpaque(uint32_t rgba) { return (rgba >> 24) >= 128; }
uint32_t opaqueKey(uint32_t rgba) { return rgba | 0xff000000u; }

// ---------------------------------------------------------------- JSON

// The text of `.aseprite` user data, if it has some (flag 1).
std::string userDataText(const Bytes& data)
{
    if (data.size() < 6 || !(data[0] & 1))
        return {};
    const size_t n = size_t(data[4] | data[5] << 8);
    if (6 + n > data.size())
        return {};
    return std::string(reinterpret_cast<const char*>(data.data() + 6), n);
}

std::string jsonString(const std::string& s)
{
    std::string out = "\"";
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

std::string rect(int x, int y, int w, int h)
{
    return "{ \"x\": " + std::to_string(x) + ", \"y\": " + std::to_string(y) + ", \"w\": " + std::to_string(w)
        + ", \"h\": " + std::to_string(h) + " }";
}

const char* directionName(AniDirection d)
{
    switch (d) {
    case AniDirection::Reverse: return "reverse";
    case AniDirection::PingPong: return "pingpong";
    case AniDirection::PingPongReverse: return "pingpong_reverse";
    default: return "forward";
    }
}

std::filesystem::path withSuffix(const std::filesystem::path& path, const char* ext)
{
    std::filesystem::path p = path;
    p.replace_extension(ext);
    return p;
}

} // namespace

Image renderScaled(const Document& doc, int frame, int scale)
{
    scale = clampScale(scale);
    Image frameImage(doc.width(), doc.height());
    renderFrame(doc, frame, frameImage, doc.bounds());
    if (scale == 1)
        return frameImage;
    Image out(doc.width() * scale, doc.height() * scale);
    for (int y = 0; y < out.height(); ++y) {
        const auto* src = reinterpret_cast<const uint32_t*>(frameImage.row(y / scale));
        auto* dst = reinterpret_cast<uint32_t*>(out.row(y));
        for (int x = 0; x < out.width(); ++x)
            dst[x] = src[x / scale];
    }
    return out;
}

std::vector<uint8_t> encodePng(const Image& image)
{
    // Rows prefixed with filter type 0, deflated as one zlib stream.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(image.height()) * (size_t(image.stride()) + 1));
    for (int y = 0; y < image.height(); ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), image.row(y), image.row(y) + image.stride());
    }
    uLongf size = compressBound(uLong(raw.size()));
    std::vector<uint8_t> idat(size);
    compress2(idat.data(), &size, raw.data(), uLong(raw.size()), 9);
    idat.resize(size);

    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    put32be(ihdr, uint32_t(image.width()));
    put32be(ihdr, uint32_t(image.height()));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, deflate, no filter set, no interlace
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", idat);
    pngChunk(out, "IEND", {});
    return out;
}

std::vector<uint8_t> encodePngIndexed(const Image& image)
{
    std::unordered_map<uint32_t, uint8_t> lookup;
    std::vector<Rgba> palette;
    std::vector<uint8_t> indices(size_t(image.width()) * size_t(image.height()));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            uint32_t v = image.pixel(x, y);
            if ((v >> 24) == 0)
                v = 0; // one transparent entry, whatever color it hid
            auto it = lookup.find(v);
            if (it == lookup.end()) {
                if (palette.size() == 256)
                    return encodePng(image);
                it = lookup.emplace(v, uint8_t(palette.size())).first;
                palette.push_back(unpack(v));
            }
            indices[size_t(y) * size_t(image.width()) + size_t(x)] = it->second;
        }
    const int bits = palette.size() <= 2 ? 1 : palette.size() <= 4 ? 2 : palette.size() <= 16 ? 4 : 8;

    // Each row: filter type 0, then the indices packed from the high bits.
    const size_t rowBytes = (size_t(image.width()) * size_t(bits) + 7) / 8;
    std::vector<uint8_t> raw(size_t(image.height()) * (rowBytes + 1), 0);
    for (int y = 0; y < image.height(); ++y) {
        uint8_t* row = &raw[size_t(y) * (rowBytes + 1) + 1];
        for (int x = 0; x < image.width(); ++x) {
            const uint8_t index = indices[size_t(y) * size_t(image.width()) + size_t(x)];
            const size_t bit = size_t(x) * size_t(bits);
            row[bit / 8] |= uint8_t(index << (8 - bits - int(bit % 8)));
        }
    }
    uLongf size = compressBound(uLong(raw.size()));
    std::vector<uint8_t> idat(size);
    compress2(idat.data(), &size, raw.data(), uLong(raw.size()), 9);
    idat.resize(size);

    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    put32be(ihdr, uint32_t(image.width()));
    put32be(ihdr, uint32_t(image.height()));
    ihdr.insert(ihdr.end(), {uint8_t(bits), 3, 0, 0, 0}); // palette, deflate, no filter set, no interlace
    pngChunk(out, "IHDR", ihdr);
    std::vector<uint8_t> plte, trns;
    for (const Rgba c : palette) {
        plte.insert(plte.end(), {c.r, c.g, c.b});
        trns.push_back(c.a);
    }
    pngChunk(out, "PLTE", plte);
    while (!trns.empty() && trns.back() == 255)
        trns.pop_back(); // entries past tRNS are opaque
    if (!trns.empty())
        pngChunk(out, "tRNS", trns);
    pngChunk(out, "IDAT", idat);
    pngChunk(out, "IEND", {});
    return out;
}

std::vector<uint8_t> encodeGif(const Document& doc, const ExportOptions& options)
{
    const Range range = frameRange(doc, options);
    const int scale = clampScale(options.scale);

    using Counts = std::unordered_map<uint32_t, uint32_t>;
    // Index 0 is transparent; up to 255 colors follow. Colors past that are
    // merged by median cut and each takes the nearest.
    struct GifPalette {
        std::vector<Rgba> colors;
        std::unordered_map<uint32_t, uint8_t> lookup; // opaque color -> index (1-based)
        int bits = 1;
    };
    const auto makePalette = [](const Counts& counts) {
        GifPalette p;
        std::vector<std::pair<uint32_t, uint32_t>> colors(counts.begin(), counts.end());
        std::sort(colors.begin(), colors.end());
        if (colors.size() <= 255) {
            for (const auto& [c, n] : colors) {
                p.lookup[c] = uint8_t(p.colors.size() + 1);
                p.colors.push_back(unpack(c));
            }
        } else {
            std::vector<ColorCount> weighted;
            for (const auto& [c, n] : colors)
                weighted.push_back({unpack(c), n});
            p.colors = medianCut(std::move(weighted), 255);
            for (const auto& [c, n] : colors) {
                const Rgba rgb = unpack(c);
                int best = 0, bestDistance = 1 << 30;
                for (size_t i = 0; i < p.colors.size(); ++i) {
                    const int dr = rgb.r - p.colors[i].r, dg = rgb.g - p.colors[i].g, db = rgb.b - p.colors[i].b;
                    const int d = dr * dr + dg * dg + db * db;
                    if (d < bestDistance) {
                        bestDistance = d;
                        best = int(i);
                    }
                }
                p.lookup[c] = uint8_t(best + 1);
            }
        }
        while ((1 << p.bits) < int(p.colors.size()) + 1)
            ++p.bits;
        return p;
    };
    const auto countColors = [](const Image& img, Counts& counts) {
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (const uint32_t v = img.pixel(x, y); isOpaque(v))
                    ++counts[opaqueKey(v)];
    };
    const auto putTable = [](std::vector<uint8_t>& out, const GifPalette& p) {
        for (int i = 0; i < (1 << p.bits); ++i) {
            const Rgba c = i >= 1 && i <= int(p.colors.size()) ? p.colors[size_t(i - 1)] : Rgba{};
            out.insert(out.end(), {c.r, c.g, c.b});
        }
    };

    std::vector<Image> frames;
    Counts all;
    std::vector<Counts> perFrame;
    for (int f = range.from; f <= range.to; ++f) {
        frames.push_back(renderScaled(doc, f, 1));
        perFrame.emplace_back();
        countColors(frames.back(), perFrame.back());
        for (const auto& [c, n] : perFrame.back())
            all[c] += n;
    }
    // One table for everything while it fits, else one per frame.
    const bool local = all.size() > 255 && frames.size() > 1;
    const GifPalette global = local ? GifPalette{} : makePalette(all);
    std::vector<GifPalette> locals;
    if (local)
        for (const Counts& counts : perFrame)
            locals.push_back(makePalette(counts));

    const int width = doc.width() * scale;
    const int height = doc.height() * scale;

    std::vector<uint8_t> out{'G', 'I', 'F', '8', '9', 'a'};
    put16(out, width);
    put16(out, height);
    // A global table, unless every frame has its own; then a small placeholder.
    const int globalBits = local ? 1 : global.bits;
    out.push_back(uint8_t(0x80 | 0x70 | (globalBits - 1))); // global table, 8-bit color resolution
    out.push_back(0);                                       // background index
    out.push_back(0);                                       // square pixels
    if (local)
        out.insert(out.end(), {0, 0, 0, 0, 0, 0});
    else
        putTable(out, global);

    // Loop forever.
    out.insert(out.end(), {0x21, 0xff, 0x0b});
    out.insert(out.end(), {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'});
    out.insert(out.end(), {0x03, 0x01, 0x00, 0x00, 0x00});

    // Delays are in centiseconds; round the running time so the total stays
    // exact. Browsers slow anything under 2 cs down to 10 cs, so 2 is the floor.
    int elapsedMs = 0;
    std::vector<uint8_t> indices(size_t(width) * size_t(height));
    for (size_t i = 0; i < frames.size(); ++i) {
        const int start = (elapsedMs + 5) / 10;
        elapsedMs += doc.frame(range.from + int(i)).durationMs;
        const int delay = std::max(2, (elapsedMs + 5) / 10 - start);

        out.insert(out.end(), {0x21, 0xf9, 0x04});
        out.push_back(uint8_t(2 << 2 | 1)); // restore to background, has transparency
        put16(out, delay);
        out.push_back(0); // transparent index
        out.push_back(0);

        out.push_back(0x2c);
        put16(out, 0);
        put16(out, 0);
        put16(out, width);
        put16(out, height);
        const GifPalette& palette = local ? locals[i] : global;
        if (local) {
            out.push_back(uint8_t(0x80 | (palette.bits - 1))); // local table, not interlaced
            putTable(out, palette);
        } else {
            out.push_back(0); // no local table, not interlaced
        }

        const Image& img = frames[i];
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const uint32_t v = img.pixel(x / scale, y / scale);
                indices[size_t(y) * size_t(width) + size_t(x)] = isOpaque(v) ? palette.lookup.at(opaqueKey(v)) : 0;
            }
        LzwWriter(out, std::max(2, palette.bits)).encode(indices);
    }
    out.push_back(0x3b);
    return out;
}

std::vector<std::vector<int>> sheetRows(const Document& doc, const SheetOptions& options)
{
    std::vector<std::vector<int>> rows;
    if (options.layout == SheetLayout::TagRows && !doc.tags.empty()) {
        const int last = doc.frameCount() - 1;
        for (const Tag& tag : doc.tags) {
            std::vector<int>& row = rows.emplace_back();
            for (int f = std::clamp(tag.from, 0, last); f <= std::clamp(tag.to, 0, last); ++f)
                row.push_back(f);
        }
        return rows;
    }

    const Range range = frameRange(doc, options);
    const int n = range.count();
    int columns = n;
    if (options.layout == SheetLayout::Vertical)
        columns = 1;
    else if (options.layout == SheetLayout::Grid)
        columns = options.columns > 0 ? std::min(options.columns, n) : int(std::ceil(std::sqrt(double(n))));
    for (int i = 0; i < n; ++i) {
        if (i % columns == 0)
            rows.emplace_back();
        rows.back().push_back(range.from + i);
    }
    return rows;
}

SpriteSheet buildSpriteSheet(const Document& doc, const SheetOptions& options, const std::string& imageFileName)
{
    const auto rows = sheetRows(doc, options);
    const bool byTag = options.layout == SheetLayout::TagRows && !doc.tags.empty();
    const int scale = clampScale(options.scale);
    const int pad = std::max(0, options.padding);
    const int cw = doc.width() * scale;
    const int ch = doc.height() * scale;

    size_t columns = 0;
    for (const auto& row : rows)
        columns = std::max(columns, row.size());
    SpriteSheet sheet;
    sheet.image = Image(int(columns) * cw + (int(columns) - 1) * pad, int(rows.size()) * ch + (int(rows.size()) - 1) * pad);

    // Cells in reading order; a frame in several tags gets a cell per tag.
    std::string frames;
    std::vector<int> rowStart; // first cell index of each row
    int cell = 0;
    for (size_t r = 0; r < rows.size(); ++r) {
        rowStart.push_back(cell);
        for (size_t c = 0; c < rows[r].size(); ++c, ++cell) {
            const int f = rows[r][c];
            const int x = int(c) * (cw + pad);
            const int y = int(r) * (ch + pad);
            sheet.image.blit(renderScaled(doc, f, scale), {x, y});
            frames += std::string(cell ? ",\n" : "") + "    { \"filename\": " + jsonString(std::to_string(f))
                + ", \"frame\": " + rect(x, y, cw, ch) + ", \"rotated\": false, \"trimmed\": false"
                + ", \"spriteSourceSize\": " + rect(0, 0, cw, ch) + ", \"sourceSize\": { \"w\": "
                + std::to_string(cw) + ", \"h\": " + std::to_string(ch) + " }, \"duration\": "
                + std::to_string(doc.frame(f).durationMs) + " }";
        }
    }

    // Tag frame numbers refer to sheet cells: its own row when laid out by
    // tag, otherwise the part of the tag inside the exported range.
    const Range range = frameRange(doc, options);
    std::string tags;
    for (size_t i = 0; i < doc.tags.size(); ++i) {
        const Tag& tag = doc.tags[i];
        int from, to;
        if (byTag) {
            if (rows[i].empty())
                continue;
            from = rowStart[i];
            to = rowStart[i] + int(rows[i].size()) - 1;
        } else {
            from = std::max(tag.from, range.from) - range.from;
            to = std::min(tag.to, range.to) - range.from;
            if (from > to)
                continue;
        }
        tags += std::string(tags.empty() ? "" : ",\n") + "      { \"name\": " + jsonString(tag.name)
            + ", \"from\": " + std::to_string(from) + ", \"to\": " + std::to_string(to)
            + ", \"direction\": \"" + directionName(tag.direction) + "\""
            + (tag.repeat > 0 ? ", \"repeat\": \"" + std::to_string(tag.repeat) + "\"" : std::string()) + " }";
    }

    // Slices, as Aseprite writes them: keys in the exported range, numbered
    // from its start (the key in effect at the start becomes frame 0; by tag,
    // the sprite's own frame numbers), scaled like the frames.
    const auto rectJson = [&](Rect r) {
        return "{ \"x\": " + std::to_string(r.x * scale) + ", \"y\": " + std::to_string(r.y * scale) + ", \"w\": "
            + std::to_string(r.w * scale) + ", \"h\": " + std::to_string(r.h * scale) + " }";
    };
    std::string slices;
    for (const Slice& slice : doc.slices) {
        std::string keys;
        for (size_t k = 0; k < slice.keys.size(); ++k) {
            const SliceKey& key = slice.keys[k];
            const int next = k + 1 < slice.keys.size() ? slice.keys[k + 1].frame : INT_MAX;
            int frame = key.frame;
            if (!byTag) {
                if (key.frame > range.to || next <= range.from)
                    continue; // not in effect anywhere in the range
                frame = std::max(key.frame, range.from) - range.from;
            }
            keys += std::string(keys.empty() ? "" : ", ") + "{ \"frame\": " + std::to_string(frame)
                + ", \"bounds\": " + rectJson(key.bounds)
                + (slice.nineSlice ? ", \"center\": " + rectJson(key.center) : std::string())
                + (slice.hasPivot ? ", \"pivot\": { \"x\": " + std::to_string(key.pivot.x * scale) + ", \"y\": "
                                        + std::to_string(key.pivot.y * scale) + " }"
                                  : std::string())
                + " }";
        }
        if (keys.empty())
            continue;
        char color[10];
        std::snprintf(color, sizeof color, "#%02x%02x%02x%02x", slice.color.r, slice.color.g, slice.color.b,
                      slice.color.a);
        const std::string text = userDataText(slice.userData);
        slices += std::string(slices.empty() ? "" : ",\n") + "      { \"name\": " + jsonString(slice.name)
            + ", \"color\": \"" + color + "\""
            + (text.empty() ? std::string() : ", \"data\": " + jsonString(text)) + ", \"keys\": [ " + keys + " ] }";
    }

    sheet.json = "{ \"frames\": [\n" + frames + "\n  ],\n  \"meta\": {\n"
        + "    \"app\": \"Smalto\",\n    \"version\": \"" SMALTO_VERSION "\",\n"
        + "    \"image\": " + jsonString(imageFileName) + ",\n    \"format\": \"RGBA8888\",\n"
        + "    \"size\": { \"w\": " + std::to_string(sheet.image.width()) + ", \"h\": "
        + std::to_string(sheet.image.height()) + " },\n    \"scale\": \"" + std::to_string(scale) + "\",\n"
        + "    \"frameTags\": [" + (tags.empty() ? "" : "\n" + tags + "\n    ") + "],\n"
        + "    \"slices\": [" + (slices.empty() ? "" : "\n" + slices + "\n    ") + "]\n  }\n}\n";
    return sheet;
}

std::vector<std::filesystem::path> pngExportPaths(const Document& doc, const ExportOptions& options,
                                                  const std::filesystem::path& path)
{
    const Range range = frameRange(doc, options);
    const std::filesystem::path png = withSuffix(path, ".png");
    if (range.count() == 1)
        return {png};
    const size_t digits = std::to_string(range.to + 1).size();
    std::vector<std::filesystem::path> paths;
    for (int f = range.from; f <= range.to; ++f) {
        std::string number = std::to_string(f + 1);
        number.insert(0, digits - number.size(), '0');
        std::filesystem::path p = png;
        p.replace_filename(png.stem().string() + "-" + number + ".png");
        paths.push_back(p);
    }
    return paths;
}

bool exportPng(const Document& doc, const ExportOptions& options, const std::filesystem::path& path,
               std::string* error)
{
    const Range range = frameRange(doc, options);
    const auto paths = pngExportPaths(doc, options, path);
    const auto encodeFrame = [&](const Image& image) {
        return options.indexedPng ? encodePngIndexed(image) : encodePng(image);
    };
    for (int i = 0; i < range.count(); ++i)
        if (!writeFileAtomically(paths[size_t(i)], encodeFrame(renderScaled(doc, range.from + i, options.scale)), error))
            return false;
    return true;
}

bool exportGif(const Document& doc, const ExportOptions& options, const std::filesystem::path& path,
               std::string* error)
{
    return writeFileAtomically(withSuffix(path, ".gif"), encodeGif(doc, options), error);
}

bool exportSpriteSheet(const Document& doc, const SheetOptions& options, const std::filesystem::path& path,
                       std::string* error)
{
    const std::filesystem::path png = withSuffix(path, ".png");
    const SpriteSheet sheet = buildSpriteSheet(doc, options, png.filename().string());
    return writeFileAtomically(png, options.indexedPng ? encodePngIndexed(sheet.image) : encodePng(sheet.image), error)
        && writeFileAtomically(withSuffix(path, ".json"),
                               std::vector<uint8_t>(sheet.json.begin(), sheet.json.end()), error);
}

} // namespace formats
