#include <doctest/doctest.h>

#include "Editor.h"
#include "ColorModeHelper.h"

#include <QSignalSpy>

namespace {

void dot(Editor& editor, QPoint at, const QColor& color)
{
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(color);
    editor.beginStroke(at, false);
    editor.endStroke();
}

} // namespace

TEST_CASE("editor: color mode conversion is one undo step and keeps the picture")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}, {"background", "white"}});
    dot(editor, {1, 1}, QColor(200, 30, 40));
    dot(editor, {2, 2}, QColor(10, 90, 250));
    const QImage before = editor.composite().copy();
    CHECK(editor.colorMode() == "rgba");
    CHECK(editor.spriteColorCount(255) == 3);
    CHECK(editor.spriteColorCount(1) == 2); // stops counting past the limit

    QSignalSpy modeChanges(&editor, &Editor::colorModeChanged);
    REQUIRE(toMode(editor, "indexed", "sprite"));
    CHECK(editor.colorMode() == "indexed");
    CHECK(modeChanges.count() == 1);
    CHECK(editor.palette().size() == 4); // transparent + 3
    CHECK(editor.composite() == before);
    CHECK(editor.modified());
    CHECK_FALSE(toMode(editor, "indexed")); // already

    // Drawing now uses the palette.
    dot(editor, {0, 0}, QColor(190, 40, 40));
    CHECK(editor.composite().pixelColor(0, 0) == QColor(200, 30, 40));
    editor.undo();

    editor.undo();
    CHECK(editor.colorMode() == "rgba");
    CHECK(modeChanges.count() == 2);
    CHECK(editor.composite() == before);
    editor.redo();
    CHECK(editor.colorMode() == "indexed");

    REQUIRE(toMode(editor, "grayscale"));
    CHECK(editor.composite().pixelColor(3, 3) == QColor(255, 255, 255));
    const QColor gray = editor.composite().pixelColor(1, 1);
    CHECK(gray.red() == gray.green());
    CHECK(gray.green() == gray.blue());
}

TEST_CASE("editor: color mode follows tab switches")
{
    Editor editor;
    editor.newSprite({{"width", 2}, {"height", 2}, {"colorMode", "grayscale"}});
    editor.newSprite({{"width", 2}, {"height", 2}});
    REQUIRE(editor.documents().size() == 2);
    QSignalSpy modeChanges(&editor, &Editor::colorModeChanged);
    editor.setCurrentDocument(0);
    CHECK(editor.colorMode() == "grayscale");
    CHECK(modeChanges.count() == 1);
    editor.setCurrentDocument(1);
    CHECK(editor.colorMode() == "rgba");
    CHECK(modeChanges.count() == 2);
}

TEST_CASE("editor: dithering to indexed spreads a flat gray over black and white")
{
    const auto whites = [](const QImage& image) {
        int n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                n += image.pixelColor(x, y) == QColor(255, 255, 255);
        return n;
    };
    const QVariantList colors = {QColor(0, 0, 0, 0), QColor(0, 0, 0), QColor(255, 255, 255)};
    const auto sprite = [&](Editor& editor) {
        editor.newSprite({{"width", 8}, {"height", 8}, {"background", "white"}});
        editor.setTool(Editor::Fill);
        editor.setPrimaryColor(QColor(128, 128, 128));
        editor.beginStroke({0, 0}, false);
        editor.endStroke();
        REQUIRE(editor.setPaletteColors(colors));
    };

    Editor plain;
    sprite(plain);
    REQUIRE(toMode(plain, "indexed", "current", "none"));
    CHECK(whites(plain.composite()) == 64); // every pixel rounds the same way

    for (const char* kind : {"ordered", "bayer2", "bayer8", "blue", "floyd", "atkinson", "sierra", "stucki", "riemersma"}) {
        CAPTURE(kind);
        Editor editor;
        sprite(editor);
        REQUIRE(toMode(editor, "indexed", "current", kind));
        const int white = whites(editor.composite());
        CHECK(white > 20);
        CHECK(white < 44); // about half
        editor.undo();
        CHECK(editor.colorMode() == "rgba");
    }
}

TEST_CASE("editor: the dither options reach the conversion")
{
    const auto whites = [](const QImage& image) {
        int n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                n += image.pixelColor(x, y) == QColor(255, 255, 255);
        return n;
    };
    const QVariantList colors = {QColor(0, 0, 0, 0), QColor(0, 0, 0), QColor(255, 255, 255)};
    const auto sprite = [&](Editor& editor) {
        editor.newSprite({{"width", 8}, {"height", 8}, {"background", "white"}});
        editor.setTool(Editor::Fill);
        editor.setPrimaryColor(QColor(128, 128, 128));
        editor.beginStroke({0, 0}, false);
        editor.endStroke();
        REQUIRE(editor.setPaletteColors(colors));
    };
    // Strength 0: the pattern does nothing, every pixel rounds the same way.
    Editor weak;
    sprite(weak);
    REQUIRE(toMode(weak, "indexed", "current", "ordered", 255, {}, false, {{"strength", 0}}));
    CHECK(whites(weak.composite()) == 64);
    // The OKLab option alone (no dither) still converts; the gray goes to the nearer color.
    Editor lab;
    sprite(lab);
    REQUIRE(toMode(lab, "indexed", "current", "none", 255, {}, false, {{"oklab", true}}));
    CHECK(lab.colorMode() == "indexed");
    CHECK(whites(lab.composite()) == 64);
    // Serpentine and the previews take the same options.
    Editor serp;
    sprite(serp);
    REQUIRE(serp.beginColorModePreview());
    serp.previewColorMode("indexed", "current", "floyd", 255, {}, false, {{"serpentine", true}});
    serp.cancelColorModePreview();
    REQUIRE(toMode(serp, "indexed", "current", "floyd", 255, {}, false, {{"serpentine", true}}));
    const int w = whites(serp.composite());
    CHECK(w > 20);
    CHECK(w < 44);
}

TEST_CASE("editor: the color mode preview shows the result without touching the document")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}, {"frames", 2}, {"background", "white"}});
    dot(editor, {1, 1}, QColor(200, 30, 40));
    const QImage before = editor.composite().copy();
    const QVariantList palette = editor.palette();
    editor.setCurrentFrame(0);

    REQUIRE(editor.beginColorModePreview());
    CHECK(editor.effectActive());
    const QVariantList shown = editor.previewColorMode("indexed", "sprite", "none", 255, {}, false, {});
    CHECK(shown.size() == 3); // transparent, white, red
    CHECK(editor.composite() == before); // the sprite's own colors are kept exactly
    editor.previewColorMode("grayscale", "current", "none", 255, {}, false, {});
    const QColor gray = editor.composite().pixelColor(1, 1);
    CHECK(gray.red() == gray.green());
    CHECK(gray.red() != 200);
    CHECK(editor.colorMode() == "rgba"); // nothing was converted
    CHECK(editor.palette() == palette);
    CHECK(editor.canUndo()); // only the dot

    editor.previewColorMode("rgba", "current", "none", 255, {}, false, {}); // back to the sprite's mode
    CHECK(editor.composite() == before);

    editor.previewColorMode("grayscale", "current", "none", 255, {}, false, {});
    editor.cancelColorModePreview();
    CHECK_FALSE(editor.effectActive());
    CHECK(editor.composite() == before);

    // Undo ends a preview before it undoes anything.
    REQUIRE(editor.beginColorModePreview());
    editor.previewColorMode("grayscale", "current", "none", 255, {}, false, {});
    editor.undo();
    CHECK_FALSE(editor.effectActive());
    CHECK(editor.composite() == before);
    CHECK(editor.canUndo());

    // And a real conversion afterwards is unaffected by the earlier previews.
    REQUIRE(toMode(editor, "grayscale"));
    CHECK(editor.colorMode() == "grayscale");
}

TEST_CASE("editor: converting to indexed takes N of the sprite's colors, or a chosen palette")
{
    const auto sprite = [](Editor& editor) {
        editor.newSprite({{"width", 4}, {"height", 4}, {"background", "white"}});
        dot(editor, {0, 0}, QColor(200, 30, 40));
        dot(editor, {1, 0}, QColor(10, 90, 250));
        dot(editor, {2, 0}, QColor(20, 200, 60));
    };

    Editor all;
    sprite(all);
    REQUIRE(toMode(all, "indexed", "sprite"));
    CHECK(all.palette().size() == 5); // transparent, white and three colors

    Editor few;
    sprite(few);
    REQUIRE(toMode(few, "indexed", "sprite", "none", 2));
    CHECK(few.palette().size() == 3); // transparent and two colors, nearest used

    Editor chosen;
    sprite(chosen);
    const QVariantList colors = {QColor(0, 0, 0), QColor(255, 255, 255), QColor(255, 0, 0)};
    REQUIRE(toMode(chosen, "indexed", "given", "none", 255, colors));
    CHECK(chosen.palette().size() == 4); // a transparent entry goes first
    CHECK(chosen.palette()[0].value<QColor>().alpha() == 0);
    CHECK(chosen.composite().pixelColor(3, 3) == QColor(255, 255, 255));
    CHECK(chosen.composite().pixelColor(0, 0) == QColor(255, 0, 0)); // the nearest chosen color

    // The preview agrees with what Convert makes, and shows its palette.
    Editor preview;
    sprite(preview);
    REQUIRE(preview.beginColorModePreview());
    const QVariantList shown = preview.previewColorMode("indexed", "given", "none", 255, colors, false, {});
    CHECK(shown.size() == 4);
    CHECK(preview.composite() == chosen.composite());
    CHECK(preview.colorMode() == "rgba");
}

TEST_CASE("editor: partly see-through pixels become palette entries with alpha when asked")
{
    const auto sprite = [](Editor& editor) {
        editor.newSprite({{"width", 4}, {"height", 4}});
        dot(editor, {0, 0}, QColor(200, 30, 40, 128));
        dot(editor, {1, 0}, QColor(200, 30, 40));
    };

    Editor opaque;
    sprite(opaque);
    REQUIRE(toMode(opaque, "indexed", "sprite"));
    CHECK(opaque.composite().pixelColor(0, 0).alpha() == 255); // dropped
    CHECK(opaque.palette().size() == 2); // transparent, red

    Editor kept;
    sprite(kept);
    REQUIRE(toMode(kept, "indexed", "sprite", "none", 255, {}, true));
    CHECK(kept.palette().size() == 3); // transparent, red, half red
    CHECK(kept.composite().pixelColor(0, 0).alpha() == 128);
    CHECK(kept.composite().pixelColor(1, 0).alpha() == 255);
    CHECK(kept.composite().pixelColor(2, 0).alpha() == 0);
}

TEST_CASE("editor: an indexed sprite's palette can be remade from its own colors")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}, {"background", "white"}});
    dot(editor, {0, 0}, QColor(200, 30, 40));
    dot(editor, {1, 0}, QColor(10, 90, 250));
    REQUIRE(toMode(editor, "indexed", "sprite"));
    // Many unused entries, then back to just what the sprite uses.
    editor.addPaletteColor(QColor(1, 2, 3));
    editor.addPaletteColor(QColor(4, 5, 6));
    const int bigger = editor.palette().size();
    const QImage before = editor.composite().copy();

    CHECK_FALSE(toMode(editor, "indexed", "current")); // same palette: nothing to do
    REQUIRE(toMode(editor, "indexed", "sprite"));
    CHECK(editor.colorMode() == "indexed");
    CHECK(editor.palette().size() < bigger);
    CHECK(editor.composite() == before); // exactly its own colors

    REQUIRE(toMode(editor, "indexed", "sprite", "none", 2)); // fewer: nearest used
    CHECK(editor.palette().size() == 3);
    editor.undo();
    CHECK(editor.composite() == before);
}
