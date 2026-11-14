#include <doctest/doctest.h>

#include "Editor.h"
#include "ColorModeHelper.h"
#include "PaletteLibrary.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

const QString kGameBoy = QStringLiteral(":/palettes/07-nintendo-gameboy-bgb.gpl");

} // namespace

TEST_CASE("palette library: built-ins first, user presets saved, listed and removed")
{
    QTemporaryDir dir;
    PaletteLibrary library(dir.path() + "/palettes");
    const QVariantList builtins = library.presets();
    REQUIRE(builtins.size() == 7);
    CHECK(builtins[0].toMap()["name"] == "PICO-8");
    CHECK(builtins[0].toMap()["colors"].toList().size() == 16);
    CHECK(builtins[0].toMap()["builtin"] == true);

    const QString path = library.presetPath("My: Palette");
    CHECK(path.endsWith("/My_ Palette.gpl"));
    CHECK(library.presetPath("a\\b/c").endsWith("/a_b_c.gpl"));
    CHECK_FALSE(library.hasPreset("My: Palette"));

    Editor editor;
    REQUIRE(editor.savePalette(path, "My: Palette"));
    library.reload();
    REQUIRE(library.presets().size() == 8);
    const QVariantMap mine = library.presets().last().toMap();
    CHECK(mine["name"] == "My: Palette");
    CHECK(mine["builtin"] == false);
    CHECK(mine["colors"].toList().size() == editor.palette().size());
    CHECK(library.hasPreset("my: palette"));

    CHECK_FALSE(library.removePreset(builtins[0].toMap()["path"].toString()));
    CHECK(library.removePreset(path));
    CHECK(library.presets().size() == 7);
}

TEST_CASE("editor: load palette replaces it as one undo step")
{
    Editor editor;
    const int before = editor.palette().size();
    REQUIRE(editor.loadPalette(kGameBoy));
    CHECK(editor.palette().size() == 4);
    CHECK(editor.modified());
    editor.undo();
    CHECK(editor.palette().size() == before);

    QSignalSpy errors(&editor, &Editor::errorOccurred);
    CHECK_FALSE(editor.loadPalette("missing.gpl"));
    CHECK(errors.count() == 1);
    CHECK(editor.palette().size() == before);
}

TEST_CASE("editor: loading a palette into an indexed sprite keeps indices")
{
    Editor editor;
    // Index 0 (black) stays index 0; white (index 16) is past the Game Boy
    // palette's end and takes its nearest color, the lightest green.
    editor.newSprite({{"width", 2}, {"height", 2}, {"colorMode", "indexed"}, {"background", "black"}});
    REQUIRE(editor.loadPalette(kGameBoy));
    CHECK(editor.composite().pixelColor(0, 0) == QColor(0x08, 0x18, 0x20));

    editor.newSprite({{"width", 2}, {"height", 2}, {"colorMode", "indexed"}, {"background", "white"}});
    REQUIRE(editor.loadPalette(kGameBoy));
    CHECK(editor.composite().pixelColor(0, 0) == QColor(0xe0, 0xf8, 0xd0));
    editor.undo();
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 255, 255));
}

TEST_CASE("editor: save and load a palette file round trip")
{
    QTemporaryDir dir;
    Editor editor;
    REQUIRE(editor.loadPalette(kGameBoy));
    const QVariantList colors = editor.palette();
    REQUIRE(editor.savePalette(dir.filePath("gb.hex"), "ignored"));
    QFile hex(dir.filePath("gb.hex"));
    REQUIRE(hex.open(QIODevice::ReadOnly));
    CHECK(hex.readAll() == "081820\n346856\n88c070\ne0f8d0\n");

    editor.newDocument(4, 4);
    REQUIRE(editor.loadPalette(dir.filePath("gb.hex")));
    CHECK(editor.palette() == colors);
}

namespace {

QColor entry(const Editor& editor, int i) { return editor.palette().at(i).value<QColor>(); }

// Makes the palette exactly `colors` (each step its own undo step).
void setPalette(Editor& editor, const std::vector<QColor>& colors)
{
    while (editor.palette().size() > int(colors.size()))
        REQUIRE(editor.removePaletteColor(editor.palette().size() - 1));
    for (size_t i = 0; i < colors.size(); ++i)
        if (int(i) < editor.palette().size())
            editor.setPaletteColor(int(i), colors[i]);
        else
            editor.addPaletteColor(colors[i]);
    REQUIRE(editor.palette().size() == int(colors.size()));
}

void dotAt(Editor& editor, QPoint p, const QColor& color)
{
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(color);
    editor.beginStroke(p, false);
    editor.endStroke();
}

const QColor kBlack(0, 0, 0), kWhite(255, 255, 255), kRed(255, 0, 0), kBlue(0, 0, 255), kGreen(0, 255, 0);

} // namespace

TEST_CASE("palette tools: ramp fills the range between its ends, one undo step")
{
    Editor editor;
    editor.newDocument(8, 8);
    setPalette(editor, {kRed, kBlack, kRed, kRed, kRed, kWhite, kBlue});
    editor.setShadeRange({1, 5});
    CHECK(editor.paletteToolEntries() == 5);
    REQUIRE(editor.rampPalette(false));
    CHECK(entry(editor, 1) == kBlack);
    CHECK(entry(editor, 5) == kWhite);
    for (int i = 2; i <= 4; ++i) {
        CHECK(entry(editor, i).red() == entry(editor, i).green()); // grays
        CHECK(entry(editor, i).red() > entry(editor, i - 1).red());
    }
    CHECK(entry(editor, 0) == kRed); // outside the range
    CHECK(entry(editor, 6) == kBlue);
    CHECK(editor.shadeRange() == QPoint(1, 5)); // still selected
    editor.undo();
    CHECK(entry(editor, 3) == kRed);

    editor.setShadeRange({1, 2});
    CHECK_FALSE(editor.rampPalette(false)); // nothing between the ends
}

TEST_CASE("palette tools: sorting an indexed sprite keeps the picture and the transparent entry")
{
    Editor editor;
    editor.newDocument(8, 8);
    setPalette(editor, {QColor(0, 0, 0, 0), kWhite, kRed, kBlack, kGreen, kBlue});
    dotAt(editor, {1, 1}, kWhite);
    dotAt(editor, {2, 1}, kRed);
    dotAt(editor, {3, 1}, kBlack);
    dotAt(editor, {4, 1}, kBlue);
    REQUIRE(toMode(editor, "indexed"));
    const QImage before = editor.composite();

    REQUIRE(editor.sortPalette("lightness"));
    CHECK(entry(editor, 0).alpha() == 0); // the transparent entry stays
    CHECK(entry(editor, 1) == kBlack);
    CHECK(entry(editor, 5) == kWhite);
    CHECK(editor.composite() == before);
    CHECK_FALSE(editor.sortPalette("lightness")); // already sorted

    REQUIRE(editor.reversePalette());
    CHECK(entry(editor, 1) == kWhite);
    CHECK(entry(editor, 0).alpha() == 0);
    CHECK(editor.composite() == before);
    editor.undo();
    editor.undo();
    CHECK(entry(editor, 2) == kRed);
    CHECK(editor.composite() == before);

    // Only the range.
    editor.setShadeRange({3, 5});
    REQUIRE(editor.reversePalette());
    CHECK(entry(editor, 3) == kBlue);
    CHECK(entry(editor, 5) == kBlack);
    CHECK(entry(editor, 1) == kWhite);
    CHECK(editor.composite() == before);
}

TEST_CASE("palette tools: reduce merges near colors; indexed pixels follow, RGBA pixels stay")
{
    const QColor nearRed(240, 10, 10);
    for (const bool indexed : {false, true}) {
        CAPTURE(indexed);
        Editor editor;
        editor.newDocument(8, 8);
        setPalette(editor, {QColor(0, 0, 0, 0), kRed, nearRed, kBlue, kWhite});
        for (int x = 0; x < 4; ++x)
            dotAt(editor, {x, 0}, kRed);
        dotAt(editor, {0, 1}, nearRed);
        dotAt(editor, {0, 2}, kBlue);
        if (indexed)
            REQUIRE(toMode(editor, "indexed"));
        const QColor nearBefore = editor.composite().pixelColor(0, 1);

        CHECK_FALSE(editor.reducePalette(editor.paletteToolEntries()));
        REQUIRE(editor.reducePalette(indexed ? 3 : 4));
        CHECK(editor.palette().size() == (indexed ? 4 : 4));
        const QColor merged = entry(editor, indexed ? 1 : 0);
        if (indexed) {
            CHECK(entry(editor, 0).alpha() == 0);
            CHECK(merged.red() > 240); // weighted toward the red the pixels use
            CHECK(editor.composite().pixelColor(0, 0) == merged);
            CHECK(editor.composite().pixelColor(0, 1) == merged); // near-red follows
            CHECK(editor.composite().pixelColor(0, 2) == kBlue);
        } else {
            CHECK(editor.composite().pixelColor(0, 1) == nearBefore); // pixels untouched
        }
        editor.undo();
        CHECK(editor.palette().size() == 5);
        CHECK(editor.composite().pixelColor(0, 1) == nearBefore);
    }
}

TEST_CASE("palette tools: reducing a range keeps the rest and shrinks the range")
{
    Editor editor;
    editor.newDocument(8, 8);
    setPalette(editor, {kBlack, kRed, QColor(250, 0, 0), QColor(245, 0, 0), kWhite});
    editor.setShadeRange({1, 3});
    REQUIRE(editor.reducePalette(1));
    CHECK(editor.palette().size() == 3);
    CHECK(entry(editor, 0) == kBlack);
    CHECK(entry(editor, 2) == kWhite);
    CHECK(editor.shadeRange() == QPoint(1, 1));
}

TEST_CASE("palette tools: remove unused and duplicate colors")
{
    for (const bool indexed : {false, true}) {
        CAPTURE(indexed);
        Editor editor;
        editor.newDocument(8, 8);
        // 0 transparent, 1 red, 2 blue, 3 red again, 4 white (never used)
        setPalette(editor, {QColor(0, 0, 0, 0), kRed, kBlue, kRed, kWhite});
        dotAt(editor, {0, 0}, kRed);
        dotAt(editor, {1, 0}, kBlue);
        if (indexed)
            REQUIRE(toMode(editor, "indexed"));
        const QImage before = editor.composite().copy();

        CHECK(editor.removableColors(false, true) == 1);
        CHECK(editor.removableColors(true, false) == (indexed ? 2 : 1)); // white; in indexed also the second red, no pixel has its index
        REQUIRE(editor.removeUnusedColors(true, true));
        CHECK(editor.palette().size() == 3); // transparent, red, blue
        CHECK(editor.composite() == before);
        CHECK_FALSE(editor.removeUnusedColors(true, true)); // nothing left to remove
        editor.undo();
        CHECK(editor.palette().size() == 5);
        CHECK(editor.composite() == before);
    }
}

TEST_CASE("palette tools: insert a ramp between two colors")
{
    for (const bool indexed : {false, true}) {
        CAPTURE(indexed);
        Editor editor;
        editor.newDocument(8, 8);
        setPalette(editor, {QColor(0, 0, 0, 0), kBlack, kWhite, kRed});
        dotAt(editor, {0, 0}, kRed);
        dotAt(editor, {1, 0}, kWhite);
        if (indexed)
            REQUIRE(toMode(editor, "indexed"));
        const QImage before = editor.composite().copy();

        CHECK_FALSE(editor.insertRamp(3, false)); // needs a range
        editor.setShadeRange({1, 2});
        REQUIRE(editor.insertRamp(3, false));
        CHECK(editor.palette().size() == 7);
        CHECK(entry(editor, 1) == kBlack);
        CHECK(entry(editor, 5) == kWhite);
        CHECK(entry(editor, 3).red() == entry(editor, 3).green()); // gray in between
        CHECK(entry(editor, 2).red() > 0);
        CHECK(entry(editor, 2).red() < entry(editor, 3).red());
        CHECK(entry(editor, 6) == kRed);
        CHECK(editor.shadeRange() == QPoint(1, 5));
        CHECK(editor.composite() == before); // pixels follow their colors
        editor.undo();
        CHECK(editor.palette().size() == 4);
        CHECK(editor.composite() == before);
    }
}

TEST_CASE("palette tools: insert works on the ends of a longer range")
{
    Editor editor;
    editor.newDocument(8, 8);
    setPalette(editor, {kBlack, kRed, kBlue, kWhite});
    editor.setShadeRange({0, 2}); // black, red, blue
    REQUIRE(editor.insertRamp(2, false));
    CHECK(editor.palette().size() == 6);
    CHECK(entry(editor, 0) == kBlack);
    CHECK(entry(editor, 1) == kRed); // the middle one stays put
    CHECK(entry(editor, 4) == kBlue);
    CHECK(entry(editor, 5) == kWhite);
    CHECK(editor.shadeRange() == QPoint(0, 4));
}

TEST_CASE("palettes: loading into an indexed sprite keeps indices or matches colors")
{
    for (const int mode : {0, 1}) {
        CAPTURE(mode);
        Editor editor;
        editor.newDocument(4, 1);
        // 0 transparent, 1 red, 2 blue
        setPalette(editor, {QColor(0, 0, 0, 0), kRed, kBlue});
        dotAt(editor, {0, 0}, kRed);
        dotAt(editor, {1, 0}, kBlue);
        REQUIRE(toMode(editor, "indexed"));
        editor.setPaletteLoadMode(mode);
        // The new palette lists blue-ish first and red-ish second.
        const QVariantList incoming{QColor(0, 0, 0, 0), QColor(10, 10, 240), QColor(240, 10, 10)};

        const QString preview = editor.palettePreview(incoming, 8);
        CHECK(preview.startsWith("data:image/png;base64,"));
        CHECK(editor.composite().pixelColor(0, 0) == kRed); // a preview changes nothing

        REQUIRE(editor.setPaletteColors(incoming));
        if (mode == 0) { // indices stay: red's pixel (1) is now the blue-ish color
            CHECK(editor.composite().pixelColor(0, 0) == QColor(10, 10, 240));
            CHECK(editor.composite().pixelColor(1, 0) == QColor(240, 10, 10));
        } else { // each pixel goes to its nearest color
            CHECK(editor.composite().pixelColor(0, 0) == QColor(240, 10, 10));
            CHECK(editor.composite().pixelColor(1, 0) == QColor(10, 10, 240));
        }
        CHECK(editor.composite().pixelColor(2, 0).alpha() == 0);
        editor.undo();
        CHECK(editor.composite().pixelColor(0, 0) == kRed);
    }

    Editor rgba;
    rgba.newDocument(2, 2);
    CHECK(rgba.palettePreview({QColor(1, 2, 3)}, 8).isEmpty()); // only indexed sprites change
}

TEST_CASE("palette tools: the previews give the colors Reduce keeps and Insert adds, changing nothing")
{
    Editor editor;
    editor.newDocument(8, 8);
    setPalette(editor, {QColor(0, 0, 0, 0), kRed, kBlue, kWhite});
    const QVariantList before = editor.palette();

    CHECK(editor.previewReduce(editor.paletteToolEntries()).isEmpty()); // nothing to reduce to
    const QVariantList kept = editor.previewReduce(2);
    CHECK(kept.size() == 2);
    CHECK(editor.palette() == before);

    CHECK(editor.previewInsert(2, false).isEmpty()); // needs a picked range
    editor.setShadeRange({1, 2});
    const QVariantList added = editor.previewInsert(2, false);
    CHECK(added.size() == 2);
    CHECK(editor.insertRamp(2, false)); // and Insert does make them
    CHECK(editor.palette()[2].value<QColor>() == added[0].value<QColor>());
    CHECK(editor.palette()[3].value<QColor>() == added[1].value<QColor>());
}

TEST_CASE("palette tools: Reduce can be shown on the canvas without changing the sprite")
{
    const QColor nearRed(240, 10, 10);
    Editor editor;
    editor.newDocument(8, 8);
    setPalette(editor, {QColor(0, 0, 0, 0), kRed, nearRed, kBlue, kWhite});
    for (int x = 0; x < 4; ++x)
        dotAt(editor, {x, 0}, kRed);
    dotAt(editor, {0, 1}, nearRed);
    REQUIRE(toMode(editor, "indexed"));
    const QImage before = editor.composite().copy();
    const QVariantList palette = editor.palette();
    const bool couldUndo = editor.canUndo();

    editor.previewReducePixels(3);
    CHECK(editor.effectActive());
    CHECK(editor.composite() != before);                       // near-red merged into red
    CHECK(editor.composite().pixelColor(0, 1) == editor.composite().pixelColor(0, 0));
    CHECK(editor.palette() == palette);                        // nothing was applied
    CHECK(editor.canUndo() == couldUndo);

    editor.previewReducePixels(editor.paletteToolEntries());   // nothing to reduce
    CHECK(editor.composite() == before);

    editor.previewReducePixels(3);
    editor.cancelColorModePreview();
    CHECK_FALSE(editor.effectActive());
    CHECK(editor.composite() == before);
    CHECK(editor.reducePalette(3)); // and Reduce itself still applies
    CHECK(editor.composite() != before);
}
