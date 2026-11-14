#include <doctest/doctest.h>

#include "Editor.h"
#include "ColorModeHelper.h"
#include "LospecClient.h"
#include "PaletteLibrary.h"

#include <QColor>
#include <QTemporaryDir>
#include <QUrlQuery>

// Shaped like Lospec's responses (trimmed); no network in tests.
namespace {

const QByteArray kList = R"({
  "palettes": [
    {"title": "Two Tone", "slug": "two-tone", "user": {"name": "someone"},
     "colors": ["000000", "ffffff"], "downloads": "1,234", "numberOfColors": 2},
    {"title": "No Slug", "colors": ["ff0000"]},
    {"title": "Odd Colors", "slug": "odd", "colors": ["12ab", "00ff00"]}
  ],
  "totalCount": 25
})";

const QByteArray kPalette = R"({"name": "PICO-8", "author": "", "colors": ["000000", "1D2B53", "FF004D"]})";

} // namespace

TEST_CASE("lospec: list parsing skips entries without a slug or colors")
{
    QVariantList results;
    int total = 0;
    QString error;
    REQUIRE(LospecClient::parseList(kList, results, total, &error));
    CHECK(total == 25);
    REQUIRE(results.size() == 2);
    const QVariantMap first = results[0].toMap();
    CHECK(first["name"] == "Two Tone");
    CHECK(first["slug"] == "two-tone");
    CHECK(first["author"] == "someone");
    CHECK(first["count"] == 2);
    CHECK(first["downloads"] == "1,234");
    CHECK(first["colors"].toList()[1].value<QColor>() == QColor(255, 255, 255));
    CHECK(results[1].toMap()["count"] == 1); // the malformed color is dropped

    CHECK_FALSE(LospecClient::parseList("not json", results, total, &error));
    CHECK_FALSE(error.isEmpty());
    CHECK_FALSE(LospecClient::parseList(R"({"something": "else"})", results, total, &error));
}

TEST_CASE("lospec: one palette")
{
    QVariantMap palette;
    QString error;
    REQUIRE(LospecClient::parsePalette(kPalette, "pico-8", palette, &error));
    CHECK(palette["name"] == "PICO-8");
    CHECK(palette["slug"] == "pico-8");
    CHECK(palette["count"] == 3);
    CHECK(palette["colors"].toList()[2].value<QColor>() == QColor(0xff, 0x00, 0x4d));
    CHECK_FALSE(LospecClient::parsePalette(R"({"name": "Empty", "colors": []})", "empty", palette, &error));
    CHECK(error.contains("empty"));
}

TEST_CASE("lospec: slugs from names, slugs and links")
{
    CHECK(LospecClient::slugFor("Sweetie 16") == "sweetie-16");
    CHECK(LospecClient::slugFor("  sweetie-16 ") == "sweetie-16");
    CHECK(LospecClient::slugFor("Endesga 32!") == "endesga-32");
    CHECK(LospecClient::slugFor("https://lospec.com/palette-list/sweetie-16") == "sweetie-16");
    CHECK(LospecClient::slugFor("lospec.com/palette-list/sweetie-16.json?x=1") == "sweetie-16");
    CHECK(LospecClient::slugFor("https://lospec.com/palette-list/sweetie-16/") == "sweetie-16");
    CHECK(LospecClient::slugFor("  ").isEmpty());
}

TEST_CASE("lospec: list request")
{
    const QUrl url = LospecClient::listUrl(" GameBoy ", 4, LospecClient::ExactCount, LospecClient::Downloads, 2);
    CHECK(url.host() == "lospec.com");
    CHECK(url.path() == "/palette-list/load");
    const QUrlQuery q(url);
    CHECK(q.queryItemValue("tag") == "gameboy");
    CHECK(q.queryItemValue("colorNumberFilterType") == "exact");
    CHECK(q.queryItemValue("colorNumber") == "4");
    CHECK(q.queryItemValue("sortingType") == "downloads");
    CHECK(q.queryItemValue("page") == "2");
}

TEST_CASE("lospec: a palette's colors load as one undo step; saving one as a preset")
{
    QVariantMap palette;
    REQUIRE(LospecClient::parsePalette(kPalette, "pico-8", palette, nullptr));
    const QVariantList colors = palette["colors"].toList();

    Editor editor;
    const QVariantList before = editor.palette();
    REQUIRE(editor.setPaletteColors(colors));
    CHECK(editor.palette() == colors);
    editor.undo();
    CHECK(editor.palette() == before);
    CHECK_FALSE(editor.setPaletteColors({}));

    // Indexed: pixels keep their indices, ones past the end take the nearest color.
    editor.newDocument(4, 4);
    editor.setPrimaryColor(editor.palette()[1].value<QColor>());
    editor.beginStroke({0, 0}, false);
    editor.endStroke();
    REQUIRE(toMode(editor, "indexed"));
    REQUIRE(editor.setPaletteColors(colors));
    CHECK(editor.composite().pixelColor(0, 0) == colors[1].value<QColor>());

    QTemporaryDir dir;
    PaletteLibrary library(dir.path() + "/palettes");
    REQUIRE(library.savePreset("PICO-8 (Lospec)", colors));
    CHECK(library.hasPreset("pico-8 (lospec)"));
    const QVariantMap saved = library.presets().last().toMap();
    CHECK(saved["name"] == "PICO-8 (Lospec)");
    CHECK(saved["colors"].toList().size() == 3);
    CHECK_FALSE(library.savePreset("", colors));
}

TEST_CASE("lospec: replies are kept on disk, one file per request")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QUrl a = LospecClient::listUrl("gameboy", 4, LospecClient::ExactCount, LospecClient::Downloads, 0);
    const QUrl b = LospecClient::listUrl("gameboy", 4, LospecClient::ExactCount, LospecClient::Downloads, 1);
    CHECK(LospecClient::loadCached(dir.path(), a).isEmpty());
    LospecClient::saveCached(dir.path(), a, kList);
    CHECK(LospecClient::loadCached(dir.path(), a) == kList);
    CHECK(LospecClient::loadCached(dir.path(), b).isEmpty());
    CHECK(LospecClient::cachePath(dir.path(), a) != LospecClient::cachePath(dir.path(), b));
}

TEST_CASE("palette library: renaming a user preset")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    PaletteLibrary library(dir.path());
    const QVariantList colors{QColor(255, 0, 0), QColor(0, 0, 255)};
    REQUIRE(library.savePreset("Old", colors));
    REQUIRE(library.savePreset("Taken", colors));
    CHECK_FALSE(library.renamePreset(library.presetPath("Old"), "Taken"));
    CHECK_FALSE(library.renamePreset(library.presetPath("Old"), "  "));
    REQUIRE(library.renamePreset(library.presetPath("Old"), "New"));
    CHECK(library.hasPreset("New"));
    CHECK_FALSE(library.hasPreset("Old"));
    bool found = false;
    for (const QVariant& p : library.presets())
        if (p.toMap().value("name").toString() == "New") {
            found = true;
            CHECK(p.toMap().value("colors").toList().size() == 2);
        }
    CHECK(found);
    CHECK_FALSE(library.renamePreset("/etc/passwd", "Nope"));
}
