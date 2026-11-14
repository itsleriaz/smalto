#include <doctest/doctest.h>

#include "Editor.h"
#include "core/Compositor.h"
#include "formats/Aseprite.h"
#include "formats/Export.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

// Exports are checked with Qt's own PNG/GIF decoders and JSON parser.

using namespace core;

namespace {

std::unique_ptr<Document> load(const char* name)
{
    auto doc = formats::loadAseprite(std::string(SMALTO_CORPUS_DIR) + "/" + name + ".aseprite");
    REQUIRE(doc);
    return doc;
}

QImage decode(const std::vector<uint8_t>& data, const char* format)
{
    QImage image;
    REQUIRE(image.loadFromData(data.data(), int(data.size()), format));
    return image.convertToFormat(QImage::Format_ARGB32);
}

QColor color(uint32_t rgba)
{
    const Rgba c = unpack(rgba);
    return QColor(c.r, c.g, c.b, c.a);
}

// True if `image` at (ox, oy) shows `expected` (RGBA8). Fully transparent
// pixels match whatever their color.
bool showsImage(const QImage& image, int ox, int oy, const Image& expected)
{
    for (int y = 0; y < expected.height(); ++y)
        for (int x = 0; x < expected.width(); ++x) {
            const QColor want = color(expected.pixel(x, y));
            const QColor got = image.pixelColor(ox + x, oy + y);
            if (want.alpha() == 0 && got.alpha() == 0)
                continue;
            if (want != got)
                return false;
        }
    return true;
}

std::vector<QImage> gifFrames(const std::vector<uint8_t>& data, std::vector<int>* delays = nullptr)
{
    QByteArray bytes(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()));
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer, "gif");
    std::vector<QImage> frames;
    for (;;) {
        QImage frame = reader.read();
        if (frame.isNull())
            break;
        frames.push_back(frame.convertToFormat(QImage::Format_ARGB32));
        if (delays)
            delays->push_back(reader.nextImageDelay());
    }
    return frames;
}

} // namespace

TEST_CASE("export: png is a scaled render of the frame")
{
    auto doc = load("rgba_features");
    const QImage png = decode(formats::encodePng(formats::renderScaled(*doc, 1, 3)), "png");
    REQUIRE(png.size() == QSize(72, 48));
    CHECK(showsImage(png, 0, 0, formats::renderScaled(*doc, 1, 3)));

    // Scaling is nearest neighbor.
    const Image plain = formats::renderScaled(*doc, 1, 1);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 24; ++x)
            CHECK(showsImage(png, x * 3 + 2, y * 3 + 1, plain.copy({x, y, 1, 1})));
}

TEST_CASE("export: png sequence names")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto doc = load("blend_modes"); // 19 frames
    formats::ExportOptions options;
    options.fromFrame = 7;
    options.toFrame = 9;
    const std::filesystem::path base = dir.filePath("walk.png").toStdString();
    REQUIRE(formats::exportPng(*doc, options, base));
    for (const char* name : {"walk-08.png", "walk-09.png", "walk-10.png"})
        CHECK(QFileInfo::exists(dir.filePath(name)));
    CHECK_FALSE(QFileInfo::exists(dir.filePath("walk.png")));

    options.fromFrame = options.toFrame = 3;
    REQUIRE(formats::exportPng(*doc, options, base));
    const QImage single(dir.filePath("walk.png"));
    CHECK(showsImage(single.convertToFormat(QImage::Format_ARGB32), 0, 0, formats::renderScaled(*doc, 3, 1)));
}

TEST_CASE("export: gif frames, timing and exact colors")
{
    auto doc = load("rgba_features"); // durations 100, 150, 80, 200
    formats::ExportOptions options;
    options.scale = 2;
    std::vector<int> delays;
    const auto frames = gifFrames(formats::encodeGif(*doc, options), &delays);
    REQUIRE(frames.size() == 4);
    CHECK(delays == std::vector<int>{100, 150, 80, 200});

    for (int f = 0; f < 4; ++f) {
        CAPTURE(f);
        const Image expected = formats::renderScaled(*doc, f, 2);
        int mismatches = 0;
        for (int y = 0; y < expected.height(); ++y)
            for (int x = 0; x < expected.width(); ++x) {
                QColor want = color(expected.pixel(x, y));
                const QColor got = frames[size_t(f)].pixelColor(x, y);
                // One-bit alpha: opaque from 128 up, with the straight color.
                if (want.alpha() < 128) {
                    mismatches += got.alpha() != 0;
                    continue;
                }
                want.setAlpha(255);
                mismatches += got != want;
            }
        CHECK(mismatches == 0);
    }
}

TEST_CASE("export: gif reduces a frame with many colors")
{
    Document doc(64, 64);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    doc.insertFrame(1, Frame{30});
    for (int f = 0; f < 2; ++f) {
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                cel->image->setPixel(x, y, pack({uint8_t(x * 4), uint8_t(y * 4), uint8_t(f * 200), 255}));
        layer->cels[size_t(f)] = cel;
    }
    std::vector<int> delays;
    const auto frames = gifFrames(formats::encodeGif(doc, {}), &delays);
    REQUIRE(frames.size() == 2);
    CHECK(delays[0] == 100);
    CHECK(delays[1] == 30);

    QSet<QRgb> distinct[2];
    int worst = 0;
    for (int f = 0; f < 2; ++f)
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const QColor got = frames[size_t(f)].pixelColor(x, y);
                distinct[f].insert(got.rgba());
                worst = std::max({worst, std::abs(got.red() - x * 4), std::abs(got.green() - y * 4),
                                  std::abs(got.blue() - f * 200)});
                CHECK(got.alpha() == 255);
            }
    CHECK(distinct[0].size() <= 255); // each frame has its own palette
    CHECK(distinct[1].size() <= 255);
    CHECK(worst <= 40);
}

TEST_CASE("export: gif frames keep their exact colors in their own palettes")
{
    // 200 reds in one frame and 200 blues in the next: 400 together, 200 each.
    Document doc(20, 10);
    Layer* layer = doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    doc.insertFrame(1, Frame{100});
    for (int f = 0; f < 2; ++f) {
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(20, 10);
        for (int i = 0; i < 200; ++i)
            cel->image->setPixel(i % 20, i / 20,
                                 f == 0 ? pack({uint8_t(50 + i), 0, 0, 255}) : pack({0, 0, uint8_t(50 + i), 255}));
        layer->cels[size_t(f)] = cel;
    }
    const auto frames = gifFrames(formats::encodeGif(doc, {}));
    REQUIRE(frames.size() == 2);
    int wrong = 0;
    for (int f = 0; f < 2; ++f)
        for (int i = 0; i < 200; ++i) {
            const QColor got = frames[size_t(f)].pixelColor(i % 20, i / 20);
            const QColor want = f == 0 ? QColor(50 + i, 0, 0) : QColor(0, 0, 50 + i);
            wrong += got != want;
        }
    CHECK(wrong == 0);
}

TEST_CASE("export: indexed png keeps every pixel, at the smallest depth")
{
    const auto make = [](int colors) {
        Image image(16, 16);
        for (int i = 0; i < 16 * 16; ++i) {
            const int c = i % (colors + 1); // the last one is transparent
            image.setPixel(i % 16, i / 16, c == colors ? 0u : pack({uint8_t(c * 3), uint8_t(255 - c), 7, uint8_t(c % 2 ? 255 : 200)}));
        }
        return image;
    };
    for (const auto& [colors, depth] : std::vector<std::pair<int, int>>{{1, 1}, {3, 2}, {10, 4}, {100, 8}}) {
        CAPTURE(colors);
        const Image image = make(colors);
        const auto bytes = formats::encodePngIndexed(image);
        QImage raw;
        REQUIRE(raw.loadFromData(bytes.data(), int(bytes.size()), "png"));
        CHECK(raw.colorCount() == colors + 1);
        CHECK(bytes[24] == depth); // IHDR bit depth
        CHECK(bytes[25] == 3);     // color type: palette
        CHECK(showsImage(raw.convertToFormat(QImage::Format_ARGB32), 0, 0, image));
        CHECK(bytes.size() < formats::encodePng(image).size() + 200); // not bigger than RGBA by much
    }
    // More than 256 colors: plain RGBA.
    Image many(32, 16);
    for (int i = 0; i < 32 * 16; ++i)
        many.setPixel(i % 32, i / 32, pack({uint8_t(i), uint8_t(i >> 8), 1, 255}));
    const auto bytes = formats::encodePngIndexed(many);
    CHECK(bytes == formats::encodePng(many));
    CHECK(bytes[25] == 6);
}

TEST_CASE("export: sprite sheet layout and json")
{
    auto doc = load("rgba_features"); // 24x16, 4 frames, tags walk 0-1, idle 2-3
    formats::SheetOptions options;
    options.layout = formats::SheetLayout::Grid;
    options.scale = 2;
    options.padding = 1;
    const auto sheet = formats::buildSpriteSheet(*doc, options, "hero.png");
    REQUIRE(sheet.image.width() == 2 * 48 + 1);
    REQUIRE(sheet.image.height() == 2 * 32 + 1);
    const QImage image = decode(formats::encodePng(sheet.image), "png");
    CHECK(showsImage(image, 49, 33, formats::renderScaled(*doc, 3, 2)));

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromStdString(sheet.json), &parseError);
    INFO(parseError.errorString().toStdString());
    REQUIRE(json.isObject());
    const QJsonArray frames = json["frames"].toArray();
    REQUIRE(frames.size() == 4);
    const QJsonObject last = frames[3].toObject();
    CHECK(last["frame"]["x"].toInt() == 49);
    CHECK(last["frame"]["y"].toInt() == 33);
    CHECK(last["frame"]["w"].toInt() == 48);
    CHECK(last["duration"].toInt() == 200);
    const QJsonObject meta = json["meta"].toObject();
    CHECK(meta["image"].toString() == "hero.png");
    CHECK(meta["size"]["w"].toInt() == 97);
    const QJsonArray tags = meta["frameTags"].toArray();
    REQUIRE(tags.size() == 2);
    CHECK(tags[0]["name"].toString() == "walk");
    CHECK(tags[0]["direction"].toString() == "pingpong");
    CHECK(tags[0]["repeat"].toString() == "2");
    CHECK(tags[1]["from"].toInt() == 2);

    // A range keeps only overlapping tags, renumbered.
    formats::SheetOptions range;
    range.fromFrame = 1;
    range.toFrame = 2;
    const auto part = formats::buildSpriteSheet(*doc, range, "part.png");
    CHECK(part.image.width() == 48);
    const QJsonArray partTags = QJsonDocument::fromJson(QByteArray::fromStdString(part.json))["meta"]["frameTags"].toArray();
    REQUIRE(partTags.size() == 2);
    CHECK(partTags[0]["from"].toInt() == 0);
    CHECK(partTags[0]["to"].toInt() == 0);
    CHECK(partTags[1]["from"].toInt() == 1);
    CHECK(partTags[1]["to"].toInt() == 1);
}

TEST_CASE("export: sprite sheet with one row per tag")
{
    auto doc = load("rgba_features"); // 24x16; tags walk 0-1, idle 2-3
    doc->tags.push_back({"everything", 0, 3, AniDirection::Forward, 0, {}, {}}); // overlaps both
    formats::SheetOptions options;
    options.layout = formats::SheetLayout::TagRows;
    options.fromFrame = options.toFrame = 1; // ignored by this layout
    const auto rows = formats::sheetRows(*doc, options);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0] == std::vector<int>{0, 1});
    CHECK(rows[1] == std::vector<int>{2, 3});
    CHECK(rows[2] == std::vector<int>{0, 1, 2, 3});

    const auto sheet = formats::buildSpriteSheet(*doc, options, "tags.png");
    REQUIRE(sheet.image.width() == 4 * 24); // widest row
    REQUIRE(sheet.image.height() == 3 * 16);
    const QImage image = decode(formats::encodePng(sheet.image), "png");
    CHECK(showsImage(image, 24, 16, formats::renderScaled(*doc, 3, 1)));     // idle, 2nd cell
    CHECK(showsImage(image, 3 * 24, 32, formats::renderScaled(*doc, 3, 1))); // everything, 4th cell
    // Short rows leave the rest of the row transparent.
    CHECK(image.pixelColor(2 * 24 + 5, 5).alpha() == 0);

    const QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromStdString(sheet.json));
    const QJsonArray frames = json["frames"].toArray();
    REQUIRE(frames.size() == 8); // a cell per tag frame, duplicates included
    CHECK(frames[4]["filename"].toString() == "0");
    CHECK(frames[4]["frame"]["y"].toInt() == 32);
    const QJsonArray tags = json["meta"]["frameTags"].toArray();
    REQUIRE(tags.size() == 3);
    CHECK(tags[1]["from"].toInt() == 2);
    CHECK(tags[1]["to"].toInt() == 3);
    CHECK(tags[2]["name"].toString() == "everything");
    CHECK(tags[2]["from"].toInt() == 4);
    CHECK(tags[2]["to"].toInt() == 7);

    // Without tags it falls back to one row of the range.
    doc->tags.clear();
    CHECK(formats::sheetRows(*doc, options) == std::vector<std::vector<int>>{{1}});
}

TEST_CASE("export preview: frames, GIF as it will look, sheets as one image")
{
    Editor editor;
    REQUIRE(editor.openPath(QString(SMALTO_CORPUS_DIR) + "/rgba_features.aseprite"));
    const int frames = editor.frameCount();
    REQUIRE(frames > 1);

    // PNG: every frame at its duration, the frame as composited, scale ignored.
    QVariantMap png{{"format", "png"}, {"range", "all"}, {"scale", 4}};
    QVariantList durations = editor.prepareExportPreview(png)["durations"].toList();
    REQUIRE(durations.size() == frames);
    editor.setCurrentFrame(1);
    CHECK(durations[1].toInt() == editor.frameDuration());
    const QImage second = editor.exportPreviewFrame(1);
    CHECK(second.size() == QSize(editor.canvasWidth(), editor.canvasHeight()));
    CHECK(second == editor.composite());
    CHECK(editor.exportPreviewFrame(frames).isNull());
    CHECK(editor.prepareExportPreview({{"format", "png"}, {"range", "current"}})["durations"].toList().size() == 1);

    // GIF: decoded from the encoded file, so alpha is on / off.
    durations = editor.prepareExportPreview({{"format", "gif"}, {"range", "all"}})["durations"].toList();
    REQUIRE(durations.size() == frames);
    for (int f = 0; f < frames; ++f) {
        CHECK(durations[f].toInt() % 10 == 0); // hundredths of a second
        const QImage image = editor.exportPreviewFrame(f);
        REQUIRE(image.size() == QSize(editor.canvasWidth(), editor.canvasHeight()));
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const int a = image.pixelColor(x, y).alpha();
                CHECK((a == 0 || a == 255));
            }
    }

    // Sheet: one image, the export's size at scale 1.
    const QVariantMap sheet{{"format", "sheet"}, {"range", "all"}, {"scale", 3}, {"layout", "grid"}};
    CHECK(editor.prepareExportPreview(sheet)["durations"].toList().size() == 1);
    QVariantMap unscaled = sheet;
    unscaled["scale"] = 1;
    const QVariantMap info = editor.exportInfo(unscaled);
    CHECK(editor.exportPreviewFrame(0).size() == QSize(info["width"].toInt(), info["height"].toInt()));
}

TEST_CASE("export: sprite sheet JSON lists slices, cut to the range and scaled")
{
    Document doc(8, 8);
    doc.insertLayer(doc.root(), 0, doc.makeImageLayer("l"));
    doc.insertFrame(1, Frame{100});
    doc.insertFrame(2, Frame{100});
    Slice slice;
    slice.name = "button";
    slice.color = Rgba{255, 0, 16, 255};
    slice.nineSlice = true;
    slice.hasPivot = true;
    slice.keys = {{0, Rect{1, 1, 4, 4}, Rect{1, 1, 2, 2}, Point{2, 3}}, {2, Rect{2, 2, 5, 5}, Rect{1, 1, 3, 3}, Point{0, 0}}};
    // User data: flag 1 (text) and "hi".
    slice.userData = {1, 0, 0, 0, 2, 0, 'h', 'i'};
    doc.slices.push_back(slice);

    formats::SheetOptions options;
    options.scale = 2;
    const auto all = QJsonDocument::fromJson(QByteArray::fromStdString(formats::buildSpriteSheet(doc, options, "s.png").json));
    const QJsonArray slices = all["meta"]["slices"].toArray();
    REQUIRE(slices.size() == 1);
    const QJsonObject s = slices[0].toObject();
    CHECK(s["name"].toString() == "button");
    CHECK(s["color"].toString() == "#ff0010ff");
    CHECK(s["data"].toString() == "hi");
    const QJsonArray keys = s["keys"].toArray();
    REQUIRE(keys.size() == 2);
    CHECK(keys[0]["frame"].toInt() == 0);
    CHECK(keys[0]["bounds"]["x"].toInt() == 2); // scaled 2x
    CHECK(keys[0]["bounds"]["w"].toInt() == 8);
    CHECK(keys[0]["center"]["w"].toInt() == 4);
    CHECK(keys[0]["pivot"]["y"].toInt() == 6);
    CHECK(keys[1]["frame"].toInt() == 2);

    // Frames 1..2: the first key (in effect at 1) is frame 0, the second frame 1.
    options.scale = 1;
    options.fromFrame = 1;
    options.toFrame = 2;
    const auto part = QJsonDocument::fromJson(QByteArray::fromStdString(formats::buildSpriteSheet(doc, options, "s.png").json));
    const QJsonArray partKeys = part["meta"]["slices"][0]["keys"].toArray();
    REQUIRE(partKeys.size() == 2);
    CHECK(partKeys[0]["frame"].toInt() == 0);
    CHECK(partKeys[1]["frame"].toInt() == 1);
    // Frame 2 only: just the second key.
    options.fromFrame = 2;
    const auto last = QJsonDocument::fromJson(QByteArray::fromStdString(formats::buildSpriteSheet(doc, options, "s.png").json));
    CHECK(last["meta"]["slices"][0]["keys"].toArray().size() == 1);
    CHECK(last["meta"]["slices"][0]["keys"][0]["bounds"]["x"].toInt() == 2);
}

TEST_CASE("export: the sheet preview says where each frame's cell is")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 6}, {"frames", 5}});
    const QVariantMap grid = editor.prepareExportPreview({{"format", "sheet"}, {"layout", "grid"}});
    const QVariantList cells = grid.value("cells").toList();
    REQUIRE(cells.size() == 5); // 3 columns, 2 rows
    CHECK(cells[0].toRectF() == QRectF(0, 0, 8, 6));
    CHECK(cells[2].toRectF() == QRectF(16, 0, 8, 6));
    CHECK(cells[3].toRectF() == QRectF(0, 6, 8, 6));
    CHECK(editor.prepareExportPreview({{"format", "gif"}}).value("cells").toList().isEmpty());
}
