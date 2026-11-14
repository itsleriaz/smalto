#include <doctest/doctest.h>

#include "core/Compositor.h"
#include "formats/Export.h"
#include "formats/ImageImport.h"
#include "formats/PaletteIO.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

using namespace core;

// Images in tests/corpus are written by Aseprite (see generate.lua and
// images.lua): rgba_seq1..4.png are the frames of rgba_features, indexed.png
// is a palette PNG of indexed.aseprite, anim.gif has 3 frames with
// transparency, still.bmp / .tga / .jpg one opaque frame. NAME.rgba holds
// Aseprite's renders.

namespace {

const std::filesystem::path kCorpus = SMALTO_CORPUS_DIR;

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Compares an RGBA8 image with frame `frame` of a .rgba reference; fully
// transparent pixels match whatever their color.
bool matchesReference(const Image& image, const char* name, int frame)
{
    const auto ref = readFile(kCorpus / (std::string(name) + ".rgba"));
    const size_t bytes = size_t(image.width()) * image.height() * 4;
    REQUIRE(ref.size() >= bytes * size_t(frame + 1));
    const uint8_t* expected = ref.data() + bytes * size_t(frame);
    for (size_t i = 0; i < bytes; i += 4) {
        if (image.data()[i + 3] == 0 && expected[i + 3] == 0)
            continue;
        if (std::memcmp(image.data() + i, expected + i, 4) != 0)
            return false;
    }
    return true;
}

// A fresh empty directory under the system temp folder.
std::filesystem::path tempDir()
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() / ("smalto-test-" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return dir;
}

void touch(const std::filesystem::path& path) { std::ofstream(path) << "x"; }

} // namespace

TEST_CASE("png: decodes what we encode")
{
    Image image(5, 3);
    image.setPixel(0, 0, pack({1, 2, 3, 255}));
    image.setPixel(4, 2, pack({200, 100, 50, 77}));
    const auto decoded = formats::decodeImage(formats::encodePng(image));
    REQUIRE(decoded);
    CHECK(*decoded == image);
}

TEST_CASE("png: Aseprite's PNGs decode to its renders")
{
    for (int f = 0; f < 4; ++f) {
        CAPTURE(f);
        std::string error;
        const auto image = formats::loadImage(kCorpus / ("rgba_seq" + std::to_string(f + 1) + ".png"), &error);
        INFO(error);
        REQUIRE(image);
        CHECK(image->width() == 24);
        CHECK(matchesReference(*image, "rgba_features", f));
    }
    const auto indexed = formats::loadImage(kCorpus / "indexed.png"); // palette PNG
    REQUIRE(indexed);
    CHECK(matchesReference(*indexed, "indexed", 0));
}

TEST_CASE("png: numbered sequences")
{
    const auto seq = formats::findImageSequence(kCorpus / "rgba_seq3.png");
    REQUIRE(seq.size() == 4);
    for (int i = 0; i < 4; ++i)
        CHECK(seq[size_t(i)].filename() == "rgba_seq" + std::to_string(i + 1) + ".png");
    CHECK(formats::findImageSequence(kCorpus / "indexed.png") == std::vector{kCorpus / "indexed.png"});

    const auto dir = tempDir();
    for (const char* name : {"walk-10.png", "walk-2.png", "walk-1.png", "walk-.png", "walk.png", "run-1.png",
                             "walk-3.txt", "walk-4.PNG"})
        touch(dir / name);
    const auto walk = formats::findImageSequence(dir / "walk-2.png");
    std::vector<std::string> names;
    for (const auto& p : walk)
        names.push_back(p.filename().string());
    // Extensions match in any case; numbers sort numerically.
    CHECK(names == std::vector<std::string>{"walk-1.png", "walk-2.png", "walk-4.PNG", "walk-10.png"});
    // Alone in its sequence: just the file.
    CHECK(formats::findImageSequence(dir / "run-1.png").size() == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("png: sequence becomes an animated document")
{
    std::string error;
    auto doc = formats::loadImageDocument(formats::findImageSequence(kCorpus / "rgba_seq1.png"), &error);
    INFO(error);
    REQUIRE(doc);
    CHECK(doc->width() == 24);
    CHECK(doc->height() == 16);
    REQUIRE(doc->frameCount() == 4);
    CHECK(doc->root().children.size() == 1);
    // Few colors: the palette is the image's colors.
    CHECK(doc->palette.size() > 0);
    CHECK(doc->palette.size() <= 256);
    for (int f = 0; f < 4; ++f) {
        Image out(24, 16);
        renderFrame(*doc, f, out, doc->bounds());
        CHECK(matchesReference(out, "rgba_features", f));
    }
}

TEST_CASE("png: bad input fails cleanly")
{
    std::string error;
    CHECK_FALSE(formats::decodeImage(std::vector<uint8_t>{1, 2, 3}, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(formats::loadImageDocument({"/nonexistent/x.png"}, &error));

    const auto data = readFile(kCorpus / "indexed.png");
    for (size_t n = 0; n < data.size(); n += 3)
        formats::decodeImage(std::span(data.data(), n)); // must not crash

    CHECK_FALSE(formats::decodeImage(formats::encodePng(Image(formats::kMaxImageSize + 1, 1)), &error));
    CHECK(error.find("larger") != std::string::npos);
}

TEST_CASE("images: extensions")
{
    for (const char* ext : {"png", ".PNG", "gif", "jpg", ".jpeg", "Bmp", "tga"})
        CHECK(formats::isImageExtension(ext));
    for (const char* ext : {"", "aseprite", ".webp", "pngx"})
        CHECK_FALSE(formats::isImageExtension(ext));
}

TEST_CASE("images: BMP and TGA match Aseprite's render, JPEG closely")
{
    for (const std::string file : {"still.bmp", "still.tga", "still.jpg"}) {
        CAPTURE(file);
        std::string error;
        const auto image = formats::loadImage(kCorpus / file, &error);
        REQUIRE_MESSAGE(image, error);
        CHECK(image->width() == 16);
        CHECK(image->height() == 16);
        if (!file.ends_with(".jpg")) {
            CHECK(matchesReference(*image, "still", 0));
            continue;
        }
        // Lossy: block centers are close, edges blur (chroma subsampling).
        const auto ref = readFile(kCorpus / "still.rgba");
        int total = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const Rgba got = unpack(image->pixel(x, y));
                const uint8_t* want = ref.data() + (y * 16 + x) * 4;
                const int d = std::abs(got.r - want[0]) + std::abs(got.g - want[1]) + std::abs(got.b - want[2]);
                total += d;
                if ((x % 8 == 3 || x % 8 == 4) && (y % 8 == 3 || y % 8 == 4))
                    CHECK(d <= 24);
                CHECK(got.a == 255);
            }
        CHECK(total / 256 <= 24);
    }
}

TEST_CASE("images: a GIF opens with every frame and its delay")
{
    std::string error;
    const auto frames = formats::loadImageFrames(kCorpus / "anim.gif", &error);
    REQUIRE_MESSAGE(frames, error);
    REQUIRE(frames->size() == 3);
    const int delays[] = {100, 250, 40};
    for (int f = 0; f < 3; ++f) {
        CAPTURE(f);
        CHECK((*frames)[size_t(f)].durationMs == delays[f]);
        CHECK(matchesReference((*frames)[size_t(f)].image, "anim", f));
    }
    CHECK(unpack((*frames)[0].image.pixel(0, 0)).a == 0); // transparent background

    auto doc = formats::loadImageDocument({kCorpus / "anim.gif", kCorpus / "still.bmp"}, &error);
    REQUIRE_MESSAGE(doc, error);
    CHECK(doc->frameCount() == 4);
    CHECK(doc->frame(1).durationMs == 250);
    CHECK(doc->frame(3).durationMs == 100);
    CHECK(doc->width() == 16);
    CHECK(doc->height() == 16);
    Image out(doc->width(), doc->height());
    renderFrame(*doc, 2, out, doc->bounds());
    CHECK(out.copy({0, 0, 16, 12}) == (*frames)[2].image);

    // The first frame alone, and its colors as a palette.
    const auto first = formats::loadImage(kCorpus / "anim.gif");
    REQUIRE(first);
    CHECK(*first == (*frames)[0].image);
    const auto data = readFile(kCorpus / "anim.gif");
    const auto palette = formats::readPalette(data, ".gif", &error);
    REQUIRE_MESSAGE(palette, error);
    CHECK(palette->palette.size() == 3);
}

TEST_CASE("images: GIFs too large or broken fail cleanly")
{
    // A logical screen of 5000 x 10 is refused before any frame is decoded.
    std::vector<uint8_t> big{'G', 'I', 'F', '8', '9', 'a', 0x88, 0x13, 10, 0, 0, 0, 0, ';'};
    std::string error;
    CHECK_FALSE(formats::decodeImageFrames(big, &error));
    CHECK(error.find("larger") != std::string::npos);
    CHECK_FALSE(formats::decodeImage(big, &error));

    const auto data = readFile(kCorpus / "anim.gif");
    for (size_t n = 0; n < data.size(); ++n)
        formats::decodeImageFrames(std::span(data.data(), n)); // must not crash
}

TEST_CASE("sequences: any image type, one file for each number")
{
    const auto dir = tempDir();
    for (const char* name : {"walk-1.png", "walk-2.jpg", "walk-3.png", "walk-3.jpg", "walk-4.bmp", "walk-5.txt"})
        touch(dir / name);
    const auto names = [](const std::vector<std::filesystem::path>& files) {
        std::vector<std::string> out;
        for (const auto& p : files)
            out.push_back(p.filename().string());
        return out;
    };
    // The types mix; where two files have one number, the one of the file we started from wins.
    CHECK(names(formats::findImageSequence(dir / "walk-1.png"))
          == std::vector<std::string>{"walk-1.png", "walk-2.jpg", "walk-3.png", "walk-4.bmp"});
    CHECK(names(formats::findImageSequence(dir / "walk-3.jpg"))
          == std::vector<std::string>{"walk-1.png", "walk-2.jpg", "walk-3.jpg", "walk-4.bmp"});
    std::filesystem::remove_all(dir);
}

TEST_CASE("jpeg: the EXIF orientation is read, and the picture is turned upright")
{
    // SOI, then an APP1 with a TIFF header and one directory entry: Orientation (0x0112), a SHORT.
    const auto jpegWith = [](bool little, int orientation) {
        std::vector<uint8_t> tiff;
        const auto u16 = [&](unsigned v) {
            if (little) { tiff.push_back(uint8_t(v)); tiff.push_back(uint8_t(v >> 8)); }
            else { tiff.push_back(uint8_t(v >> 8)); tiff.push_back(uint8_t(v)); }
        };
        const auto u32 = [&](unsigned v) { if (little) { u16(v & 0xffff); u16(v >> 16); } else { u16(v >> 16); u16(v & 0xffff); } };
        tiff.push_back(little ? 'I' : 'M');
        tiff.push_back(little ? 'I' : 'M');
        u16(42);
        u32(8);          // the first directory
        u16(1);          // one entry
        u16(0x0112);     // Orientation
        u16(3);          // a SHORT
        u32(1);          // one value
        u16(unsigned(orientation));
        u16(0);
        u32(0);          // no next directory
        std::vector<uint8_t> out{0xFF, 0xD8, 0xFF, 0xE1};
        const size_t length = 2 + 6 + tiff.size();
        out.push_back(uint8_t(length >> 8));
        out.push_back(uint8_t(length));
        for (const char c : std::string("Exif\0\0", 6))
            out.push_back(uint8_t(c));
        out.insert(out.end(), tiff.begin(), tiff.end());
        out.insert(out.end(), {0xFF, 0xDA}); // the picture starts
        return out;
    };
    for (const bool little : {true, false})
        for (int o = 1; o <= 8; ++o) {
            CAPTURE(little);
            CHECK(formats::jpegOrientation(jpegWith(little, o)) == o);
        }
    CHECK(formats::jpegOrientation(jpegWith(true, 9)) == 1); // not an orientation
    CHECK(formats::jpegOrientation(std::vector<uint8_t>{0x89, 'P', 'N', 'G'}) == 1); // not a JPEG
    CHECK(formats::jpegOrientation(std::vector<uint8_t>{0xFF, 0xD8, 0xFF, 0xDA, 0, 2}) == 1); // no EXIF
    auto cut = jpegWith(true, 6);
    cut.resize(12); // cut off in the middle of the header
    CHECK(formats::jpegOrientation(cut) == 1);

    // Where each pixel of a 3 x 2 picture goes (EXIF's own table).
    Image src(3, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            src.setPixel(x, y, pack({uint8_t(x * 10 + 1), uint8_t(y * 10 + 1), 0, 255}));
    const auto check = [&](int orientation, int w, int h, auto where) {
        const Image out = formats::orientedImage(src, orientation);
        CAPTURE(orientation);
        REQUIRE(out.width() == w);
        REQUIRE(out.height() == h);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x) {
                const auto [dx, dy] = where(x, y);
                CHECK(out.pixel(dx, dy) == src.pixel(x, y));
            }
    };
    check(1, 3, 2, [](int x, int y) { return std::pair{x, y}; });
    check(2, 3, 2, [](int x, int y) { return std::pair{2 - x, y}; });
    check(3, 3, 2, [](int x, int y) { return std::pair{2 - x, 1 - y}; });
    check(4, 3, 2, [](int x, int y) { return std::pair{x, 1 - y}; });
    check(5, 2, 3, [](int x, int y) { return std::pair{y, x}; });
    check(6, 2, 3, [](int x, int y) { return std::pair{1 - y, x}; });
    check(7, 2, 3, [](int x, int y) { return std::pair{1 - y, 2 - x}; });
    check(8, 2, 3, [](int x, int y) { return std::pair{y, 2 - x}; });
}
