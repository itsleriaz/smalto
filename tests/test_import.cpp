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
    // A logical screen of 9000 x 10 is refused before any frame is decoded.
    std::vector<uint8_t> big{'G', 'I', 'F', '8', '9', 'a', 0x28, 0x23, 10, 0, 0, 0, 0, ';'};
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

// --- Sprite sheets ---------------------------------------------------------------------------------

TEST_CASE("sheet slice: fit counts whole and partial tiles")
{
    formats::SheetSlice s;
    s.width = 16;
    s.height = 16;
    auto fit = formats::fitSheetSlice(40, 33, s, false);
    CHECK(fit.columns == 2);
    CHECK(fit.rows == 2);
    fit = formats::fitSheetSlice(40, 33, s, true);
    CHECK(fit.columns == 3);
    CHECK(fit.rows == 3);

    s.x = 8; // 32 pixels left: exactly two
    fit = formats::fitSheetSlice(40, 33, s, false);
    CHECK(fit.columns == 2);
    CHECK(formats::fitSheetSlice(40, 33, s, true).columns == 2);

    s.x = 0;
    s.padX = 2; // 16 + 2 + 16 = 34 <= 40; a third would start at 36
    CHECK(formats::fitSheetSlice(40, 33, s, false).columns == 2);
    CHECK(formats::fitSheetSlice(40, 33, s, true).columns == 3);

    s.x = 40; // outside the sheet
    CHECK(formats::fitSheetSlice(40, 33, s, true).columns == 0);
    s.x = 0;
    s.width = 50; // wider than the sheet
    CHECK(formats::fitSheetSlice(40, 33, s, false).columns == 0);
    CHECK(formats::fitSheetSlice(40, 33, s, true).columns == 1);
}

TEST_CASE("sheet slice: cells follow the order and the padding")
{
    formats::SheetSlice s;
    s.x = 1;
    s.y = 2;
    s.width = 4;
    s.height = 3;
    s.padX = 1;
    s.padY = 2;
    s.columns = 2;
    s.rows = 2;
    auto cells = formats::sheetSliceCells(s);
    REQUIRE(cells.size() == 4);
    CHECK(cells[0].x == 1);
    CHECK(cells[0].y == 2);
    CHECK(cells[1].x == 6); // 1 + 4 + 1
    CHECK(cells[1].y == 2);
    CHECK(cells[2].x == 1);
    CHECK(cells[2].y == 7); // 2 + 3 + 2

    s.order = formats::SheetOrder::Columns;
    cells = formats::sheetSliceCells(s);
    REQUIRE(cells.size() == 4);
    CHECK(cells[1].x == 1); // down first
    CHECK(cells[1].y == 7);
    CHECK(cells[2].x == 6);

    s.columns = 0;
    CHECK(formats::sheetSliceCells(s).empty());
}

TEST_CASE("sheet slice: tiles carry the sheet's pixels, cut-off ones are transparent")
{
    Image sheet(5, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            sheet.setPixel(x, y, pack({uint8_t(x * 10), uint8_t(y * 10), 7, 255}));
    formats::SheetSlice s;
    s.width = 2;
    s.height = 2;
    s.columns = 3; // the third reaches past the edge, as does the second row
    s.rows = 2;
    const auto tiles = formats::sliceSheet(sheet, s);
    REQUIRE(tiles.size() == 6);
    for (const Image& t : tiles) {
        CHECK(t.width() == 2);
        CHECK(t.height() == 2);
    }
    CHECK(tiles[1].pixel(1, 1) == sheet.pixel(3, 1));
    CHECK(tiles[2].pixel(0, 0) == sheet.pixel(4, 0));
    CHECK((tiles[2].pixel(1, 0) >> 24) == 0); // x = 5 is off the sheet
    CHECK(tiles[3].pixel(0, 0) == sheet.pixel(0, 2));
    CHECK((tiles[3].pixel(0, 1) >> 24) == 0); // y = 3 is off the sheet
}

// --- Folders of animations ------------------------------------------------------------------------------

namespace {

std::filesystem::path scratchFolder(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeSolidPng(const std::filesystem::path& path, int w, int h, uint8_t shade)
{
    std::filesystem::create_directories(path.parent_path());
    Image image(w, h);
    image.clear(pack({shade, shade, shade, 255}));
    const auto bytes = formats::encodePng(image);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

} // namespace

TEST_CASE("folder clips: numbered files group by name, others are clips of one")
{
    const auto dir = scratchFolder("smalto_clips_a");
    for (int i : {1, 2, 10})
        writeSolidPng(dir / ("walk_" + std::to_string(i) + ".png"), 4, 4, uint8_t(i));
    writeSolidPng(dir / "idle01.png", 4, 4, 1);
    writeSolidPng(dir / "idle02.png", 4, 4, 2);
    writeSolidPng(dir / "jump.png", 4, 4, 3);
    std::ofstream(dir / "notes.txt") << "not an image";

    const auto clips = formats::findImageClips(dir);
    REQUIRE(clips.size() == 3);
    CHECK(clips[0].name == "idle");
    CHECK(clips[0].files.size() == 2);
    CHECK(clips[1].name == "jump");
    CHECK(clips[1].files.size() == 1);
    CHECK(clips[2].name == "walk");
    REQUIRE(clips[2].files.size() == 3);
    CHECK(clips[2].files[0].filename() == "walk_1.png"); // numeric order: 1, 2, 10
    CHECK(clips[2].files[2].filename() == "walk_10.png");
    std::filesystem::remove_all(dir);
}

TEST_CASE("folder clips: subfolders name unnamed sequences, clashing names stay unique")
{
    const auto dir = scratchFolder("smalto_clips_b");
    writeSolidPng(dir / "run" / "1.png", 4, 4, 1);
    writeSolidPng(dir / "run" / "2.png", 4, 4, 2);
    writeSolidPng(dir / "hero" / "idle1.png", 4, 4, 1);
    writeSolidPng(dir / "idle1.png", 4, 4, 1);
    writeSolidPng(dir / "idle2.png", 4, 4, 2);

    const auto clips = formats::findImageClips(dir);
    REQUIRE(clips.size() == 3);
    std::vector<std::string> names;
    for (const auto& c : clips)
        names.push_back(c.name);
    CHECK(std::count(names.begin(), names.end(), "run") == 1);
    CHECK(std::count(names.begin(), names.end(), "idle") == 1);
    CHECK(std::count(names.begin(), names.end(), "hero_idle") == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("folder clips: one document, a tag for each clip")
{
    const auto dir = scratchFolder("smalto_clips_c");
    writeSolidPng(dir / "a1.png", 4, 4, 10);
    writeSolidPng(dir / "a2.png", 6, 3, 20);
    writeSolidPng(dir / "a3.png", 4, 4, 30);
    writeSolidPng(dir / "b.png", 4, 4, 40);

    std::string error;
    auto doc = formats::loadClipsDocument(formats::findImageClips(dir), &error);
    REQUIRE(doc);
    CHECK(doc->frameCount() == 4);
    CHECK(doc->width() == 6); // fits the largest
    CHECK(doc->height() == 4);
    REQUIRE(doc->tags.size() == 2);
    CHECK(doc->tags[0].name == "a");
    CHECK(doc->tags[0].from == 0);
    CHECK(doc->tags[0].to == 2);
    CHECK(doc->tags[1].name == "b");
    CHECK(doc->tags[1].from == 3);
    CHECK(doc->tags[1].to == 3);
    CHECK(doc->tags[0].color.a == 255); // visible, and not all one color
    CHECK(doc->tags[0].color != doc->tags[1].color);

    CHECK(!formats::loadClipsDocument({}, &error));
    std::filesystem::remove_all(dir);
}

TEST_CASE("images: pictures wider than 4096 load, up to the limit")
{
    std::string error;
    const auto wide = formats::decodeImage(formats::encodePng(Image(6000, 2)), &error);
    REQUIRE(wide);
    CHECK(wide->width() == 6000);
    CHECK_FALSE(formats::decodeImage(formats::encodePng(Image(formats::kMaxImageSize + 1, 2)), &error));
    CHECK(error.find("larger") != std::string::npos);
}
