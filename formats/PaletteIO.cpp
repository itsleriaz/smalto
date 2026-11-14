#include "formats/PaletteIO.h"

#include "formats/Aseprite.h"
#include "formats/ImageImport.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <unordered_set>

namespace formats {

using core::Palette;
using core::Rgba;

namespace {

void fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

std::string_view trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// Calls fn(line) for each line, without the line break.
template <typename Fn>
void forEachLine(std::string_view text, Fn fn)
{
    while (!text.empty()) {
        const auto end = text.find('\n');
        fn(text.substr(0, end));
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// Next whitespace-separated integer in `s`, consumed.
std::optional<int> nextInt(std::string_view& s)
{
    s = trim(s);
    int value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || (ptr != s.data() + s.size() && *ptr != ' ' && *ptr != '\t'))
        return std::nullopt;
    s.remove_prefix(size_t(ptr - s.data()));
    return value;
}

bool tooMany(const Palette& palette, std::string* error)
{
    if (palette.size() <= kMaxPaletteSize)
        return false;
    fail(error, "The palette has " + std::to_string(palette.size()) + " colors; at most "
                    + std::to_string(kMaxPaletteSize) + " are supported.");
    return true;
}

} // namespace

std::optional<PaletteFile> readGpl(std::string_view text, std::string* error)
{
    if (startsWith(text, "\xEF\xBB\xBF")) // UTF-8 BOM
        text.remove_prefix(3);
    PaletteFile file;
    bool header = false, rgba = false;
    std::string problem;
    int lineNumber = 0;
    forEachLine(text, [&](std::string_view raw) {
        ++lineNumber;
        const std::string_view line = trim(raw);
        if (!problem.empty())
            return;
        if (!header) {
            if (line.empty())
                return;
            header = line == "GIMP Palette";
            if (!header)
                problem = "Not a GIMP palette (it should start with \"GIMP Palette\").";
            return;
        }
        if (line.empty() || line[0] == '#' || startsWith(line, "Columns:"))
            return;
        if (startsWith(line, "Name:")) {
            file.name = std::string(trim(line.substr(5)));
            return;
        }
        if (startsWith(line, "Channels:")) {
            rgba = trim(line.substr(9)) == "RGBA";
            return;
        }
        std::string_view rest = line;
        int v[4] = {0, 0, 0, 255};
        for (int i = 0; i < (rgba ? 4 : 3); ++i) {
            const auto n = nextInt(rest);
            if (!n || *n < 0 || *n > 255) {
                problem = "Line " + std::to_string(lineNumber) + " isn't a color.";
                return;
            }
            v[i] = *n;
        }
        file.palette.entries.push_back(
            {Rgba{uint8_t(v[0]), uint8_t(v[1]), uint8_t(v[2]), uint8_t(v[3])}, std::string(trim(rest))});
    });
    if (problem.empty() && file.palette.entries.empty())
        problem = header ? "The palette has no colors." : "The file is empty.";
    if (!problem.empty()) {
        fail(error, problem);
        return std::nullopt;
    }
    if (tooMany(file.palette, error))
        return std::nullopt;
    return file;
}

std::string writeGpl(const Palette& palette, std::string_view name)
{
    bool alpha = false;
    for (const auto& e : palette.entries)
        alpha = alpha || e.color.a != 255;
    std::string out = "GIMP Palette\n";
    if (alpha)
        out += "Channels: RGBA\n";
    out += "Name: " + std::string(name) + "\n";
    out += "Columns: 8\n#\n";
    char buf[32];
    for (const auto& e : palette.entries) {
        const Rgba c = e.color;
        if (alpha)
            std::snprintf(buf, sizeof buf, "%3d %3d %3d %3d", c.r, c.g, c.b, c.a);
        else
            std::snprintf(buf, sizeof buf, "%3d %3d %3d", c.r, c.g, c.b);
        out += buf;
        if (!e.name.empty())
            out += "\t" + e.name;
        out += "\n";
    }
    return out;
}

std::optional<PaletteFile> readHex(std::string_view text, std::string* error)
{
    PaletteFile file;
    std::string problem;
    int lineNumber = 0;
    forEachLine(text, [&](std::string_view raw) {
        ++lineNumber;
        std::string_view line = trim(raw);
        if (!problem.empty() || line.empty() || line[0] == ';')
            return;
        if (line[0] == '#')
            line.remove_prefix(1);
        uint32_t v = 0;
        const auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), v, 16);
        if (ec != std::errc() || ptr != line.data() + line.size() || (line.size() != 6 && line.size() != 8)) {
            problem = "Line " + std::to_string(lineNumber) + " isn't a hex color.";
            return;
        }
        if (line.size() == 6)
            v = (v << 8) | 0xFF;
        file.palette.entries.push_back({Rgba{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}, {}});
    });
    if (problem.empty() && file.palette.entries.empty())
        problem = "The palette has no colors.";
    if (!problem.empty()) {
        fail(error, problem);
        return std::nullopt;
    }
    if (tooMany(file.palette, error))
        return std::nullopt;
    return file;
}

std::string writeHex(const Palette& palette)
{
    std::string out;
    char buf[16];
    for (const auto& e : palette.entries) {
        const Rgba c = e.color;
        if (c.a == 255)
            std::snprintf(buf, sizeof buf, "%02x%02x%02x\n", c.r, c.g, c.b);
        else
            std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x\n", c.r, c.g, c.b, c.a);
        out += buf;
    }
    return out;
}

std::optional<PaletteFile> readPal(std::span<const uint8_t> data, std::string* error)
{
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    PaletteFile file;
    if (startsWith(text, "RIFF")) {
        // RIFF header (12), "data" chunk (8): version, count, then 4 bytes per entry.
        const auto u16 = [&](size_t at) { return size_t(data[at]) | size_t(data[at + 1]) << 8; };
        const auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
        size_t at = 12;
        while (at + 8 <= data.size() && text.substr(at, 4) != "data")
            at += 8 + u32(at + 4) + (u32(at + 4) & 1);
        if (data.size() < 12 || text.substr(8, 4) != "PAL " || at + 8 + 4 > data.size()) {
            fail(error, "Not a palette file.");
            return std::nullopt;
        }
        const size_t count = u16(at + 10);
        if (at + 12 + count * 4 > data.size()) {
            fail(error, "The palette file is cut short.");
            return std::nullopt;
        }
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* e = &data[at + 12 + i * 4];
            file.palette.entries.push_back({Rgba{e[0], e[1], e[2], 255}, {}});
        }
    } else {
        int lineNumber = 0, expected = -1;
        std::string problem;
        forEachLine(text, [&](std::string_view raw) {
            std::string_view line = trim(raw);
            if (!problem.empty() || line.empty())
                return;
            if (++lineNumber == 1) {
                if (line != "JASC-PAL")
                    problem = "Not a palette file.";
            } else if (lineNumber == 2) {
                if (line != "0100")
                    problem = "Unsupported palette version.";
            } else if (lineNumber == 3) {
                expected = nextInt(line).value_or(-1);
                if (expected < 1)
                    problem = "The palette has no colors.";
            } else {
                const auto r = nextInt(line), g = nextInt(line), b = nextInt(line);
                if (!r || !g || !b || *r < 0 || *r > 255 || *g < 0 || *g > 255 || *b < 0 || *b > 255)
                    problem = "Line " + std::to_string(lineNumber + 1) + " isn't a color.";
                else if (int(file.palette.entries.size()) < expected)
                    file.palette.entries.push_back({Rgba{uint8_t(*r), uint8_t(*g), uint8_t(*b), 255}, {}});
            }
        });
        if (problem.empty() && (expected < 1 || int(file.palette.entries.size()) < expected))
            problem = "The palette file is cut short.";
        if (!problem.empty()) {
            fail(error, problem);
            return std::nullopt;
        }
    }
    if (file.palette.entries.empty()) {
        fail(error, "The palette has no colors.");
        return std::nullopt;
    }
    if (tooMany(file.palette, error))
        return std::nullopt;
    return file;
}

std::string writePal(const Palette& palette)
{
    std::string out = "JASC-PAL\r\n0100\r\n" + std::to_string(palette.size()) + "\r\n";
    for (const auto& e : palette.entries)
        out += std::to_string(e.color.r) + " " + std::to_string(e.color.g) + " " + std::to_string(e.color.b) + "\r\n";
    return out;
}

std::optional<PaletteFile> readAct(std::span<const uint8_t> data, std::string* error)
{
    if (data.size() != 768 && data.size() != 772) {
        fail(error, "Not a color table file.");
        return std::nullopt;
    }
    size_t count = 256;
    int transparent = -1;
    if (data.size() == 772) {
        count = size_t(data[768]) << 8 | data[769];
        const int t = data[770] << 8 | data[771];
        transparent = t == 0xFFFF ? -1 : t;
    }
    if (count < 1 || count > 256) {
        fail(error, "The palette has no colors.");
        return std::nullopt;
    }
    PaletteFile file;
    for (size_t i = 0; i < count; ++i)
        file.palette.entries.push_back({Rgba{data[i * 3], data[i * 3 + 1], data[i * 3 + 2], int(i) == transparent ? uint8_t(0) : uint8_t(255)}, {}});
    return file;
}

std::vector<uint8_t> writeAct(const Palette& palette)
{
    std::vector<uint8_t> out(768, 0);
    const size_t count = std::min<size_t>(size_t(palette.size()), 256);
    int transparent = -1;
    for (size_t i = 0; i < count; ++i) {
        const Rgba c = palette.entries[i].color;
        out[i * 3] = c.r;
        out[i * 3 + 1] = c.g;
        out[i * 3 + 2] = c.b;
        if (c.a == 0 && transparent < 0)
            transparent = int(i);
    }
    if (count < 256 || transparent >= 0) {
        const int t = transparent < 0 ? 0xFFFF : transparent;
        out.insert(out.end(), {uint8_t(count >> 8), uint8_t(count), uint8_t(t >> 8), uint8_t(t)});
    }
    return out;
}

std::optional<PaletteFile> readPalette(std::span<const uint8_t> data, std::string_view extension, std::string* error)
{
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    if (extension == ".gpl")
        return readGpl(text, error);
    if (extension == ".hex")
        return readHex(text, error);
    if (extension == ".pal")
        return readPal(data, error);
    if (extension == ".act")
        return readAct(data, error);
    if (extension == ".aseprite" || extension == ".ase") {
        auto doc = readAseprite(data, error);
        if (!doc)
            return std::nullopt;
        return PaletteFile{std::move(doc->palette), {}};
    }
    if (isImageExtension(extension)) {
        auto image = decodeImage(data, error);
        if (!image)
            return std::nullopt;
        PaletteFile file;
        std::unordered_set<uint32_t> seen;
        for (int y = 0; y < image->height(); ++y)
            for (int x = 0; x < image->width(); ++x) {
                const uint32_t v = image->pixel(x, y);
                if (core::unpack(v).a == 0 || !seen.insert(v).second)
                    continue;
                if (file.palette.size() == kMaxPaletteSize) {
                    fail(error, "The image has more than " + std::to_string(kMaxPaletteSize) + " colors.");
                    return std::nullopt;
                }
                file.palette.entries.push_back({core::unpack(v), {}});
            }
        if (file.palette.entries.empty()) {
            fail(error, "The image has no visible colors.");
            return std::nullopt;
        }
        return file;
    }
    fail(error, "Unsupported palette file type.");
    return std::nullopt;
}

std::optional<std::string> writePalette(const Palette& palette, std::string_view name, std::string_view extension)
{
    if (extension == ".gpl")
        return writeGpl(palette, name);
    if (extension == ".hex")
        return writeHex(palette);
    if (extension == ".pal")
        return writePal(palette);
    return std::nullopt;
}

} // namespace formats
