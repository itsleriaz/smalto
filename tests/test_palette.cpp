#include <doctest/doctest.h>

#include "formats/PaletteIO.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace core;
using namespace formats;

namespace {

const std::filesystem::path kCorpus = SMALTO_CORPUS_DIR;

std::vector<uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::optional<PaletteFile> parse(std::string_view text, std::string_view ext, std::string* error = nullptr)
{
    return readPalette({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, ext, error);
}

} // namespace

TEST_CASE("gpl: read names, comments, columns; round trip")
{
    const auto file = readGpl("GIMP Palette\r\nName: Test\r\nColumns: 4\r\n# comment\r\n"
                              "255   0   0\tRed\r\n  0 128 255 Sky blue\r\n1 2 3\r\n");
    REQUIRE(file);
    CHECK(file->name == "Test");
    REQUIRE(file->palette.size() == 3);
    CHECK(file->palette.color(0) == Rgba{255, 0, 0, 255});
    CHECK(file->palette.entries[1].name == "Sky blue");
    CHECK(file->palette.color(2) == Rgba{1, 2, 3, 255});

    const auto again = readGpl(writeGpl(file->palette, "Test"));
    REQUIRE(again);
    CHECK(again->name == "Test");
    CHECK(again->palette.size() == 3);
    CHECK(again->palette.entries[0].name == "Red");
    CHECK(again->palette.color(1) == Rgba{0, 128, 255, 255});
}

TEST_CASE("gpl: Aseprite's RGBA channels")
{
    Palette p;
    p.entries = {{Rgba{10, 20, 30, 128}, {}}, {Rgba{1, 1, 1, 255}, {}}};
    const std::string text = writeGpl(p, "a");
    CHECK(text.find("Channels: RGBA") != std::string::npos);
    const auto file = readGpl(text);
    REQUIRE(file);
    CHECK(file->palette.color(0) == Rgba{10, 20, 30, 128});
    CHECK(writeGpl(file->palette, "a") == text);

    Palette opaque;
    opaque.entries = {{Rgba{1, 2, 3, 255}, {}}};
    CHECK(writeGpl(opaque, "o").find("Channels") == std::string::npos);
}

TEST_CASE("gpl: errors")
{
    std::string error;
    CHECK_FALSE(readGpl("JASC-PAL\n0100\n", &error));
    CHECK(error.find("GIMP Palette") != std::string::npos);
    CHECK_FALSE(readGpl("GIMP Palette\n1 2\n", &error));
    CHECK(error == "Line 2 isn't a color.");
    CHECK_FALSE(readGpl("GIMP Palette\n300 0 0\n", &error));
    CHECK_FALSE(readGpl("GIMP Palette\nName: x\n", &error));
    CHECK(error == "The palette has no colors.");
    std::string big = "GIMP Palette\n";
    for (int i = 0; i < 257; ++i)
        big += "0 0 0\n";
    CHECK_FALSE(readGpl(big, &error));
}

TEST_CASE("hex: read and write, with alpha")
{
    const auto file = parse("ff0000\n#00FF00\n\n0000ff80\n", ".hex");
    REQUIRE(file);
    REQUIRE(file->palette.size() == 3);
    CHECK(file->palette.color(1) == Rgba{0, 255, 0, 255});
    CHECK(file->palette.color(2) == Rgba{0, 0, 255, 128});
    CHECK(writeHex(file->palette) == "ff0000\n00ff00\n0000ff80\n");
    std::string error;
    CHECK_FALSE(parse("ff00\n", ".hex", &error));
    CHECK(error == "Line 1 isn't a hex color.");
    CHECK(writePalette(file->palette, "x", ".png") == std::nullopt);
}

TEST_CASE("palette from .aseprite and .png")
{
    auto bytes = readBytes(kCorpus / "indexed.aseprite");
    const auto fromSprite = readPalette(bytes, ".aseprite");
    REQUIRE(fromSprite);
    CHECK(fromSprite->palette.size() > 0);

    bytes = readBytes(kCorpus / "rgba_seq1.png");
    const auto fromImage = readPalette(bytes, ".png");
    REQUIRE(fromImage);
    CHECK(fromImage->palette.size() >= 1);
    for (const auto& e : fromImage->palette.entries)
        CHECK(e.color.a > 0); // transparent pixels skipped
    std::string error;
    CHECK_FALSE(readPalette(bytes, ".act", &error));
}

TEST_CASE("bundled presets all parse")
{
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(kCorpus / "../../resources/palettes")) {
        const auto file = readPalette(readBytes(entry.path()), ".gpl");
        CHECK_MESSAGE(file, entry.path().string());
        if (file) {
            CHECK_FALSE(file->name.empty());
            CHECK(file->palette.size() >= 4);
        }
        ++count;
    }
    CHECK(count == 7);
}

TEST_CASE("pal: JASC read and write, RIFF read")
{
    const auto file = parse("JASC-PAL\r\n0100\r\n2\r\n255 0 0\r\n0 128 255\r\n", ".pal");
    REQUIRE(file);
    REQUIRE(file->palette.size() == 2);
    CHECK(file->palette.color(1) == Rgba{0, 128, 255, 255});
    const std::string text = writePal(file->palette);
    const auto again = parse(text, ".pal");
    REQUIRE(again);
    CHECK(again->palette.color(0) == Rgba{255, 0, 0, 255});

    std::string error;
    CHECK_FALSE(parse("JASC-PAL\r\n0100\r\n3\r\n1 2 3\r\n", ".pal", &error));
    CHECK(error == "The palette file is cut short.");
    CHECK_FALSE(parse("nope", ".pal", &error));
    CHECK(error == "Not a palette file.");

    const std::vector<uint8_t> riff = {'R', 'I', 'F', 'F', 24, 0, 0, 0, 'P', 'A', 'L', ' ', 'd', 'a', 't', 'a',
                                       12, 0, 0, 0, 0, 3, 2, 0, 9, 8, 7, 0, 1, 2, 3, 0};
    const auto fromRiff = readPalette(riff, ".pal");
    REQUIRE(fromRiff);
    REQUIRE(fromRiff->palette.size() == 2);
    CHECK(fromRiff->palette.color(0) == Rgba{9, 8, 7, 255});
    CHECK(fromRiff->palette.color(1) == Rgba{1, 2, 3, 255});
}

TEST_CASE("act: 768 bytes, or a trailer with the count and transparent index")
{
    Palette palette;
    palette.entries.push_back({Rgba{1, 2, 3, 0}, {}});
    palette.entries.push_back({Rgba{4, 5, 6, 255}, {}});
    const std::vector<uint8_t> bytes = writeAct(palette);
    REQUIRE(bytes.size() == 772);
    const auto file = readAct(bytes);
    REQUIRE(file);
    REQUIRE(file->palette.size() == 2);
    CHECK(file->palette.color(0) == Rgba{1, 2, 3, 0});
    CHECK(file->palette.color(1) == Rgba{4, 5, 6, 255});
    CHECK(readAct(std::vector<uint8_t>(768, 7))->palette.size() == 256);
    CHECK_FALSE(readAct(std::vector<uint8_t>(10)));
}
