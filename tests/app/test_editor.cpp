#include <doctest/doctest.h>

#include "Editor.h"
#include "TimelineModel.h"
#include "formats/Aseprite.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kCorpus = QStringLiteral(SMALTO_CORPUS_DIR);

QVariantMap row(const Editor& editor, int index) { return editor.layers().at(index).toMap(); }

QStringList names(const Editor& editor)
{
    QStringList out;
    for (const QVariant& v : editor.layers())
        out << v.toMap().value("name").toString();
    return out;
}

int rowNamed(const Editor& editor, const QString& name) { return int(names(editor).indexOf(name)); }

QColor pixel(const Editor& editor, int x, int y) { return editor.composite().pixelColor(x, y); }

void click(Editor& editor, QPoint p, bool secondary = false)
{
    editor.beginStroke(p, secondary);
    editor.endStroke();
}

} // namespace

TEST_CASE("editor: open lists nested layers and starts clean")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    CHECK(editor.fileName() == "rgba_features.aseprite");
    CHECK_FALSE(editor.modified());
    CHECK_FALSE(editor.canUndo());
    CHECK(editor.canvasWidth() == 24);
    CHECK(editor.frameCount() == 4);

    // Bottom to top, each group right after its children (shown above them).
    CHECK(names(editor)
          == QStringList{"bg", "multiply", "overlay child", "hidden child", "group", "locked ünïcode"});
    CHECK(row(editor, 4).value("kind") == "group");
    CHECK(row(editor, 2).value("depth") == 1);
    CHECK(row(editor, 3).value("shown") == false);
    CHECK(row(editor, 5).value("locked") == true);
    // Starts on the topmost image layer.
    CHECK(editor.currentLayer() == 5);
}

TEST_CASE("editor: collapsing a group hides its children and keeps the selection sensible")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    editor.setCurrentLayer(rowNamed(editor, "overlay child"));
    editor.setLayerCollapsed(rowNamed(editor, "group"), true);
    CHECK(names(editor) == QStringList{"bg", "multiply", "group", "locked ünïcode"});
    CHECK(editor.currentLayer() == 2); // moved up to the group
    CHECK_FALSE(editor.modified());
    editor.setLayerCollapsed(2, false);
    CHECK(names(editor).size() == 6);
}

TEST_CASE("editor: modified follows the undo stack and saving")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    QSignalSpy modifiedSpy(&editor, &Editor::modifiedChanged);
    CHECK(editor.fileName() == "Untitled");
    CHECK_FALSE(editor.saveFile()); // no file yet

    editor.setPrimaryColor(QColor(255, 0, 0));
    click(editor, {3, 4});
    CHECK(editor.modified());
    CHECK(pixel(editor, 3, 4) == QColor(255, 0, 0));
    editor.undo();
    CHECK_FALSE(editor.modified());
    editor.redo();
    CHECK(editor.modified());

    const QString path = dir.filePath("drawing"); // suffix gets added
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    CHECK(editor.fileName() == "drawing.aseprite");
    CHECK_FALSE(editor.modified());
    CHECK(modifiedSpy.count() >= 4);

    // Undoing past the save point is a change; coming back isn't.
    editor.undo();
    CHECK(editor.modified());
    editor.redo();
    CHECK_FALSE(editor.modified());

    // Edits outside the undo stack count until the next save.
    editor.setLayerVisible(0, false);
    CHECK(editor.modified());
    REQUIRE(editor.saveFile());
    CHECK_FALSE(editor.modified());

    Editor reopened;
    REQUIRE(reopened.openFile(QUrl::fromLocalFile(dir.filePath("drawing.aseprite"))));
    CHECK(row(reopened, 0).value("visible") == false);
    reopened.setLayerVisible(0, true);
    CHECK(pixel(reopened, 3, 4) == QColor(255, 0, 0));
}

TEST_CASE("editor: only image layers that are visible and unlocked take pixels")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    for (const char* name : {"group", "hidden child", "locked ünïcode"}) {
        CAPTURE(name);
        editor.setCurrentLayer(rowNamed(editor, QString::fromUtf8(name)));
        click(editor, {0, 0});
        CHECK_FALSE(editor.canUndo());
    }
    editor.setCurrentLayer(rowNamed(editor, "overlay child"));
    click(editor, {0, 0});
    CHECK(editor.canUndo());

    Editor tiles;
    REQUIRE(tiles.openPath(kCorpus + "/tilemap.aseprite"));
    tiles.setCurrentLayer(rowNamed(tiles, "tiles"));
    CHECK(row(tiles, tiles.currentLayer()).value("kind") == "tilemap");
    // Both modes draw on tilemaps: pixel mode through the tiles, tile mode tiles.
    tiles.setEditTiles(false);
    click(tiles, {1, 1});
    CHECK(tiles.canUndo());
    tiles.undo();
    tiles.setEditTiles(true);
    click(tiles, {1, 1});
    CHECK(tiles.canUndo());
}

TEST_CASE("editor: layers are added and removed inside groups")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    editor.setCurrentLayer(rowNamed(editor, "overlay child"));
    editor.addLayer();
    // Right above the current layer, in the same group.
    const int added = editor.currentLayer();
    CHECK(added == rowNamed(editor, "overlay child") + 1);
    CHECK(row(editor, added).value("depth") == 1);

    editor.removeLayer();
    CHECK(editor.currentLayer() == rowNamed(editor, "overlay child"));

    // Removing a group takes its children; undo brings them back.
    editor.setCurrentLayer(rowNamed(editor, "group"));
    editor.removeLayer();
    CHECK(names(editor) == QStringList{"bg", "multiply", "locked ünïcode"});
    CHECK(editor.currentLayer() == 1);
    editor.undo();
    CHECK(names(editor).size() == 6);
}

TEST_CASE("editor: drawing in indexed and grayscale documents")
{
    Editor indexed;
    REQUIRE(indexed.openPath(kCorpus + "/indexed.aseprite"));
    const QColor paletteColor = indexed.palette().at(6).value<QColor>();
    indexed.setPrimaryColor(paletteColor);
    indexed.setCurrentLayer(rowNamed(indexed, "top"));
    click(indexed, {2, 2});
    CHECK(pixel(indexed, 2, 2) == paletteColor);
    // Erasing writes the transparent index, showing the background below.
    const QColor below = pixel(indexed, 9, 9);
    indexed.setTool(Editor::Eraser);
    click(indexed, {9, 9});
    CHECK(pixel(indexed, 9, 9) != below);
    CHECK(pixel(indexed, 9, 9).alpha() == 255);

    Editor gray;
    REQUIRE(gray.openPath(kCorpus + "/grayscale.aseprite"));
    gray.setPrimaryColor(QColor(255, 255, 255));
    gray.setCurrentLayer(0); // the top layer is at 50% opacity
    click(gray, {0, 0});
    CHECK(pixel(gray, 0, 0) == QColor(255, 255, 255));
}

TEST_CASE("editor: a failed open keeps the current document")
{
    Editor editor;
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    click(editor, {1, 1});
    CHECK_FALSE(editor.openPath(kCorpus + "/missing.aseprite"));
    CHECK(errors.count() == 1);
    CHECK(editor.modified());
    CHECK(editor.canUndo());
}

TEST_CASE("editor: export ranges, sizes and files")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    REQUIRE(editor.tags().size() == 2);
    CHECK(editor.tags().at(1).toMap().value("name") == "idle");

    const QVariantMap sheet{{"format", "sheet"}, {"range", "all"}, {"scale", 2}, {"layout", "grid"}};
    const QVariantMap info = editor.exportInfo(sheet);
    CHECK(info.value("frames") == 4);
    CHECK(info.value("width") == 96);
    CHECK(info.value("height") == 64);
    CHECK(editor.exportInfo({{"format", "png"}, {"range", 1}}).value("frames") == 2);
    const QVariantMap byTag = editor.exportInfo({{"format", "sheet"}, {"layout", "tags"}, {"range", "current"}});
    CHECK(byTag.value("rows") == 2);
    CHECK(byTag.value("frames") == 4);
    CHECK(byTag.value("width") == 48);
    CHECK(byTag.value("height") == 32);
    // The layout only applies to sheets.
    CHECK(editor.exportInfo({{"format", "gif"}, {"layout", "tags"}}).value("frames") == 4);
    editor.setCurrentFrame(2);
    CHECK(editor.exportInfo({{"format", "gif"}, {"range", "current"}}).value("frames") == 1);

    REQUIRE(editor.exportFile(QUrl::fromLocalFile(dir.filePath("hero.png")), sheet));
    CHECK(QImage(dir.filePath("hero.png")).size() == QSize(96, 64));
    CHECK(QFileInfo::exists(dir.filePath("hero.json")));

    REQUIRE(editor.exportFile(QUrl::fromLocalFile(dir.filePath("idle.gif")), {{"format", "gif"}, {"range", 1}}));
    QImageReader gif(dir.filePath("idle.gif"));
    CHECK(gif.imageCount() == 2);

    CHECK_FALSE(editor.modified()); // exporting isn't an edit
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    CHECK_FALSE(editor.exportFile(QUrl::fromLocalFile(dir.filePath("missing/dir/x.png")), {}));
    CHECK(errors.count() == 1);
}

TEST_CASE("editor: PNGs open as new documents and import as layers")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_seq2.png"));
    CHECK(editor.fileName() == "rgba_seq2");
    CHECK(editor.filePath().isEmpty()); // saving asks where
    CHECK_FALSE(editor.modified());
    CHECK(editor.frameCount() == 1);

    const QStringList sequence = editor.imageSequence(kCorpus + "/rgba_seq2.png");
    REQUIRE(sequence.size() == 4);
    CHECK(sequence.first().endsWith("rgba_seq1.png"));
    REQUIRE(editor.openImages(sequence));
    CHECK(editor.fileName() == "rgba_seq");
    CHECK(editor.frameCount() == 4);

    // A GIF opens with its frames and delays; other types are images too.
    CHECK(editor.isImageFile("a.GIF"));
    CHECK(editor.isImageFile("a.jpeg"));
    CHECK_FALSE(editor.isImageFile("a.aseprite"));
    REQUIRE(editor.openPath(kCorpus + "/anim.gif"));
    CHECK(editor.fileName() == "anim");
    CHECK(editor.frameCount() == 3);
    editor.setCurrentFrame(1);
    CHECK(editor.frameDuration() == 250);
    CHECK(editor.imageSequence(kCorpus + "/anim.gif").size() == 1);
    REQUIRE(editor.openPath(kCorpus + "/still.jpg"));
    CHECK(editor.frameCount() == 1);

    // Import into an indexed sprite: converted to palette indices, undoable.
    Editor indexed;
    REQUIRE(indexed.openPath(kCorpus + "/indexed.aseprite"));
    const QImage before = indexed.composite().copy();
    REQUIRE(indexed.importLayer(QUrl::fromLocalFile(kCorpus + "/rgba_seq1.png")));
    // 24 x 16 into 16 x 16: it starts transformed, fitted; Enter keeps that.
    CHECK(indexed.floating());
    indexed.commitFloating();
    CHECK(indexed.modified());
    CHECK(names(indexed).last() == "rgba_seq1");
    CHECK(indexed.currentLayer() == names(indexed).size() - 1);
    CHECK(indexed.composite() != before);
    indexed.undo(); // the fitting
    CHECK(names(indexed).size() == 3);
    indexed.undo(); // the import
    CHECK(names(indexed).size() == 2);
    CHECK(indexed.composite() == before);

    QSignalSpy errors(&indexed, &Editor::errorOccurred);
    CHECK_FALSE(indexed.importLayer(QUrl::fromLocalFile(kCorpus + "/missing.png")));
    CHECK(errors.count() == 1);
}

TEST_CASE("editor: new sprite options")
{
    Editor editor;
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.newSprite({{"width", 20}, {"height", 10}, {"frames", 3}});
    CHECK(editor.canvasWidth() == 20);
    CHECK(editor.canvasHeight() == 10);
    CHECK(editor.frameCount() == 3);
    CHECK(names(editor) == QStringList{"Layer 1"});
    CHECK(editor.composite().pixelColor(0, 0).alpha() == 0);
    CHECK_FALSE(editor.modified());
    CHECK_FALSE(editor.canUndo());

    // Opaque background: a Background layer on every frame, each its own cel.
    editor.newSprite({{"width", 8}, {"height", 8}, {"background", "white"}, {"frames", 2}});
    CHECK(names(editor) == QStringList{"Background", "Layer 1"});
    CHECK(editor.composite().pixelColor(3, 3) == QColor(255, 255, 255));
    CHECK(editor.currentLayer() == 1); // draws on Layer 1, not the background
    const auto& bg = *editor.layerRows()[0];
    CHECK(bg.isBackground());
    CHECK(bg.cel(0) != bg.cel(1));

    editor.newSprite({{"colorMode", "indexed"}, {"background", "black"}});
    CHECK(editor.document().colorMode() == core::ColorMode::Indexed);
    CHECK(editor.composite().pixelColor(0, 0) == QColor(0, 0, 0)); // opaque even on index 0
    editor.newSprite({{"colorMode", "grayscale"}, {"width", 99999}});
    CHECK(editor.document().colorMode() == core::ColorMode::Grayscale);
    CHECK(editor.canvasWidth() == Editor::maxImageSize());
}

TEST_CASE("editor: layer opacity and blend mode")
{
    Editor editor;
    editor.newDocument(4, 4);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(editor.blendModes().size() == 19);
    CHECK(editor.blendModes().at(1) == "Multiply");

    editor.setLayerOpacity(0, 50);
    CHECK(row(editor, 0).value("opacity") == 50);
    CHECK(editor.composite().pixelColor(1, 1).alpha() == 128);
    CHECK(editor.modified());
    editor.setLayerBlend(0, 1);
    CHECK(row(editor, 0).value("blend") == 1);
    editor.setLayerBlend(0, 99); // ignored
    CHECK(row(editor, 0).value("blend") == 1);
}

TEST_CASE("editor: groups, moving layers in and out, duplicating")
{
    Editor editor; // "Layer 1"
    editor.addLayer(); // "Layer 2" above
    editor.setCurrentLayer(0);
    editor.addGroup(); // above Layer 1
    CHECK(names(editor) == QStringList{"Layer 1", "Group 1", "Layer 2"});
    const int group = rowNamed(editor, "Group 1");
    CHECK(row(editor, group).value("kind") == "group");

    // Into the group (zone 2), then the group shows above its child.
    REQUIRE(editor.moveLayer(rowNamed(editor, "Layer 2"), group, 2));
    CHECK(names(editor) == QStringList{"Layer 1", "Layer 2", "Group 1"});
    CHECK(row(editor, rowNamed(editor, "Layer 2")).value("depth") == 1);
    CHECK(editor.currentLayer() == rowNamed(editor, "Layer 2"));

    // Below an open group's row means the top of that group.
    REQUIRE(editor.moveLayer(rowNamed(editor, "Layer 1"), rowNamed(editor, "Group 1"), 1));
    CHECK(names(editor) == QStringList{"Layer 2", "Layer 1", "Group 1"});
    CHECK(row(editor, rowNamed(editor, "Layer 1")).value("depth") == 1);

    // Out again: above the group.
    REQUIRE(editor.moveLayer(rowNamed(editor, "Layer 1"), rowNamed(editor, "Group 1"), 0));
    CHECK(names(editor) == QStringList{"Layer 2", "Group 1", "Layer 1"});
    CHECK(row(editor, rowNamed(editor, "Layer 1")).value("depth") == 0);

    // A group can't go into itself; a no-op move is refused.
    CHECK_FALSE(editor.moveLayer(rowNamed(editor, "Group 1"), rowNamed(editor, "Layer 2"), 0));
    CHECK_FALSE(editor.moveLayer(rowNamed(editor, "Group 1"), rowNamed(editor, "Group 1"), 2));

    editor.undo();
    editor.undo();
    editor.undo();
    CHECK(names(editor) == QStringList{"Layer 1", "Group 1", "Layer 2"});

    // Duplicate: a copy above, its own pixels.
    editor.setCurrentLayer(rowNamed(editor, "Layer 1"));
    editor.setPrimaryColor(QColor(0, 255, 0));
    editor.beginStroke({2, 2}, false);
    editor.endStroke();
    editor.duplicateLayer();
    CHECK(names(editor) == QStringList{"Layer 1", "Layer 1 copy", "Group 1", "Layer 2"});
    const auto& rows = editor.layerRows();
    CHECK(rows[1]->cel(0) != rows[0]->cel(0));
    CHECK(*rows[1]->cel(0)->image == *rows[0]->cel(0)->image);
    editor.undo();
    CHECK(names(editor).size() == 3);
}

TEST_CASE("editor: palette editing, with indexed pixels remapped")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 1}, {"colorMode", "indexed"}});
    // Default palette (PICO-8): 0 black (transparent index), 8 red, 12 blue.
    const QColor red(0xFF, 0x00, 0x4D), blue(0x29, 0xAD, 0xFF);
    editor.setPrimaryColor(red);
    editor.beginStroke({0, 0}, false);
    editor.endStroke();
    editor.setPrimaryColor(blue);
    editor.beginStroke({1, 0}, false);
    editor.endStroke();
    const int size = editor.palette().size();
    auto px = [&](int x) { return editor.composite().pixelColor(x, 0); };

    // Moving entries keeps the picture.
    REQUIRE(editor.movePaletteColor(8, 1));
    CHECK(editor.palette().at(1).value<QColor>() == red);
    CHECK(px(0) == red);
    CHECK(px(1) == blue);
    editor.undo();
    CHECK(editor.palette().at(8).value<QColor>() == red);
    CHECK(px(0) == red);

    // Removing one: its pixels take the nearest remaining color.
    REQUIRE(editor.removePaletteColor(12));
    CHECK(editor.palette().size() == size - 1);
    CHECK(px(0) == red);
    CHECK(px(1).alpha() == 255);
    CHECK(px(1) != blue);
    editor.undo();
    CHECK(px(1) == blue);
    CHECK_FALSE(editor.removePaletteColor(0)); // the transparent index stays

    // Recoloring an entry changes every pixel using it (that's indexed color).
    REQUIRE(editor.setPaletteColor(8, QColor(1, 2, 3)));
    CHECK(px(0) == QColor(1, 2, 3));
    editor.undo();

    // Adding: appended, or the existing index.
    CHECK(editor.addPaletteColor(red) == 8);
    CHECK(editor.addPaletteColor(QColor(9, 9, 9)) == size);
    CHECK(editor.palette().size() == size + 1);

    // RGBA sprites: a plain list, pixels untouched.
    Editor rgba;
    const int n = rgba.palette().size();
    CHECK(rgba.movePaletteColor(0, n - 1));
    CHECK(rgba.palette().last().value<QColor>() == QColor(0, 0, 0));
    CHECK(rgba.removePaletteColor(0));
    CHECK(rgba.palette().size() == n - 1);
}

TEST_CASE("editor: layer and frame property edits are undoable, scrubs merge")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    editor.addFrame();
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(dir.filePath("p.aseprite"))));
    CHECK_FALSE(editor.modified());

    editor.setLayerVisible(0, false);
    editor.setLayerLocked(0, true);
    editor.setLayerName(0, "Ink");
    editor.setLayerBlend(0, 2);
    CHECK(editor.modified());
    CHECK(row(editor, 0).value("name") == "Ink");
    editor.undo();
    CHECK(row(editor, 0).value("blend") == 0);
    editor.undo();
    CHECK(row(editor, 0).value("name") == "Layer 1");
    editor.undo();
    CHECK(row(editor, 0).value("locked") == false);
    editor.undo();
    CHECK(row(editor, 0).value("visible") == true);
    CHECK_FALSE(editor.modified()); // back at the saved state

    // A scrub of opacity is one step.
    for (int v = 95; v >= 40; v -= 5)
        editor.setLayerOpacity(0, v);
    CHECK(row(editor, 0).value("opacity") == 40);
    editor.undo();
    CHECK(row(editor, 0).value("opacity") == 100);
    CHECK_FALSE(editor.modified());

    // Another kind of edit in between keeps them apart.
    editor.setLayerOpacity(0, 80);
    editor.setLayerVisible(0, false);
    editor.setLayerOpacity(0, 60);
    editor.undo();
    CHECK(row(editor, 0).value("opacity") == 80);
    editor.undo();
    editor.undo();
    CHECK(row(editor, 0).value("opacity") == 100);

    // Durations: scroll ticks merge, undo restores.
    editor.selectFrames(0, 1);
    for (int ms = 110; ms <= 200; ms += 10)
        editor.setFramesDuration(ms);
    editor.clearRange();
    CHECK(editor.frameDuration() == 200);
    editor.undo();
    CHECK(editor.frameDuration() == 100);
    editor.setCurrentFrame(1);
    CHECK(editor.frameDuration() == 100);
    CHECK_FALSE(editor.modified());
}

TEST_CASE("editor: sprite size scales the picture and undoes, buffers follow")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}, {"background", "white"}});
    editor.selectAll();
    QSignalSpy sized(&editor, &Editor::canvasSizeChanged);

    editor.resizeSprite(8, 12);
    CHECK(sized.count() == 1);
    CHECK(editor.canvasWidth() == 8);
    CHECK(editor.canvasHeight() == 12);
    CHECK(editor.composite().size() == QSize(8, 12));
    CHECK(editor.composite().pixelColor(7, 11) == QColor(255, 255, 255));
    CHECK_FALSE(editor.hasSelection()); // dropped with the old size
    CHECK(editor.modified());

    editor.undo();
    CHECK(sized.count() == 2);
    CHECK(editor.composite().size() == QSize(4, 4));
    CHECK(editor.selection() == QRect(0, 0, 4, 4)); // back with the size
    CHECK(editor.composite().pixelColor(3, 3) == QColor(255, 255, 255));
    editor.redo();
    CHECK(editor.composite().size() == QSize(8, 12));

    editor.resizeSprite(8, 12); // same size: no undo step
    editor.undo();
    CHECK(editor.canvasWidth() == 4);
    editor.undo(); // Select All
    CHECK_FALSE(editor.hasSelection());
    CHECK_FALSE(editor.canUndo());
}

TEST_CASE("editor: canvas size grows with the secondary color and crops, undoable")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}, {"background", "white"}});
    editor.setSecondaryColor(QColor(0, 0, 255, 100));
    QSignalSpy sized(&editor, &Editor::canvasSizeChanged);

    editor.resizeCanvas(QRect(-2, 0, 6, 5)); // 2 px left, 1 px down
    CHECK(sized.count() == 1);
    CHECK(editor.composite().size() == QSize(6, 5));
    CHECK(editor.composite().pixelColor(0, 0) == QColor(0, 0, 255)); // opaque fill
    CHECK(editor.composite().pixelColor(2, 0) == QColor(255, 255, 255));
    CHECK(editor.composite().pixelColor(2, 4) == QColor(0, 0, 255));

    editor.resizeCanvas(QRect(2, 0, 2, 2)); // crop to the old top-left corner
    CHECK(editor.composite().size() == QSize(2, 2));
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 255, 255));

    editor.undo();
    editor.undo();
    CHECK(editor.composite().size() == QSize(4, 4));
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 255, 255));
    CHECK_FALSE(editor.canUndo());
}

TEST_CASE("editor: trim canvas crops to what any frame shows, undoable")
{
    Editor editor;
    editor.newSprite({{"width", 10}, {"height", 8}, {"frames", 2}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.setBrushSize(1);
    QSignalSpy errors(&editor, &Editor::errorOccurred);

    CHECK_FALSE(editor.trimCanvas()); // nothing drawn
    CHECK(errors.count() == 1);
    CHECK(editor.canvasWidth() == 10);

    click(editor, {2, 3});
    editor.setCurrentFrame(1);
    click(editor, {5, 4}); // another frame widens the content
    CHECK(editor.trimCanvas());
    CHECK(editor.canvasWidth() == 4); // x 2..5
    CHECK(editor.canvasHeight() == 2); // y 3..4
    editor.setCurrentFrame(0);
    CHECK(pixel(editor, 0, 0) == QColor(255, 0, 0));
    // The preview gets the trimmed frames, the current one and the others.
    CHECK(editor.frameImage(0).size() == QSize(4, 2));
    CHECK(editor.frameImage(0).pixelColor(0, 0) == QColor(255, 0, 0));
    CHECK(editor.frameImage(1).pixelColor(3, 1) == QColor(255, 0, 0));
    CHECK_FALSE(editor.trimCanvas()); // already fits

    editor.undo();
    CHECK(editor.canvasWidth() == 10);
    CHECK(editor.canvasHeight() == 8);
}

TEST_CASE("grid: set, undo and redo, scrubbing merges into one step")
{
    Editor editor;
    QSignalSpy spy(&editor, &Editor::gridChanged);
    editor.setGrid(2, 3, 8, 9);
    editor.setGrid(2, 3, 10, 9);
    CHECK(editor.grid() == QRect(2, 3, 10, 9));
    editor.undo();
    CHECK(editor.grid() == QRect(0, 0, 16, 16));
    editor.redo();
    CHECK(editor.grid() == QRect(2, 3, 10, 9));
    editor.setGrid(0, 0, 0, 99999); // sizes are clamped
    CHECK(editor.grid() == QRect(0, 0, 1, Editor::maxImageSize()));
    CHECK(spy.count() >= 3);
}

TEST_CASE("editor: new sprites get the preferred grid size")
{
    Editor editor;
    CHECK(editor.newGridSize() == QSize(16, 16));
    // The untouched startup sprite follows it (it's made before preferences are read).
    editor.setNewGridSize({8, 12});
    CHECK(editor.grid() == QRect(0, 0, 8, 12));
    CHECK(!editor.canUndo());

    editor.newSprite({{"width", 32}, {"height", 32}});
    CHECK(editor.grid() == QRect(0, 0, 8, 12));
    // An edited sprite keeps its own grid.
    editor.setGrid(0, 0, 4, 4);
    editor.setNewGridSize({0, 99999}); // clamped
    CHECK(editor.newGridSize() == QSize(1, Editor::maxImageSize()));
    CHECK(editor.grid() == QRect(0, 0, 4, 4));

    // Preferences > Canvas > Reset.
    editor.resetNewGridSize();
    CHECK(editor.newGridSize() == QSize(16, 16));
    editor.setAutosaveDelay(40);
    editor.resetAutosaveDelay();
    CHECK(editor.autosaveDelay() == 15);
}

TEST_CASE("editor: frameImage renders any frame for the preview")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 3}, {"frames", 2}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();

    const QImage first = editor.frameImage(0);
    REQUIRE(first.size() == QSize(4, 3));
    CHECK(first.pixelColor(1, 1) == QColor(255, 0, 0));
    editor.setCurrentFrame(1); // frame 0 is now rendered on its own
    CHECK(editor.frameImage(0).pixelColor(1, 1) == QColor(255, 0, 0));
    CHECK(editor.frameImage(1).pixelColor(1, 1).alpha() == 0);
    CHECK(editor.frameImage(2).isNull());

    editor.setFrameDuration(250);
    CHECK(editor.frameDurationAt(1) == 250);
    CHECK(editor.frameDurationAt(0) == 100);
}

TEST_CASE("editor: a bigger imported image starts transformed to fit the canvas")
{
    QTemporaryDir dir;
    QImage big(40, 20, QImage::Format_RGBA8888);
    big.fill(QColor(0, 0, 255));
    const QString path = dir.filePath("big.png");
    REQUIRE(big.save(path));

    Editor editor;
    editor.newSprite({{"width", 10}, {"height", 10}});
    REQUIRE(editor.importLayer(QUrl::fromLocalFile(path)));
    REQUIRE(editor.floating());
    CHECK(editor.transformBox() == QRect(0, 2, 10, 5)); // 40 x 20 fitted, centered
    editor.commitFloating();
    CHECK(!editor.floating());
    const QImage shown = editor.composite();
    CHECK(shown.pixelColor(5, 5) == QColor(0, 0, 255));
    CHECK(shown.pixelColor(5, 1).alpha() == 0); // above the fitted image

    // Esc keeps it as imported: the canvas shows its top-left 10 x 10.
    REQUIRE(editor.importLayer(QUrl::fromLocalFile(path)));
    editor.cancelFloating();
    CHECK(editor.composite().pixelColor(5, 1) == QColor(0, 0, 255));

    // Ctrl+T takes the whole cel, off-canvas pixels too.
    editor.startTransform();
    REQUIRE(editor.floating());
    CHECK(editor.transformBox() == QRect(0, 0, 40, 20));
    editor.cancelFloating();
}

TEST_CASE("editor: group and ungroup the selected layers, move several at once")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}});
    editor.addLayer();
    editor.addLayer();
    editor.addLayer(); // Layer 1 .. 4, bottom to top
    const auto names = [&] {
        QStringList out;
        for (const QVariant& l : editor.layers())
            out << l.toMap().value("name").toString();
        return out;
    };
    const QStringList start = names();
    REQUIRE(start.size() == 4);

    // Layers 2 and 3 (rows of a cel range) into a group, where 3 was.
    editor.selectCels(1, 0, 2, 0);
    REQUIRE(editor.groupLayers());
    QStringList grouped = names(); // children come right before their group
    REQUIRE(grouped.size() == 5);
    CHECK(grouped[0] == start[0]);
    CHECK(grouped[1] == start[1]);
    CHECK(grouped[2] == start[2]);
    CHECK(grouped[3].startsWith("Group"));
    CHECK(grouped[4] == start[3]);
    CHECK(editor.layers()[1].toMap().value("depth").toInt() == 1);
    editor.undo();
    CHECK(names() == start); // one step

    editor.redo();
    editor.setCurrentLayer(3); // the group
    REQUIRE(editor.ungroupLayer());
    CHECK(names() == start);
    CHECK(editor.layers()[1].toMap().value("depth").toInt() == 0);
    editor.undo();
    CHECK(names().size() == 5);
    editor.undo();
    CHECK(names() == start);

    // Dragging one of two selected layers moves both, keeping their order:
    // layers 1 and 2 above layer 4.
    editor.selectCels(0, 0, 1, 0);
    REQUIRE(editor.moveLayer(0, 3, 0));
    CHECK(names() == QStringList{start[2], start[3], start[0], start[1]});
    editor.undo();
    CHECK(names() == start);
}

TEST_CASE("editor: layers that aren't next to each other can be picked, grouped, moved and deleted together")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}});
    for (int i = 0; i < 4; ++i)
        editor.addLayer(); // Layer 1 .. 5, bottom to top
    const auto names = [&] {
        QStringList out;
        for (const QVariant& l : editor.layers())
            out << l.toMap().value("name").toString();
        return out;
    };
    const QStringList start = names();
    REQUIRE(start.size() == 5);

    // From layer 1, picking layer 3 and layer 5 (not next to each other).
    editor.setCurrentLayer(0);
    CHECK(editor.layerPickCount() == 0);
    editor.togglePick(2);
    editor.togglePick(4);
    CHECK(editor.layerPickCount() == 3);
    CHECK(editor.currentLayer() == 4); // the last one picked
    CHECK(editor.isPicked(0));
    CHECK_FALSE(editor.isPicked(1));
    CHECK(editor.isPicked(2));
    CHECK(editor.isPicked(4));

    // Taking one out again; the last one can't be taken out.
    editor.togglePick(2);
    CHECK(editor.layerPickCount() == 2);
    CHECK_FALSE(editor.isPicked(2));
    editor.togglePick(2); // back in
    CHECK(editor.layerPickCount() == 3);

    // Group: layers 1, 3 and 5 go in a group above the top one; 2 and 4 stay.
    REQUIRE(editor.groupLayers());
    CHECK(editor.layerPickCount() == 0);
    int groupRow = -1;
    for (int i = 0; i < names().size(); ++i)
        if (names()[i].startsWith("Group"))
            groupRow = i;
    REQUIRE(groupRow >= 0);
    CHECK(names().size() == 6);
    int children = 0;
    for (const QVariant& l : editor.layers())
        children += l.toMap().value("depth").toInt() == 1;
    CHECK(children == 3);
    editor.undo();
    CHECK(names() == start);

    // Delete takes all picked ones at once.
    editor.setCurrentLayer(1);
    editor.togglePick(3);
    CHECK(editor.layerPickCount() == 2);
    editor.removeLayer();
    CHECK(names() == QStringList{start[0], start[2], start[4]});
    editor.undo();
    CHECK(names() == start);

    // Dragging one of the picked layers moves them all, keeping their order: layers 1 and 3 to the top.
    editor.setCurrentLayer(0);
    editor.togglePick(2);
    REQUIRE(editor.moveLayer(2, 4, 0));
    CHECK(names() == QStringList{start[1], start[3], start[4], start[0], start[2]});
    editor.undo();

    // A plain move to another layer, Esc / a canvas click (clearRange) and a Shift block all end the picks.
    editor.setCurrentLayer(0);
    editor.togglePick(2);
    editor.setCurrentLayer(1);
    CHECK(editor.layerPickCount() == 0);
    editor.togglePick(3);
    editor.clearRange();
    CHECK(editor.layerPickCount() == 0);
    editor.setCurrentLayer(0);
    editor.togglePick(2);
    editor.selectCels(0, 0, 1, 0);
    CHECK(editor.layerPickCount() == 0);
    CHECK(editor.rangeType() == Editor::CelRange);
    editor.setCurrentLayer(0);
    editor.togglePick(2);
    editor.setTool(Editor::Pencil);
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(editor.layerPickCount() == 0); // a click on the canvas keeps the current layer only

    // A block taken with Shift can have layers added or taken out with Cmd.
    editor.selectCels(0, 0, 2, 0); // layers 1 .. 3
    editor.togglePick(4);
    CHECK(editor.layerPickCount() == 4);
    editor.togglePick(1); // out of the middle of the block
    CHECK(editor.layerPickCount() == 3);
    CHECK_FALSE(editor.isPicked(1));
    CHECK(editor.isPicked(0));
    CHECK(editor.isPicked(2));
}

TEST_CASE("editor: delete the selected layers together, and a canvas click drops the selection")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}});
    editor.addLayer();
    editor.addLayer();
    editor.addLayer(); // Layer 1 .. 4, bottom to top
    const auto names = [&] {
        QStringList out;
        for (const QVariant& l : editor.layers())
            out << l.toMap().value("name").toString();
        return out;
    };
    const QStringList start = names();
    REQUIRE(start.size() == 4);

    // Layers 2 and 3 go in one step, and the layer below is current.
    editor.selectCels(1, 0, 2, 0);
    editor.removeLayer();
    CHECK(names() == QStringList{start[0], start[3]});
    CHECK(editor.rangeType() == Editor::NoRange);
    CHECK(editor.currentLayer() == 0);
    editor.undo();
    CHECK(names() == start);

    // Every layer selected: one stays (the current one), the sprite is never empty.
    editor.setCurrentLayer(2);
    editor.selectCels(0, 0, 3, 0);
    editor.removeLayer();
    CHECK(names() == QStringList{start[2]});
    editor.undo();
    CHECK(names() == start);

    // A click on the canvas with several layers selected keeps only the current one.
    editor.setCurrentLayer(1);
    editor.selectCels(1, 0, 2, 0);
    editor.setTool(Editor::Pencil);
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(editor.rangeType() == Editor::NoRange);
    CHECK(editor.currentLayer() == 1);

    // One layer's block of frames isn't a multi-layer selection: it stays.
    editor.addFrame();
    editor.selectCels(1, 0, 1, 1);
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(editor.rangeType() == Editor::CelRange);
}

TEST_CASE("editor: Alt picks a color with the gradient tool too")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(10, 200, 30));
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Gradient);
    editor.beginStroke({1, 1}, false, Qt::AltModifier);
    editor.endStroke();
    CHECK(editor.primaryColor() == QColor(10, 200, 30));
}

TEST_CASE("editor: slices show per frame and follow sprite and canvas size")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    REQUIRE(editor.slices().size() == 1);
    QVariantMap hitbox = editor.slices()[0].toMap();
    CHECK(hitbox.value("name").toString() == "hitbox");
    CHECK(hitbox.value("bounds").toRect() == QRect(2, 2, 10, 10));
    CHECK(hitbox.value("center").toRect() == QRect(1, 1, 8, 8));
    CHECK(hitbox.value("pivot").toPoint() == QPoint(5, 5));
    const QRect grid = editor.grid();

    // Twice the size: bounds, center, pivot and the grid scale.
    editor.resizeSprite(editor.canvasWidth() * 2, editor.canvasHeight() * 2);
    hitbox = editor.slices()[0].toMap();
    CHECK(hitbox.value("bounds").toRect() == QRect(4, 4, 20, 20));
    CHECK(hitbox.value("center").toRect() == QRect(2, 2, 16, 16));
    CHECK(hitbox.value("pivot").toPoint() == QPoint(10, 10));
    CHECK(editor.grid() == QRect(grid.x() * 2, grid.y() * 2, grid.width() * 2, grid.height() * 2));
    editor.undo();
    CHECK(editor.slices()[0].toMap().value("bounds").toRect() == QRect(2, 2, 10, 10));
    CHECK(editor.grid() == grid);

    // Canvas size from (1, 1): everything moves up-left by one.
    editor.resizeCanvas(QRect(1, 1, editor.canvasWidth(), editor.canvasHeight()));
    CHECK(editor.slices()[0].toMap().value("bounds").toRect() == QRect(1, 1, 10, 10));
    CHECK(editor.grid().topLeft() == grid.topLeft() - QPoint(1, 1));
    editor.undo();
    CHECK(editor.slices()[0].toMap().value("bounds").toRect() == QRect(2, 2, 10, 10));
}

TEST_CASE("editor: slices are added, moved, renamed, recolored and removed")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    CHECK(editor.currentSlice() == -1);
    CHECK(editor.addSlice(QRect(1, 1, 4, 4)) == 0);
    CHECK(editor.addSlice(QRect(10, 10, 5, 3)) == 1);
    CHECK(editor.currentSlice() == 1);
    CHECK(editor.addSlice(QRect(0, 0, 0, 0)) == -1); // empty: no slice
    QVariantList all = editor.allSlices();
    REQUIRE(all.size() == 2);
    CHECK(all[0].toMap().value("name").toString() == "Slice 1");
    CHECK(all[1].toMap().value("name").toString() == "Slice 2");

    // A drag's steps merge into one undo step.
    editor.setSliceBounds(0, QRect(2, 1, 4, 4));
    editor.setSliceBounds(0, QRect(3, 1, 4, 4));
    CHECK(editor.slices()[0].toMap().value("bounds").toRect() == QRect(3, 1, 4, 4));
    editor.undo();
    CHECK(editor.slices()[0].toMap().value("bounds").toRect() == QRect(1, 1, 4, 4));

    editor.setSliceName(0, "button");
    editor.setSliceColor(0, QColor(255, 0, 0));
    CHECK(editor.allSlices()[0].toMap().value("name").toString() == "button");
    CHECK(editor.allSlices()[0].toMap().value("color").value<QColor>() == QColor(255, 0, 0));
    editor.undo();
    CHECK(editor.allSlices()[0].toMap().value("color").value<QColor>() != QColor(255, 0, 0));

    // Removing the current one: none is current; undo brings it back.
    editor.setCurrentSlice(1);
    editor.removeSlice(1);
    CHECK(editor.allSlices().size() == 1);
    CHECK(editor.currentSlice() == -1);
    editor.undo();
    CHECK(editor.allSlices().size() == 2);
    // A new slice after "Slice 2" is "Slice 3", even with "button" in between.
    editor.addSlice(QRect(0, 0, 2, 2));
    CHECK(editor.allSlices()[2].toMap().value("name").toString() == "Slice 3");
}

TEST_CASE("editor: 9-slice centers, pivots and keys per frame")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}, {"frames", 4}});
    REQUIRE(editor.addSlice(QRect(0, 0, 9, 6)) == 0);
    const auto at = [&](int frame) {
        editor.setCurrentFrame(frame);
        return editor.slices()[0].toMap();
    };

    // 9-slice starts at the middle third; pivot in the middle.
    editor.setSliceNineSlice(0, true);
    CHECK(at(0).value("center").toRect() == QRect(3, 2, 3, 2));
    editor.setSliceCenter(0, QRect(1, 1, 7, 4));
    CHECK(at(0).value("center").toRect() == QRect(1, 1, 7, 4));
    editor.setSliceCenter(0, QRect(0, 0, 99, 99)); // kept inside the bounds
    CHECK(at(0).value("center").toRect() == QRect(0, 0, 9, 6));
    editor.setSliceHasPivot(0, true);
    CHECK(at(0).value("pivot").toPoint() == QPoint(4, 3));
    editor.setSlicePivot(0, QPoint(1, 5));
    CHECK(at(0).value("pivot").toPoint() == QPoint(1, 5));

    // Keys off: an edit at frame 2 changes the one key, for every frame.
    at(2);
    editor.setSliceBounds(0, QRect(5, 5, 9, 6));
    CHECK(at(0).value("bounds").toRect() == QRect(5, 5, 9, 6));
    CHECK(at(0).value("keys").toInt() == 1);

    // Keys on: frame 2 gets its own key; frames before keep the old one.
    editor.setSliceKeys(true);
    at(2);
    editor.setSliceBounds(0, QRect(10, 10, 9, 6));
    CHECK(at(0).value("bounds").toRect() == QRect(5, 5, 9, 6));
    CHECK(at(2).value("bounds").toRect() == QRect(10, 10, 9, 6));
    CHECK(at(3).value("bounds").toRect() == QRect(10, 10, 9, 6));
    CHECK(at(2).value("keyFrame").toInt() == 2);
    CHECK(at(2).value("pivot").toPoint() == QPoint(1, 5)); // copied from the key before
    // Removing frame 2's key: the first one applies again; the first can't go.
    at(2);
    editor.removeSliceKey(0);
    CHECK(at(3).value("bounds").toRect() == QRect(5, 5, 9, 6));
    at(0);
    editor.removeSliceKey(0);
    CHECK(at(0).value("keys").toInt() == 1);
}

TEST_CASE("editor: the current tilemap layer's tileset, and new tiles")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/tilemap.aseprite"));
    int tilemap = -1;
    for (int i = 0; i < int(editor.layers().size()); ++i)
        if (editor.layers()[i].toMap().value("kind").toString() == "tilemap")
            tilemap = i;
    REQUIRE(tilemap >= 0);
    editor.setCurrentLayer(tilemap == 0 ? 1 : 0);
    CHECK_FALSE(editor.tilemapLayer());
    CHECK(editor.tileCount() == 0);

    editor.setCurrentLayer(tilemap);
    CHECK(editor.tilemapLayer());
    CHECK(editor.tilesetName() == "set");
    CHECK(editor.tileSize() == QSize(4, 4));
    CHECK(editor.tileCount() == 4);
    CHECK(editor.tileImage(1).pixelColor(0, 0) == QColor(80, 40, 175));
    CHECK(editor.tileImage(0).pixelColor(0, 0).alpha() == 0); // the empty tile
    CHECK(editor.tileImage(9).isNull());

    const int revision = editor.tilesetRevision();
    CHECK(editor.addTile() == 4);
    CHECK(editor.tileCount() == 5);
    CHECK(editor.currentTile() == 4);
    CHECK(editor.tilesetRevision() > revision);
    editor.undo();
    CHECK(editor.tileCount() == 4);
    CHECK(editor.currentTile() == 3); // kept in range
}

TEST_CASE("editor: groups and tilemap layers have thumbnails of what they show")
{
    // A tilemap layer: its tiles, drawn alone.
    Editor tiles;
    REQUIRE(tiles.openPath(kCorpus + "/tilemap.aseprite"));
    const int tilemap = rowNamed(tiles, "tiles");
    REQUIRE(tilemap >= 0);
    const QImage tilesThumb = tiles.layerImage(tilemap, 48);
    REQUIRE_FALSE(tilesThumb.isNull());
    bool anyPixel = false;
    for (int y = 0; y < tilesThumb.height() && !anyPixel; ++y)
        for (int x = 0; x < tilesThumb.width(); ++x)
            anyPixel = anyPixel || tilesThumb.pixelColor(x, y).alpha() > 0;
    CHECK(anyPixel);

    // A group: its children together.
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    const int group = rowNamed(editor, "group");
    REQUIRE(group >= 0);
    const QImage groupThumb = editor.layerImage(group, 48);
    CHECK_FALSE(groupThumb.isNull());
    // An image layer still has its own, and a layer with no pixels on the frame has none.
    CHECK_FALSE(editor.layerImage(rowNamed(editor, "overlay child"), 48).isNull());
    editor.addLayer();
    CHECK(editor.layerImage(editor.currentLayer(), 48).isNull());
}

TEST_CASE("editor: a new sprite can start with a background of any color")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}, {"background", "color"}, {"backgroundColor", QColor(10, 20, 30)}});
    CHECK(editor.composite().pixelColor(0, 0) == QColor(10, 20, 30));
    CHECK(editor.composite().pixelColor(3, 3) == QColor(10, 20, 30));
    CHECK(editor.layers().size() == 2); // the Background layer and Layer 1
    // Always opaque, whatever alpha the color had.
    editor.newSprite({{"width", 2}, {"height", 2}, {"background", "color"}, {"backgroundColor", QColor(200, 100, 50, 40)}});
    CHECK(editor.composite().pixelColor(1, 1) == QColor(200, 100, 50));
    // The built-in ones are unchanged.
    editor.newSprite({{"width", 2}, {"height", 2}, {"background", "white"}});
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 255, 255));
}

TEST_CASE("editor: layers are copied in one document and pasted in another")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}, {"frames", 3}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    editor.setCurrentFrame(2);
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({3, 3}, false);
    editor.endStroke();
    editor.setCurrentFrame(0);
    const QString name = editor.layers()[editor.currentLayer()].toMap().value("name").toString();

    editor.newSprite({{"width", 4}, {"height", 4}}); // the second tab, one frame
    REQUIRE(editor.documents().size() == 2);
    editor.setCurrentDocument(0);
    editor.setCurrentLayer(0);
    const int before = int(editor.layers().size());

    // Nothing to paste until something is copied.
    CHECK_FALSE(editor.canPasteLayers());
    CHECK_FALSE(editor.pasteLayers());
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    QSignalSpy clipboard(&editor, &Editor::layerClipboardChanged);
    const bool sourceModified = editor.isDocumentModified(0);
    REQUIRE(editor.copyLayers());
    CHECK(editor.canPasteLayers());
    CHECK(clipboard.count() == 1);
    CHECK(int(editor.layers().size()) == before); // copying changes nothing
    CHECK(editor.isDocumentModified(0) == sourceModified);

    editor.setCurrentDocument(1);
    REQUIRE(editor.pasteLayers());
    CHECK(int(editor.layers().size()) == 2); // its own layer and the pasted one
    CHECK(editor.layers()[editor.currentLayer()].toMap().value("name").toString() == name);
    CHECK(editor.composite().pixelColor(1, 1) == QColor(255, 0, 0));
    // The target has one frame: the third frame's pixel was left out. One undo step.
    editor.undo();
    CHECK(int(editor.layers().size()) == 1);
    editor.redo();
    CHECK(int(editor.layers().size()) == 2);
    // The same copy pastes again, here too, and stays after the source is gone.
    REQUIRE(editor.pasteLayers());
    CHECK(int(editor.layers().size()) == 3);
    editor.setCurrentDocument(0);
    CHECK(int(editor.layers().size()) == before);
    CHECK(errors.count() == 0);

    // A group goes with its children.
    editor.addLayer();
    editor.addLayer();
    editor.selectCels(1, 0, 2, 0);
    REQUIRE(editor.groupLayers());
    editor.setCurrentLayer(editor.currentLayer());
    REQUIRE(editor.copyLayers());
    editor.setCurrentDocument(1);
    const int layersThere = int(editor.layers().size());
    REQUIRE(editor.pasteLayers());
    int depth1 = 0;
    for (const QVariant& l : editor.layers())
        depth1 += l.toMap().value("depth").toInt() == 1;
    CHECK(depth1 == 2); // the group's two layers came along
    CHECK(int(editor.layers().size()) == layersThere + 3); // the group and its two
}

TEST_CASE("editor: tilemap layers can't be copied")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/tilemap.aseprite"));
    editor.setCurrentLayer(rowNamed(editor, "tiles"));
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    CHECK_FALSE(editor.copyLayers());
    CHECK(errors.count() == 1);
    CHECK_FALSE(editor.canPasteLayers());
}

TEST_CASE("editor: a GIF imported as a layer brings all its frames")
{
    Editor editor;
    editor.newSprite({{"width", 24}, {"height", 16}});
    REQUIRE(editor.frameCount() == 1);
    const int layers = int(editor.layers().size());
    REQUIRE(editor.importLayer(QUrl::fromLocalFile(kCorpus + "/anim.gif")));
    CHECK(editor.frameCount() == 3); // the sprite grew to the GIF's three frames
    CHECK(int(editor.layers().size()) == layers + 1);
    CHECK_FALSE(editor.floating()); // several frames: no transform
    const int imported = editor.currentLayer();
    for (int f = 0; f < 3; ++f) {
        editor.setCurrentFrame(f);
        CAPTURE(f);
        CHECK_FALSE(editor.layerImage(imported, 48).isNull()); // a cel on every frame
    }
    editor.undo(); // one step: the layer and the frames
    CHECK(editor.frameCount() == 1);
    CHECK(int(editor.layers().size()) == layers);
    editor.redo();
    CHECK(editor.frameCount() == 3);

    // On a longer sprite it starts at the current frame and adds only the frames it needs.
    Editor longer;
    longer.newSprite({{"width", 24}, {"height", 16}, {"frames", 2}});
    longer.setCurrentFrame(1);
    REQUIRE(longer.importLayer(QUrl::fromLocalFile(kCorpus + "/anim.gif")));
    CHECK(longer.frameCount() == 4); // frames 1..3 are the GIF's; one frame was there
    longer.setCurrentFrame(0);
    CHECK(longer.layerImage(longer.currentLayer(), 48).isNull()); // nothing before where it starts
    longer.setCurrentFrame(3);
    CHECK_FALSE(longer.layerImage(longer.currentLayer(), 48).isNull());
}

TEST_CASE("editor: tile mode places, erases, flips and picks tiles")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/tilemap.aseprite"));
    editor.setCurrentLayer(rowNamed(editor, "tiles"));
    editor.setEditTiles(true);
    REQUIRE(editor.tilesActive());
    const QColor tile2 = editor.tileImage(2).pixelColor(0, 0);
    const QColor tile3 = editor.tileImage(3).pixelColor(0, 0);
    const auto shown = [&](int x, int y) { return editor.composite().pixelColor(x, y); };
    CHECK(editor.tileCellAt({5, 6}) == QRect(4, 4, 4, 4));

    // A drag across the top row places tile 2 in each cell it passes.
    editor.setTool(Editor::Pencil);
    editor.setCurrentTile(2);
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({15, 0}, {});
    editor.endStroke();
    for (int x = 0; x < 16; x += 4)
        CHECK(shown(x, 0) == tile2);
    editor.undo(); // one step
    CHECK(shown(0, 0).alpha() == 0); // cell (0, 0) held tile 0, the empty one

    // The eraser clears; the eyedropper picks the tile and its flips.
    editor.setTool(Editor::Eraser);
    editor.beginStroke({4, 0}, false, {});
    editor.endStroke();
    CHECK(shown(4, 0).alpha() == 0);
    editor.setTool(Editor::Eyedropper);
    editor.beginStroke({8, 0}, false, {}); // (x + y) % 4 = 2 there
    editor.endStroke();
    CHECK(editor.currentTile() == 2);

    // Flipped: tile 3 flipped horizontally shows its last column first.
    editor.setTool(Editor::Pencil);
    editor.setCurrentTile(3);
    editor.setTileFlips(4);
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    CHECK(shown(0, 0) == tile3); // a flat-color tile looks the same flipped
    editor.setTool(Editor::Eyedropper);
    editor.setTileFlips(0);
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    CHECK(editor.tileFlips() == 4);

    // Painting past the map grows it (whole cells).
    editor.setTool(Editor::Pencil);
    editor.resizeCanvas(QRect(0, 0, 24, 16));
    editor.beginStroke({20, 2}, false, {});
    editor.endStroke();
    CHECK(shown(20, 2) == tile3);
}

TEST_CASE("editor: tile use counts follow the cells as they are placed and erased")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/tilemap.aseprite"));
    editor.setCurrentLayer(rowNamed(editor, "tiles"));
    editor.setEditTiles(true);
    const int start = editor.tileUses(2);
    REQUIRE(start >= 1);
    CHECK(editor.tileUseCounts()[2].toInt() == start);

    // Clearing every cell that shows tile 2 takes it to 0: unused, however it began.
    editor.setTool(Editor::Eraser);
    for (int y = 0; y < 16; y += 4)
        for (int x = 0; x < 16; x += 4) {
            editor.beginStroke({x, y}, false, {});
            editor.endStroke();
        }
    CHECK(editor.tileUses(2) == 0);
    CHECK(editor.tileUseCounts()[2].toInt() == 0);
    editor.undo();
    editor.undo();
    editor.undo();
    editor.undo();
    editor.undo();
    editor.undo();
    editor.undo();
    editor.undo();
    CHECK(editor.tileUseCounts()[2].toInt() > 0);
}

TEST_CASE("editor: pixel mode on a tilemap edits tiles (Manual, Auto, Stack)")
{
    // The corpus map: cell (x, y) holds tile (x + y) % 4, so tile 1 is at
    // (1, 0), (0, 1), (3, 2), (2, 3), and (0, 0) is empty.
    const auto open = [](Editor& editor, int mode) {
        REQUIRE(editor.openPath(kCorpus + "/tilemap.aseprite"));
        editor.setCurrentLayer(rowNamed(editor, "tiles"));
        editor.setEditTiles(false);
        editor.setTileEditMode(mode);
        editor.setTool(Editor::Pencil);
        editor.setBrushSize(1);
        editor.setPrimaryColor(QColor(0, 255, 0));
    };
    const QColor green(0, 255, 0);
    const auto dot = [](Editor& editor, QPoint p) {
        editor.beginStroke(p, false, {});
        editor.endStroke();
    };

    // Auto: a pixel on tile 1 changes tile 1, so every cell with it shows it.
    {
        Editor editor;
        open(editor, 1);
        dot(editor, {4, 0}); // cell (1, 0), pixel (0, 0) of tile 1
        CHECK(editor.composite().pixelColor(4, 0) == green);
        CHECK(editor.composite().pixelColor(0, 4) == green);  // cell (0, 1): tile 1 too
        CHECK(editor.composite().pixelColor(8, 0) != green);  // cell (2, 0): tile 2
        CHECK(editor.tileCount() == 4);
        // On the empty cell (0, 0): a new tile.
        dot(editor, {1, 1});
        CHECK(editor.tileCount() == 5);
        CHECK(editor.composite().pixelColor(1, 1) == green);
        editor.undo();
        CHECK(editor.tileCount() == 4);
        editor.undo();
        CHECK(editor.composite().pixelColor(0, 4) != green); // one step each
    }
    // Manual: tiles change, the empty cell stays empty.
    {
        Editor editor;
        open(editor, 0);
        dot(editor, {1, 1});
        CHECK(editor.composite().pixelColor(1, 1).alpha() == 0);
        CHECK(editor.tileCount() == 4);
        CHECK_FALSE(editor.canUndo());
        dot(editor, {4, 0});
        CHECK(editor.composite().pixelColor(0, 4) == green);
    }
    // Stack: the painted cell gets a tile of its own; others keep tile 1.
    {
        Editor editor;
        open(editor, 2);
        dot(editor, {4, 0});
        CHECK(editor.tileCount() == 5);
        CHECK(editor.composite().pixelColor(4, 0) == green);
        CHECK(editor.composite().pixelColor(0, 4) != green);
    }
    // A line dragged over the empty cell and back leaves no tile behind.
    {
        Editor editor;
        open(editor, 1);
        editor.setTool(Editor::Line);
        editor.beginStroke({5, 1}, false, {}); // in cell (1, 0): tile 1
        editor.moveStroke({1, 1}, {});         // across the empty cell (0, 0): a new tile there
        CHECK(editor.tileCount() == 5);
        editor.moveStroke({6, 1}, {});         // back inside cell (1, 0)
        editor.endStroke();
        CHECK(editor.tileCount() == 4);        // the new tile went with it
        CHECK(editor.composite().pixelColor(1, 1).alpha() == 0);
        CHECK(editor.composite().pixelColor(5, 1) == green);
    }
}

TEST_CASE("editor: new tilemap layers, and layers to tilemaps and back")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 4}});
    editor.setGrid(0, 0, 4, 4);
    // Two identical red 4 x 4 blocks side by side.
    editor.setTool(Editor::Rectangle);
    editor.setFillShapes(true);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({7, 3}, {});
    editor.endStroke();
    const QImage before = editor.composite().copy();

    // To a tilemap: one shared tile for both cells (plus the empty one).
    REQUIRE(editor.convertToTilemap({}));
    CHECK(editor.tilemapLayer());
    CHECK(editor.tileCount() == 2);
    CHECK(editor.tileSize() == QSize(4, 4));
    CHECK(editor.composite() == before); // looks the same
    // And back: pixels again, still the same picture.
    REQUIRE(editor.convertToImageLayer());
    CHECK_FALSE(editor.tilemapLayer());
    CHECK(editor.composite() == before);
    editor.undo();
    CHECK(editor.tilemapLayer());
    editor.undo();
    CHECK_FALSE(editor.tilemapLayer());
    CHECK(editor.composite() == before);

    // A new tilemap layer: empty, with a tileset of grid-sized tiles.
    const int layers = int(editor.layers().size());
    REQUIRE(editor.addTilemapLayer({}));
    CHECK(int(editor.layers().size()) == layers + 1);
    CHECK(editor.tilemapLayer());
    CHECK(editor.tileCount() == 1);
    CHECK(editor.tileSize() == QSize(4, 4));
    editor.undo();
    CHECK(int(editor.layers().size()) == layers);

    // Sprite size scales the tiles with the cels.
    REQUIRE(editor.convertToTilemap({}));
    editor.resizeSprite(16, 8);
    CHECK(editor.tileSize() == QSize(8, 8));
    CHECK(editor.composite().pixelColor(15, 7) == QColor(255, 0, 0));
}

TEST_CASE("editor: tilemap layers pick a new or an existing tileset")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    REQUIRE(editor.addTilemapLayer({{"name", "Ground"}, {"tileWidth", 8}, {"tileHeight", 4}, {"baseIndex", 0}}));
    QVariantList sets = editor.tilesetList();
    REQUIRE(sets.size() == 1);
    CHECK(sets[0].toMap()["name"] == "Ground");
    CHECK(sets[0].toMap()["tileWidth"] == 8);
    CHECK(sets[0].toMap()["tileHeight"] == 4);
    CHECK(sets[0].toMap()["baseIndex"] == 0);
    CHECK(editor.tileSize() == QSize(8, 4));
    editor.addTile();
    CHECK(editor.tilesetList()[0].toMap()["tiles"] == 1);

    // A second layer on the same tileset: no new tileset, the tiles are shared.
    REQUIRE(editor.addTilemapLayer({{"tileset", 0}}));
    CHECK(editor.tilesetList().size() == 1);
    CHECK(editor.tileCount() == 2);
    CHECK(editor.tileSize() == QSize(8, 4));
    editor.undo();
    CHECK(editor.tilesetList().size() == 1);

    // Converting onto an existing tileset reuses its identical tiles.
    editor.setCurrentLayer(0); // Layer 1
    editor.setGrid(0, 0, 8, 4);
    REQUIRE(editor.convertToTilemap({{"tileset", 0}}));
    CHECK(editor.tilesetList().size() == 1);
    CHECK(editor.tileCount() == 2); // the empty layer adds nothing
}

TEST_CASE("editor: a second tilemap layer on a painted tileset sees its tiles")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.setGrid(0, 0, 8, 8);
    REQUIRE(editor.addTilemapLayer({{"name", "Set"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    editor.setEditTiles(false);
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    editor.beginStroke({9, 1}, false, {});
    editor.endStroke();
    INFO(editor.tilesetList());
    CHECK(editor.tileCount() == 2);
    REQUIRE(editor.addTilemapLayer({{"tileset", 0}}));
    CHECK(editor.tileCount() == 2);
    CHECK_FALSE(editor.tileImage(1).isNull());
}

TEST_CASE("editor: the sprite size preview is the frame scaled, cut to the thumbnail size")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 2}, {"background", "white"}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    click(editor, {0, 0});

    const auto decode = [&](int w, int h, int maxSide) {
        const QString url = editor.spriteSizePreview(w, h, maxSide);
        REQUIRE(url.startsWith("data:image/png;base64,"));
        QImage image;
        REQUIRE(image.loadFromData(QByteArray::fromBase64(url.mid(22).toLatin1()), "PNG"));
        return image;
    };
    const QImage twice = decode(8, 4, 240);
    CHECK(twice.size() == QSize(8, 4));
    CHECK(twice.pixelColor(1, 1) == QColor(255, 0, 0)); // the red pixel became 2 x 2
    CHECK(twice.pixelColor(2, 0) == QColor(255, 255, 255));
    CHECK(decode(400, 200, 100).size() == QSize(100, 50)); // cut, keeping the shape
    CHECK(editor.canvasWidth() == 4); // nothing changed
}

TEST_CASE("editor: a layer's thumbnail is its cel on the current frame, fitted, empty for groups and empty cels")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 4}, {"frames", 2}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    click(editor, {1, 1});
    const int layer = editor.currentLayer();

    const QImage whole = editor.layerImage(layer, 64); // smaller than the limit: actual pixels
    REQUIRE(whole.size() == QSize(8, 4));
    CHECK(whole.pixelColor(1, 1) == QColor(255, 0, 0));
    CHECK(whole.pixelColor(0, 0).alpha() == 0);
    const QImage fitted = editor.layerImage(layer, 4); // 8 x 4 into 4: 4 x 2
    CHECK(fitted.size() == QSize(4, 2));
    CHECK(fitted.pixelColor(0, 0).alpha() + fitted.pixelColor(1, 0).alpha() + fitted.pixelColor(0, 1).alpha()
              + fitted.pixelColor(1, 1).alpha() > 0); // the red pixel shows in one

    editor.setCurrentFrame(1); // no cel there
    CHECK(editor.layerImage(layer, 64).isNull());
    editor.setCurrentFrame(0);
    editor.addGroup();
    CHECK(editor.layerImage(editor.currentLayer(), 64).isNull()); // a group
    CHECK(editor.layerImage(-1, 64).isNull());
}

TEST_CASE("live layer: a particle layer renders from its recipe, can't be painted on, and the emitter moves")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.addFrame();
    editor.setPrimaryColor(QColor(255, 0, 0));
    const int layersBefore = int(editor.layers().size());
    editor.addParticleLayer();
    REQUIRE(int(editor.layers().size()) == layersBefore + 1);
    CHECK(editor.liveLayer());
    CHECK(editor.layers()[editor.currentLayer()].toMap().value("kind").toString() == "particles");
    const QPointF start = editor.liveEmitter();
    CHECK(start.x() == 16.5);

    // Frame 0 has the particles born there at the emitter; they can't be painted over.
    CHECK(editor.composite().pixelColor(16, 22).alpha() == 255);
    editor.setTool(Editor::Pencil);
    editor.beginStroke({3, 3}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(3, 3).alpha() == 0);
    editor.clearLayer(); // nor cleared
    CHECK(editor.composite().pixelColor(16, 22).alpha() == 255);

    // The move tool drags the emitter, wherever; one undo step puts it back.
    editor.setTool(Editor::Move);
    editor.beginStroke({5, 5}, false, {});
    editor.moveStroke({7, 6}, {});
    editor.moveStroke({9, 8}, {});
    editor.endStroke();
    CHECK(editor.liveEmitter() == QPointF(start.x() + 4, start.y() + 3));
    CHECK(editor.composite().pixelColor(20, 25).alpha() == 255);
    CHECK(editor.composite().pixelColor(16, 22).alpha() == 0);
    editor.undo();
    CHECK(editor.liveEmitter() == start);
    CHECK(editor.composite().pixelColor(16, 22).alpha() == 255);
    editor.redo();
    CHECK(editor.liveEmitter() == QPointF(start.x() + 4, start.y() + 3));

    // A frame added or removed renders it again for the new frame count.
    const int frames = editor.frameCount();
    editor.addFrame();
    CHECK(editor.frameCount() == frames + 1);
    editor.undo();

    // Ordinary layers are unaffected.
    editor.setCurrentLayer(0);
    CHECK_FALSE(editor.liveLayer());
}

TEST_CASE("live layer: the settings panel's bridge edits the recipe, clamped, as undoable steps")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    CHECK(editor.particleSettings().isEmpty()); // not live yet
    editor.setParticleField("count", 10, false);
    editor.addParticleLayer();
    const QVariantMap before = editor.particleSettings();
    CHECK(before.value("count").toInt() == 40);

    editor.setParticleField("count", 99999, false);
    CHECK(editor.particleSettings().value("count").toInt() == 5000);
    editor.setParticleField("lifetime", 20, false);
    CHECK(editor.particleRecipe().lifetime == 20);
    editor.setParticleField("nonsense", 1, false);

    // Pairs stay in order, the field just changed wins.
    editor.setParticleField("startFrame", 30, false);
    CHECK(editor.particleRecipe().endFrame == 30);
    editor.setParticleField("speed", 9999, false);
    CHECK(editor.particleRecipe().speed == 200);

    // A drag of one number is one undo step.
    const int steps = editor.particleRecipe().lifetime;
    editor.setParticleField("spread", 10, true);
    editor.setParticleField("spread", 20, true);
    editor.setParticleField("spread", 30, true);
    CHECK(editor.particleRecipe().spread == 30);
    editor.undo();
    CHECK(editor.particleRecipe().spread == 40);
    CHECK(editor.particleRecipe().lifetime == steps);

    const uint32_t seed = editor.particleRecipe().seed;
    editor.reseedParticles();
    CHECK(editor.particleRecipe().seed != seed);
    editor.setParticleColors({QColor(1, 2, 3), QColor(4, 5, 6)});
    CHECK(editor.particleRecipe().colors.size() == 2);
    editor.setParticleColors({}); // never empty
    CHECK(editor.particleRecipe().colors.size() == 2);
}

TEST_CASE("live layer: reset puts named fields back to the built-in values in one step")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addParticleLayer();
    const core::ParticleRecipe base;
    CHECK(editor.particleDefaults().value("lifetime").toInt() == base.lifetime);

    editor.setParticleField("lifetime", 30, false);
    editor.setParticleField("spread", 90, false);
    editor.setParticleColors({QColor(1, 2, 3), QColor(4, 5, 6)});
    editor.resetParticleFields({"lifetime", "colors"});
    CHECK(editor.particleRecipe().lifetime == base.lifetime);
    CHECK(editor.particleRecipe().colors == base.colors);
    CHECK(editor.particleRecipe().spread == 90); // not named
    editor.undo();
    CHECK(editor.particleRecipe().lifetime == 30);
    CHECK(editor.particleRecipe().colors.size() == 2);
}

TEST_CASE("live layer: it is still live after saving and opening the file")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("fire.aseprite");
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.addFrame();
    editor.addParticleLayer();
    editor.setParticleField("lifetime", 7, false);
    editor.setLiveEmitter(QPointF(10.5, 12.5));
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));

    Editor reopened;
    REQUIRE(reopened.openFile(QUrl::fromLocalFile(path)));
    bool found = false;
    for (int i = 0; i < int(reopened.layers().size()); ++i) {
        reopened.setCurrentLayer(i);
        if (reopened.liveLayer()) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
    CHECK(reopened.particleRecipe().lifetime == 7);
    CHECK(reopened.liveEmitter() == QPointF(10.5, 12.5));
    CHECK(reopened.layers()[reopened.currentLayer()].toMap().value("kind").toString() == "particles");
}

TEST_CASE("live layer: baking turns it into an ordinary layer, or into a copy beside the live one")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.addParticleLayer();
    const int liveIndex = editor.currentLayer();
    const QImage shown = editor.composite();
    REQUIRE(shown.pixelColor(16, 22).alpha() == 255);

    // To a new layer: the live one stays, the copy is ordinary, can be painted on, and is current.
    const int layers = int(editor.layers().size());
    editor.bakeToNewLayer();
    REQUIRE(int(editor.layers().size()) == layers + 1);
    CHECK_FALSE(editor.liveLayer());
    CHECK(editor.layers()[editor.currentLayer()].toMap().value("kind").toString() == "image");
    editor.setTool(Editor::Pencil);
    editor.beginStroke({3, 3}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(3, 3).alpha() == 255);
    editor.undo();
    editor.undo(); // the paint, then the new layer
    CHECK(int(editor.layers().size()) == layers);
    editor.setCurrentLayer(liveIndex);
    REQUIRE(editor.liveLayer());

    // In place: same pictures, no recipe, undoable.
    editor.bakeLayer();
    CHECK_FALSE(editor.liveLayer());
    CHECK(editor.layers()[editor.currentLayer()].toMap().value("kind").toString() == "image");
    CHECK(editor.composite() == shown);
    editor.undo();
    CHECK(editor.liveLayer());

    // A baked layer does not come back to life from a file.
    editor.redo();
    const QString path = dir.filePath("baked.aseprite");
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    Editor reopened;
    REQUIRE(reopened.openFile(QUrl::fromLocalFile(path)));
    for (int i = 0; i < int(reopened.layers().size()); ++i) {
        reopened.setCurrentLayer(i);
        CHECK_FALSE(reopened.liveLayer());
    }
}

TEST_CASE("live layer: moving or pasting cels leaves it alone, moving whole frames keeps its animation")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    for (int i = 0; i < 5; ++i)
        editor.addFrame();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.setTool(Editor::Pencil);
    editor.setCurrentFrame(0);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    const int plain = editor.currentLayer();
    editor.addParticleLayer();
    const int live = editor.currentLayer();
    REQUIRE(live != plain);
    const auto shown = [&](int frame) {
        editor.setCurrentFrame(frame);
        return editor.composite();
    };
    std::vector<QImage> before;
    for (int f = 0; f < editor.frameCount(); ++f)
        before.push_back(shown(f));

    // Both layers selected on frame 0 and moved one frame on: the plain cel moves, the live one stays.
    editor.selectCels(plain, 0, live, 0);
    REQUIRE(editor.moveRange(0, 1, false));
    editor.setCurrentLayer(plain);
    editor.setCurrentFrame(1);
    CHECK(editor.composite().pixelColor(2, 2) == QColor(0, 0, 255));
    editor.setCurrentFrame(0);
    CHECK(editor.composite().pixelColor(2, 2).alpha() == 0);
    for (int f = 0; f < editor.frameCount(); ++f) {
        // Without the blue pixel, every frame shows what the effect rendered before.
        QImage now = shown(f);
        now.setPixelColor(2, 2, before[size_t(f)].pixelColor(2, 2));
        CHECK(now == before[size_t(f)]);
    }
    editor.undo();

    // Moving a plain cel onto the live row is refused; so is pasting into it.
    editor.selectCels(plain, 0, plain, 0);
    CHECK_FALSE(editor.moveRange(1, 0, false));
    editor.setCurrentLayer(plain);
    editor.setCurrentFrame(0);
    editor.selectCels(plain, 0, plain, 0);
    editor.copyTimeline();
    editor.setCurrentLayer(live);
    editor.setCurrentFrame(3);
    editor.pasteTimeline();
    for (int f = 0; f < editor.frameCount(); ++f)
        CHECK(shown(f) == before[size_t(f)]);

    // Whole frames move: the live layer is drawn again, so the effect stays as it was.
    editor.selectFrames(0, 0);
    REQUIRE(editor.moveRange(0, 2, false));
    for (int f = 0; f < editor.frameCount(); ++f) {
        QImage now = shown(f);
        now.setPixelColor(2, 2, before[size_t(f)].pixelColor(2, 2));
        CHECK(now == before[size_t(f)]);
    }
}

TEST_CASE("live layer: only the eyedropper, hand and move can be picked, and the old tool comes back")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setTool(Editor::Line);
    const int plain = editor.currentLayer();
    editor.addParticleLayer();
    REQUIRE(editor.liveLayer());

    // The line tool is replaced with Move, and nothing else can be picked.
    CHECK(editor.tool() == Editor::Move);
    for (const Editor::Tool tool : {Editor::Pencil, Editor::Eraser, Editor::Fill, Editor::Line, Editor::Text,
                                    Editor::Select, Editor::Gradient}) {
        editor.setTool(tool);
        CHECK(editor.tool() == Editor::Move);
        CHECK_FALSE(Editor::toolUsableOnLive(tool));
    }
    editor.setTool(Editor::Eyedropper);
    CHECK(editor.tool() == Editor::Eyedropper);
    editor.setTool(Editor::Hand);
    CHECK(editor.tool() == Editor::Hand);

    // Back on an ordinary layer after choosing a usable tool: that choice stays.
    editor.setCurrentLayer(plain);
    CHECK(editor.tool() == Editor::Hand);
    editor.setTool(Editor::Pencil);
    CHECK(editor.tool() == Editor::Pencil);

    // Untouched while live: the old tool returns.
    editor.setTool(Editor::Line);
    editor.setCurrentLayer(editor.currentLayer() + 1);
    REQUIRE(editor.liveLayer());
    CHECK(editor.tool() == Editor::Move);
    editor.setCurrentLayer(plain);
    CHECK(editor.tool() == Editor::Line);

    // Baking makes the layer ordinary: tools work again.
    editor.setCurrentLayer(editor.currentLayer() + 1);
    CHECK(editor.tool() == Editor::Move);
    editor.bakeLayer();
    CHECK(editor.tool() == Editor::Line);
    editor.setTool(Editor::Pencil);
    CHECK(editor.tool() == Editor::Pencil);
}

TEST_CASE("live layer: the arrow keys' nudge moves the emitter as one undo step")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addParticleLayer();
    const QPointF start = editor.liveEmitter();
    CHECK(editor.nudge(1, 0, false));
    CHECK(editor.nudge(1, 0, false));
    CHECK(editor.nudge(0, -10, false));
    CHECK(editor.liveEmitter() == QPointF(start.x() + 2, start.y() - 10));
    editor.undo();
    CHECK(editor.liveEmitter() == start);
}

TEST_CASE("live layer: a file whose pixels were changed elsewhere asks, and either answer can be undone")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("edited.aseprite");
    {
        Editor editor;
        editor.newSprite({{"width", 32}, {"height", 32}});
        editor.addFrame();
        editor.addParticleLayer();
        REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    }
    // Another program paints a pixel on the layer.
    {
        std::string error;
        auto doc = formats::loadAseprite(path.toStdString(), &error);
        REQUIRE(doc);
        core::Layer* layer = nullptr;
        doc->forEachLayer([&](core::Layer& l) { if (l.isLive()) layer = &l; });
        REQUIRE(layer);
        REQUIRE(layer->cels[0]);
        auto image = std::make_shared<core::Image>(*layer->cels[0]->image);
        image->setPixel(0, 0, 0xff0000ffu);
        layer->cels[0] = std::make_shared<core::CelData>(*layer->cels[0]);
        layer->cels[0]->image = image;
        layer->particles.reset(); // as if the other program didn't know about it
        REQUIRE(formats::saveAseprite(*doc, path.toStdString(), &error));
    }

    for (const bool render : {false, true}) {
        Editor editor;
        QSignalSpy spy(&editor, &Editor::staleLiveFound);
        REQUIRE(editor.openFile(QUrl::fromLocalFile(path)));
        QTest::qWait(10);
        CHECK(spy.count() == 1);
        const QVariantList stale = editor.staleLiveLayers();
        REQUIRE(stale.size() == 1);
        CHECK(stale[0].toMap().value("name").toString().startsWith("Particles"));
        CHECK_FALSE(editor.modified()); // asking changes nothing

        const int index = stale[0].toMap().value("index").toInt();
        editor.resolveStaleLive(index, render);
        CHECK(editor.staleLiveLayers().isEmpty());
        CHECK(editor.modified());
        bool anyLive = false;
        for (int i = 0; i < int(editor.layers().size()); ++i) {
            editor.setCurrentLayer(i);
            anyLive = anyLive || editor.liveLayer();
        }
        CHECK(anyLive == render);
        editor.undo();
        CHECK(editor.staleLiveLayers().size() == 1);
    }
}

TEST_CASE("playback: while playing the canvas is for panning, except a live layer's emitter")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.addFrame();
    editor.setTool(Editor::Pencil);
    CHECK_FALSE(editor.lockedToPan());
    editor.setPlaying(true);
    REQUIRE(editor.playing());
    CHECK(editor.lockedToPan()); // an ordinary layer: pan and zoom only

    // A live layer: Move drags the emitter and the eyedropper picks, both without stopping the animation.
    editor.setPlaying(false);
    editor.addParticleLayer();
    REQUIRE(editor.liveLayer());
    editor.setPlaying(true);
    REQUIRE(editor.playing());
    CHECK(editor.tool() == Editor::Move);
    CHECK_FALSE(editor.lockedToPan());
    const QPointF start = editor.liveEmitter();
    editor.beginStroke({5, 5}, false, {});
    editor.moveStroke({8, 7}, {});
    editor.endStroke();
    CHECK(editor.playing());
    CHECK(editor.liveEmitter() != start);
    editor.setTool(Editor::Eyedropper);
    CHECK_FALSE(editor.lockedToPan());
    editor.beginStroke({5, 5}, false, {});
    editor.endStroke();
    CHECK(editor.playing());
    editor.setTool(Editor::Hand);
    CHECK(editor.lockedToPan());

    // Typing numbers and nudging with the keys keep it playing too.
    editor.setParticleField("lifetime", 9, false);
    editor.nudge(1, 0, false);
    CHECK(editor.playing());
}

TEST_CASE("frames: adding up to a total appends empty frames as one undo step")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.addFrame();
    editor.setCurrentFrame(0);
    const int start = editor.frameCount();
    editor.addFramesUpTo(start); // nothing to add
    CHECK(editor.frameCount() == start);
    editor.addFramesUpTo(start + 5);
    CHECK(editor.frameCount() == start + 5);
    CHECK(editor.currentFrame() == 0);
    editor.undo();
    CHECK(editor.frameCount() == start);
    editor.redo();
    CHECK(editor.frameCount() == start + 5);

    // A live layer is drawn for the new frames.
    editor.addParticleLayer();
    editor.addFramesUpTo(start + 8);
    CHECK(editor.frameCount() == start + 8);
}

TEST_CASE("live layer: the particles can take the shape of the custom brush")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.addParticleLayer();
    CHECK_FALSE(editor.useCustomBrushAsParticle()); // no brush yet

    // A brush from two pixels on an ordinary layer (3 x 1).
    editor.setCurrentLayer(0);
    click(editor, {4, 4});
    click(editor, {6, 4});
    editor.setTool(Editor::Select);
    editor.beginStroke({4, 4}, false, {});
    editor.moveStroke({6, 4}, {});
    editor.endStroke();
    REQUIRE(editor.brushFromSelection());
    editor.deselect();

    editor.setCurrentLayer(1);
    REQUIRE(editor.liveLayer());
    REQUIRE(editor.useCustomBrushAsParticle());
    const auto recipe = editor.particleRecipe();
    CHECK(recipe.shape == core::ParticleRecipe::Shape::Brush);
    CHECK(recipe.brush.width == 3);
    CHECK(recipe.brush.height == 1);
    REQUIRE(recipe.brush.pixels.size() == 3);
    CHECK((recipe.brush.pixels[0] >> 24) == 255);
    CHECK((recipe.brush.pixels[1] >> 24) == 0);
    CHECK((recipe.brush.pixels[2] >> 24) == 255);
    CHECK((recipe.brush.pixels[0] & 0xffffff) == 0x0000ff); // the red of the pixel (r in the low byte)
    CHECK(editor.particleSettings().value("brushWidth").toInt() == 3);

    // It stays in the recipe, whatever the editor's brush does afterwards.
    editor.undo();
    CHECK(editor.particleRecipe().shape != core::ParticleRecipe::Shape::Brush);
    editor.redo();
    CHECK(editor.particleRecipe().brush.width == 3);
}

TEST_CASE("live layer: a custom brush with a transparent margin still shows when taken as the particle shape")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.addParticleLayer();

    // Two pixels in the middle of a larger selection: the brush has an empty margin around them.
    editor.setCurrentLayer(0);
    click(editor, {10, 10});
    click(editor, {11, 10});
    editor.setTool(Editor::Select);
    editor.beginStroke({7, 7}, false, {});
    editor.moveStroke({14, 13}, {});
    editor.endStroke();
    REQUIRE(editor.brushFromSelection());
    editor.deselect();

    editor.setCurrentLayer(1);
    REQUIRE(editor.liveLayer());
    REQUIRE(editor.useCustomBrushAsParticle());
    // The particles are the brush's pixels, not nothing.
    const auto recipe = editor.particleRecipe();
    CHECK(recipe.brush.width == 2); // trimmed to what is in it
    CHECK(recipe.brush.height == 1);
    CHECK(recipe.sizeStart == 2);   // and shown at its own size
    CHECK(recipe.sizeEnd == 2);
    int lit = 0;
    const QImage image = editor.composite();
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            lit += image.pixelColor(x, y).alpha() > 0 && image.pixelColor(x, y) == QColor(255, 0, 0) && y > 12;
    CHECK(lit > 0);
}

TEST_CASE("live layer: the source around the emitter is set through the panel's fields")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addParticleLayer();
    CHECK(editor.particleSettings().value("source").toInt() == 0);
    editor.setParticleField("source", 2, false);
    editor.setParticleField("sourceWidth", 30, false);
    editor.setParticleField("sourceHeight", 99999, false);
    editor.setParticleField("sourceAngle", 45, false);
    editor.setParticleField("sourceFill", 0, false);
    const auto r = editor.particleRecipe();
    CHECK(r.source == core::ParticleRecipe::Source::Rectangle);
    CHECK(r.sourceWidth == 30);
    CHECK(r.sourceHeight == 4096); // clamped
    CHECK(r.sourceAngle == 45);
    CHECK_FALSE(r.sourceFill);
    // Reset puts them back in one step.
    editor.resetParticleFields({"source", "sourceWidth", "sourceHeight", "sourceAngle", "sourceFill"});
    CHECK(editor.particleRecipe().source == core::ParticleRecipe::Source::Point);
    CHECK(editor.particleRecipe().sourceFill);
    editor.undo();
    CHECK(editor.particleRecipe().source == core::ParticleRecipe::Source::Rectangle);
}

TEST_CASE("live layer: fields are added, edited, moved and removed as undoable steps")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addParticleLayer();
    CHECK(editor.particleSettings().value("fields").toList().isEmpty());
    const QPointF emitter = editor.liveEmitter();

    const int first = editor.addParticleField(0);
    REQUIRE(first == 0);
    CHECK(editor.addParticleField(0) == 1);
    auto fields = editor.particleSettings().value("fields").toList();
    REQUIRE(fields.size() == 2);
    CHECK(fields[0].toMap().value("x").toDouble() == emitter.x());
    CHECK(fields[0].toMap().value("y").toDouble() == emitter.y() - 24);

    editor.setParticleFieldValue(1, "strength", -0.6, false);
    editor.setParticleFieldValue(1, "radius", 99999, false);
    editor.setParticleFieldValue(1, "nonsense", 1, false);
    editor.setParticleFieldPosition(0, 10, 12, true);
    editor.setParticleFieldPosition(0, 11, 13, true);
    const auto r = editor.particleRecipe();
    CHECK(r.fields[1].strength == -0.6);
    CHECK(r.fields[1].radius == 4096);
    CHECK(r.fields[0].x == 11);
    CHECK(r.fields[0].y == 13);
    editor.undo(); // the two moves are one step
    CHECK(editor.particleRecipe().fields[0].x == emitter.x());

    editor.removeParticleField(0);
    CHECK(editor.particleRecipe().fields.size() == 1);
    editor.removeParticleField(5); // not there
    CHECK(editor.particleRecipe().fields.size() == 1);
    editor.undo();
    CHECK(editor.particleRecipe().fields.size() == 2);
}

TEST_CASE("live layer: vortex, wind and floor fields start sensibly and clamp their own settings")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addParticleLayer();
    const QPointF emitter = editor.liveEmitter();
    CHECK(editor.addParticleField(7) == -1); // not a kind

    const int vortex = editor.addParticleField(1);
    const int wind = editor.addParticleField(2);
    const int floor = editor.addParticleField(3);
    REQUIRE(floor == 2);
    auto r = editor.particleRecipe();
    CHECK(r.fields[size_t(vortex)].type == core::ParticleField::Type::Vortex);
    CHECK(r.fields[size_t(wind)].type == core::ParticleField::Type::Wind);
    CHECK(r.fields[size_t(wind)].strength == 0.1); // a gentle wind
    CHECK(r.fields[size_t(floor)].y == emitter.y() + 24); // below the emitter

    editor.setParticleFieldValue(wind, "angle", 999, false);
    editor.setParticleFieldValue(wind, "turbulence", 250, false);
    editor.setParticleFieldValue(wind, "scale", 0, false);
    editor.setParticleFieldValue(floor, "mode", 9, false);
    editor.setParticleFieldValue(floor, "bounciness", -5, false);
    editor.setParticleFieldValue(floor, "friction", 40, false);
    r = editor.particleRecipe();
    CHECK(r.fields[size_t(wind)].angle == 360);
    CHECK(r.fields[size_t(wind)].turbulence == 100);
    CHECK(r.fields[size_t(wind)].scale == 1);
    CHECK(r.fields[size_t(floor)].mode == 2);
    CHECK(r.fields[size_t(floor)].bounciness == 0);
    CHECK(r.fields[size_t(floor)].friction == 40);
    const auto map = editor.particleSettings().value("fields").toList()[floor].toMap();
    CHECK(map.value("type").toInt() == 3);
    CHECK(map.value("mode").toInt() == 2);
}

TEST_CASE("live layer: the source and field tools draw the emitter's source and place fields")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    // They belong to live layers: an ordinary layer won't take them.
    editor.setTool(Editor::SourceTool);
    CHECK(editor.tool() != Editor::SourceTool);
    editor.setTool(Editor::FieldTool);
    CHECK(editor.tool() != Editor::FieldTool);

    editor.setTool(Editor::Line);
    editor.addParticleLayer();
    CHECK(editor.tool() == Editor::Move);
    editor.setTool(Editor::SourceTool);
    REQUIRE(editor.tool() == Editor::SourceTool);

    // A rectangle from corner to corner: 10 x 5 pixels, the emitter in its middle.
    editor.setLiveSourceKind(2);
    editor.beginStroke({10, 10}, false, {});
    editor.moveStroke({15, 12}, {});
    editor.moveStroke({19, 14}, {});
    editor.endStroke();
    auto r = editor.particleRecipe();
    CHECK(r.source == core::ParticleRecipe::Source::Rectangle);
    CHECK(r.sourceWidth == 10);
    CHECK(r.sourceHeight == 5);
    CHECK(r.x == 15);
    CHECK(r.y == 12.5);
    editor.undo(); // the whole drag is one step
    CHECK(editor.particleRecipe().source == core::ParticleRecipe::Source::Point);
    editor.redo();

    // Shift: a square; the other way up and left works too.
    editor.beginStroke({30, 30}, false, Qt::ShiftModifier);
    editor.moveStroke({26, 28}, Qt::ShiftModifier);
    editor.endStroke();
    r = editor.particleRecipe();
    CHECK(r.sourceWidth == 5);
    CHECK(r.sourceHeight == 5);
    CHECK(r.x == 28.5); // pixels 26 to 30

    // Alt: from the middle outward, and an oval.
    editor.setLiveSourceKind(3);
    editor.beginStroke({20, 20}, false, Qt::AltModifier);
    editor.moveStroke({23, 22}, Qt::AltModifier);
    editor.endStroke();
    r = editor.particleRecipe();
    CHECK(r.source == core::ParticleRecipe::Source::Ellipse);
    CHECK(r.sourceWidth == 7);
    CHECK(r.sourceHeight == 5);
    CHECK(r.x == 20.5);

    // A line: its length counts the pixels, and it turns toward the pointer.
    editor.setLiveSourceKind(1);
    editor.beginStroke({10, 40}, false, {});
    editor.moveStroke({10, 49}, {});
    editor.endStroke();
    r = editor.particleRecipe();
    CHECK(r.source == core::ParticleRecipe::Source::Line);
    CHECK(r.sourceWidth == 10);
    CHECK(r.sourceAngle == 90);
    CHECK(r.x == 10.5);
    CHECK(r.y == 45);

    // The field tool places the chosen kind where it is clicked, announces it, and moves it with the drag.
    editor.setTool(Editor::FieldTool);
    REQUIRE(editor.tool() == Editor::FieldTool);
    QSignalSpy placed(&editor, &Editor::liveFieldPlaced);
    editor.setLiveFieldKind(1);
    editor.beginStroke({40, 8}, false, {});
    editor.moveStroke({44, 10}, {});
    editor.endStroke();
    REQUIRE(placed.count() == 1);
    CHECK(placed.first().first().toInt() == 0);
    r = editor.particleRecipe();
    REQUIRE(r.fields.size() == 1);
    CHECK(r.fields[0].type == core::ParticleField::Type::Vortex);
    CHECK(r.fields[0].x == 44.5);
    CHECK(r.fields[0].y == 10.5);

    // Back on an ordinary layer the live-only tool gives way.
    editor.setCurrentLayer(0);
    CHECK(editor.tool() != Editor::FieldTool);
    CHECK(editor.tool() == Editor::Line); // the one it replaced
}

TEST_CASE("live layer: presets start a layer, replace its settings, and are what the reset dots go back to")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    const auto list = editor.particlePresetList();
    REQUIRE(list.size() >= 1);
    CHECK(list[0].toMap().value("id").toString() == "fire");

    editor.addParticleLayerFrom("fire");
    REQUIRE(editor.liveLayer());
    auto s = editor.particleSettings();
    CHECK(s.value("preset").toString() == "fire");
    CHECK(s.value("presetName").toString() == "Fire");
    CHECK(s.value("presetEdited").toInt() == 0);
    const int fireLifetime = editor.particleRecipe().lifetime;
    CHECK(fireLifetime != core::ParticleRecipe().lifetime);
    CHECK(editor.particleRecipe().colors.size() == 4);
    CHECK(editor.particleReference().value("lifetime").toInt() == fireLifetime); // not the built-in one
    CHECK(editor.layers()[editor.currentLayer()].toMap().value("name").toString().startsWith("Fire"));

    // Edited once something differs; the emitter moving, a reseed and the seed field don't count.
    editor.setLiveEmitter(QPointF(10.5, 12.5));
    editor.reseedParticles();
    CHECK(editor.particleSettings().value("presetEdited").toInt() == 0);
    editor.setParticleField("lifetime", fireLifetime + 4, false);
    CHECK(editor.particleSettings().value("presetEdited").toInt() == 1);

    // Resetting a value goes back to the preset's, not the built-in one.
    editor.resetParticleFields({"lifetime"});
    CHECK(editor.particleRecipe().lifetime == fireLifetime);
    CHECK(editor.particleSettings().value("presetEdited").toInt() == 0);

    // Applying a preset keeps the emitter where it is, in one undo step.
    editor.setParticleField("count", 5, false);
    const QPointF emitter = editor.liveEmitter();
    editor.applyParticlePreset("fire");
    CHECK(editor.liveEmitter() == emitter);
    CHECK(editor.particleRecipe().count == core::findParticlePreset("fire")->recipe.count);
    editor.undo();
    CHECK(editor.particleRecipe().count == 5);
    editor.redo();

    // The plain emitter: no preset, the built-in values, the primary color; resets go to built-in.
    editor.setPrimaryColor(QColor(10, 200, 30));
    editor.applyParticlePreset("");
    s = editor.particleSettings();
    CHECK(s.value("preset").toString().isEmpty());
    CHECK(s.value("presetName").toString().isEmpty());
    CHECK(editor.particleRecipe().lifetime == core::ParticleRecipe().lifetime);
    CHECK(editor.particleRecipe().colors.size() == 1);
    CHECK(editor.particleRecipe().fields.empty());
    CHECK(editor.liveEmitter() == emitter);

    // It survives the file.
    editor.applyParticlePreset("fire");
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("fire.aseprite");
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    Editor reopened;
    REQUIRE(reopened.openFile(QUrl::fromLocalFile(path)));
    bool live = false;
    for (int i = 0; i < int(reopened.layers().size()); ++i) {
        reopened.setCurrentLayer(i);
        live = live || reopened.liveLayer();
        if (reopened.liveLayer())
            break;
    }
    REQUIRE(live);
    CHECK(reopened.particleRecipe().preset == "fire");
    CHECK(reopened.particleSettings().value("presetEdited").toInt() == 0);
}

TEST_CASE("live layer: a quick effect adds the layer and the frames it needs as one step, scaled to the sprite")
{
    Editor editor;
    editor.newSprite({{"width", 128}, {"height", 128}});
    CHECK(editor.frameCount() == 1);
    const int layers = int(editor.layers().size());
    editor.addQuickEffect("smoke");
    CHECK(int(editor.layers().size()) == layers + 1);
    REQUIRE(editor.liveLayer());
    CHECK(editor.particleSettings().value("presetName").toString() == "Smoke");
    const core::ParticleRecipe r = editor.particleRecipe();
    CHECK(r.presetScale == 2.0); // 128 over 64
    CHECK(editor.frameCount() == core::particleFramesNeeded(r)); // its loop: 32
    CHECK(editor.frameCount() == 32);
    CHECK(editor.particleSettings().value("presetEdited").toInt() == 0); // scaled, not edited

    // A longer sprite is not shortened.
    editor.undo();
    CHECK(editor.frameCount() == 1); // one step took both back
    CHECK(int(editor.layers().size()) == layers);
    editor.addFramesUpTo(50);
    editor.addQuickEffect("fire");
    CHECK(editor.frameCount() == 50);
    CHECK(editor.liveLayer());

    // A reset goes back to the scaled preset values, not the unscaled ones.
    const double scaledSpeed = editor.particleRecipe().speed;
    editor.setParticleField("speed", 9, false);
    editor.resetParticleFields({"speed"});
    CHECK(editor.particleRecipe().speed == scaledSpeed);
    CHECK(scaledSpeed == core::findParticlePreset("fire")->recipe.speed * 2);
}

TEST_CASE("live layer: a quick effect with a home puts the emitter there")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addQuickEffect("snowpile");
    REQUIRE(editor.liveLayer());
    CHECK(editor.liveEmitter().y() == 0.5); // the top
    CHECK(editor.liveEmitter().x() == 32.5);
    CHECK(editor.frameCount() == 60); // as long as it was made for: it runs to the end of whatever there is
    auto fields = editor.particleSettings().value("fields").toList();
    REQUIRE(fields.size() == 2);
    CHECK(fields[1].toMap().value("type").toInt() == 3); // the floor, near the bottom
    CHECK(fields[1].toMap().value("y").toDouble() == doctest::Approx(60.0));
    editor.addQuickEffect("fire"); // the others start low
    CHECK(editor.liveEmitter().y() == 44.5);
}

TEST_CASE("live layer: the sub-particles are edited through a context of their own")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addParticleLayer();
    CHECK(editor.particleSettings().value("hasSub").toInt() == 0);
    editor.setParticleContext(1); // nothing to edit yet
    CHECK(editor.particleContext() == 0);

    // Turning a trigger on gives them a recipe; they are born on impact here.
    const int parentCount = editor.particleRecipe().count;
    editor.setParticleField("subTrigger", 2, false);
    auto r = editor.particleRecipe();
    CHECK(r.subTrigger == core::ParticleRecipe::Trigger::Impact);
    REQUIRE(r.children.size() == 1);
    CHECK(editor.particleSettings().value("hasSub").toInt() == 1);
    editor.setParticleField("subInherit", 40, false);
    CHECK(editor.particleRecipe().subInherit == 40);
    CHECK(editor.particleContext() == 0);

    // In the other context every call is about the sub-particles.
    editor.setParticleContext(1);
    REQUIRE(editor.particleContext() == 1);
    CHECK(editor.particleSettings().value("context").toInt() == 1);
    CHECK(editor.particleSettings().value("count").toInt() == r.children[0].count);
    editor.setParticleField("count", 9, false);
    editor.setParticleField("lifetime", 5, false);
    editor.setParticleColors({QColor(10, 20, 30), QColor(40, 50, 60)});
    r = editor.particleRecipe();
    CHECK(r.children[0].count == 9);
    CHECK(r.children[0].lifetime == 5);
    CHECK(r.children[0].colors.size() == 2);
    CHECK(r.count == parentCount); // the particles are as they were
    CHECK(r.colors.size() == 1);

    // Resets go back to the sub-particles' own starting values, not the parent's.
    editor.resetParticleFields({"count", "lifetime"});
    r = editor.particleRecipe();
    CHECK(r.children[0].count == 4);
    CHECK(r.children[0].lifetime == 8);
    CHECK(editor.particleReference().value("count").toInt() == 4);

    // Back to the particles, and a trigger off keeps the recipe for later but stops the editing.
    editor.setParticleContext(0);
    CHECK(editor.particleSettings().value("count").toInt() == parentCount);
    editor.setParticleContext(1);
    editor.setParticleContext(0);
    editor.setParticleField("subTrigger", 0, false);
    CHECK(editor.particleRecipe().children.size() == 1);
    CHECK(editor.particleRecipe().subTrigger == core::ParticleRecipe::Trigger::None);

    // Changing layers leaves the sub-particles' context.
    editor.setParticleField("subTrigger", 1, false);
    editor.setParticleContext(1);
    REQUIRE(editor.particleContext() == 1);
    editor.setCurrentLayer(0);
    editor.setCurrentLayer(1);
    CHECK(editor.particleContext() == 0);

    // And it all survives a save.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("sub.aseprite");
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    Editor reopened;
    REQUIRE(reopened.openFile(QUrl::fromLocalFile(path)));
    for (int i = 0; i < int(reopened.layers().size()); ++i) {
        reopened.setCurrentLayer(i);
        if (reopened.liveLayer())
            break;
    }
    REQUIRE(reopened.liveLayer());
    CHECK(reopened.particleRecipe().children.size() == 1);
    CHECK(reopened.particleRecipe().subTrigger == core::ParticleRecipe::Trigger::Death);
}

TEST_CASE("live layer: a quick group adds a group of live layers, their frames, in one step")
{
    Editor editor;
    editor.newSprite({{"width", 128}, {"height", 128}});
    const auto list = editor.quickEffectList();
    bool hasGroup = false, hasSingle = false;
    for (const QVariant& item : list) {
        hasGroup = hasGroup || (item.toMap().value("id") == "campfire" && item.toMap().value("group").toBool());
        hasSingle = hasSingle || (item.toMap().value("id") == "fire" && !item.toMap().value("group").toBool());
    }
    CHECK(hasGroup);
    CHECK(hasSingle);

    const int layers = int(editor.layers().size());
    editor.addQuickEffect("campfire");
    CHECK(int(editor.layers().size()) == layers + 3); // the group and its two layers
    CHECK(editor.frameCount() == 32);
    REQUIRE(editor.liveLayer()); // the flame is current
    CHECK(editor.particleSettings().value("presetName").toString() == "Fire");
    const QPointF fire = editor.liveEmitter();
    int smokeIndex = -1;
    for (int i = 0; i < int(editor.layers().size()); ++i) {
        editor.setCurrentLayer(i);
        if (editor.liveLayer() && editor.particleSettings().value("presetName").toString() == "Smoke")
            smokeIndex = i;
    }
    REQUIRE(smokeIndex >= 0);
    editor.setCurrentLayer(smokeIndex);
    CHECK(editor.liveEmitter().x() == fire.x());
    CHECK(editor.liveEmitter().y() == doctest::Approx(fire.y() - 14 * 2)); // scaled: 128 over 64
    bool group = false;
    for (const QVariant& row : editor.layers())
        group = group || row.toMap().value("kind").toString() == "group";
    CHECK(group);

    // One step takes all of it back.
    editor.undo();
    CHECK(int(editor.layers().size()) == layers);
    CHECK(editor.frameCount() == 1);
    editor.redo();
    CHECK(int(editor.layers().size()) == layers + 3);
}

TEST_CASE("live layer: a layer's settings can be saved as a preset and used again, at any sprite size")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QString id;
    {
        Editor editor;
        editor.setPresetFolder(dir.path());
        editor.newSprite({{"width", 128}, {"height", 128}}); // scale 2
        editor.addParticleLayer();
        CHECK(editor.userPresets().isEmpty());
        CHECK_FALSE(editor.saveParticlePreset("   ")); // a name is needed
        editor.setLiveEmitter(QPointF(64.5, 32.5));
        editor.setParticleField("speed", 4, false);
        editor.setParticleField("sizeStart", 6, false);
        editor.setParticleField("count", 20, false);
        editor.setParticleField("source", 1, false);
        editor.setParticleField("sourceWidth", 40, false);
        const int field = editor.addParticleField(3); // a floor 24 below the emitter
        REQUIRE(field == 0);
        CHECK(editor.saveParticlePreset("My Spray"));
        REQUIRE(editor.userPresets().size() == 1);
        id = editor.userPresets()[0].toMap().value("id").toString();
        CHECK(id.startsWith("user:"));
        CHECK(editor.userPresets()[0].toMap().value("name").toString() == "My Spray");
        // The layer is that preset now, unedited.
        CHECK(editor.particleSettings().value("preset").toString() == id);
        CHECK(editor.particleSettings().value("presetName").toString() == "My Spray");
        CHECK(editor.particleSettings().value("presetEdited").toInt() == 0);

        // The same name replaces it, a different one adds another.
        editor.setParticleField("speed", 5, false);
        CHECK(editor.saveParticlePreset("my spray"));
        CHECK(editor.userPresets().size() == 1);
        CHECK(editor.saveParticlePreset("Other"));
        CHECK(editor.userPresets().size() == 2);
    }

    // A new session finds them; on a sprite half the size the numbers are half what they were made at.
    Editor editor;
    editor.setPresetFolder(dir.path());
    REQUIRE(editor.userPresets().size() == 2);
    editor.newSprite({{"width", 64}, {"height", 64}});
    bool listed = false;
    for (const QVariant& item : editor.quickEffectList())
        listed = listed || (item.toMap().value("id").toString() == id && item.toMap().value("user").toBool());
    CHECK(listed);
    editor.addQuickEffect(id);
    REQUIRE(editor.liveLayer());
    auto r = editor.particleRecipe();
    CHECK(r.speed == 2.5); // 5 on the 128 pixel sprite it was made on, so 2.5 for a 64 pixel one
    CHECK(r.sizeStart == 6 / 2.0);
    CHECK(r.sourceWidth == 20);
    CHECK(r.count == 10);
    CHECK(r.presetScale == 1.0);
    CHECK(r.preset == id.toStdString());
    REQUIRE(r.fields.size() == 1);
    CHECK(r.fields[0].y - r.y == doctest::Approx(24 / 2.0)); // 24 below at scale 2, 12 below at scale 1
    CHECK(editor.particleSettings().value("presetEdited").toInt() == 0);
    // Its home: where the emitter was, as a fraction of the sprite (half way across, a quarter down).
    CHECK(editor.liveEmitter().x() == 32.5);
    CHECK(editor.liveEmitter().y() == 16.5);

    // The pill menu offers it, and deleting removes the file.
    editor.deleteUserPreset(id);
    CHECK(editor.userPresets().size() == 1);
    Editor again;
    again.setPresetFolder(dir.path());
    CHECK(again.userPresets().size() == 1);
}

TEST_CASE("move tool: with several layers, cels or frames selected it moves them all together")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.addFrame();
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    // Layer 0 (the background layer of the new sprite) and one more, a red pixel each, on every frame.
    editor.addLayer();
    const int lower = 0, upper = editor.currentLayer();
    REQUIRE(upper > lower);
    for (int f = 0; f < 3; ++f) {
        editor.setCurrentFrame(f);
        editor.setCurrentLayer(lower);
        editor.beginStroke({4, 4}, false, {});
        editor.endStroke();
        editor.setCurrentLayer(upper);
        editor.beginStroke({10, 4}, false, {});
        editor.endStroke();
    }
    editor.setTool(Editor::Move);
    const auto at = [&](int layer, int frame, int x, int y) {
        editor.setCurrentFrame(frame);
        editor.setCurrentLayer(layer);
        return editor.composite().pixelColor(x, y).alpha() > 0;
    };
    const auto drag = [&](QPoint from, QPoint to) {
        editor.beginStroke(from, false, {});
        editor.moveStroke(to, {});
        editor.endStroke();
    };
    REQUIRE(at(lower, 0, 4, 4));
    REQUIRE(at(upper, 0, 10, 4));

    // Two layers' cels on this frame (a range of cels across both): both go.
    editor.setCurrentFrame(0);
    editor.selectCels(lower, 0, upper, 0);
    drag({20, 20}, {23, 22}); // by (3, 2)
    CHECK(at(lower, 0, 7, 6));
    CHECK(at(upper, 0, 13, 6));
    CHECK_FALSE(at(lower, 0, 4, 4));
    CHECK(at(lower, 1, 4, 4)); // another frame was not selected
    editor.undo();
    CHECK(at(lower, 0, 4, 4));
    CHECK(at(upper, 0, 10, 4)); // one step took both back

    // Frames: every layer's cel in the selected frames.
    editor.selectFrames(0, 1);
    drag({20, 20}, {18, 20}); // by (-2, 0)
    for (int f = 0; f < 2; ++f) {
        CHECK(at(lower, f, 2, 4));
        CHECK(at(upper, f, 8, 4));
    }
    CHECK(at(lower, 2, 4, 4)); // the third frame stays
    editor.undo();
    CHECK(at(lower, 0, 4, 4));
    CHECK(at(lower, 1, 4, 4));

    // A range of one layer over several frames.
    editor.selectCels(upper, 0, upper, 2);
    drag({20, 20}, {20, 23}); // by (0, 3)
    for (int f = 0; f < 3; ++f)
        CHECK(at(upper, f, 10, 7));
    CHECK(at(lower, 0, 4, 4)); // the other layer stays
    editor.undo();

    // Layers picked together: their cels on the current frame.
    editor.clearRange();
    editor.setCurrentFrame(1);
    editor.setCurrentLayer(lower);
    editor.togglePick(upper);
    REQUIRE(editor.picksActive());
    drag({20, 20}, {21, 21});
    CHECK(at(lower, 1, 5, 5));
    CHECK(at(upper, 1, 11, 5));
    CHECK(at(lower, 0, 4, 4)); // another frame
    editor.undo();

    // One cel is the plain move of the current one.
    editor.clearRange();
    editor.setCurrentFrame(0);
    editor.setCurrentLayer(upper);
    editor.selectCels(upper, 0, upper, 0);
    drag({20, 20}, {25, 20});
    CHECK(at(upper, 0, 15, 4));
    CHECK(at(lower, 0, 4, 4));
}

TEST_CASE("move tool: live layers selected with others move their emitters with them")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addQuickEffect("campfire"); // a group with a flame and smoke layer
    int fire = -1, smoke = -1;
    for (int i = 0; i < int(editor.layers().size()); ++i) {
        editor.setCurrentLayer(i);
        if (!editor.liveLayer())
            continue;
        const QString name = editor.particleSettings().value("presetName").toString();
        (name == "Fire" ? fire : smoke) = i;
    }
    REQUIRE(fire >= 0);
    REQUIRE(smoke >= 0);
    editor.setCurrentLayer(fire);
    const QPointF fireAt = editor.liveEmitter();
    editor.setCurrentLayer(smoke);
    const QPointF smokeAt = editor.liveEmitter();

    // Both selected (a range over both): one drag moves both.
    editor.setCurrentLayer(fire);
    editor.selectCels(std::min(fire, smoke), 0, std::max(fire, smoke), 0);
    editor.setTool(Editor::Move);
    editor.beginStroke({10, 10}, false, {});
    editor.moveStroke({14, 7}, {});
    editor.endStroke();
    editor.setCurrentLayer(fire);
    CHECK(editor.liveEmitter() == QPointF(fireAt.x() + 4, fireAt.y() - 3));
    editor.setCurrentLayer(smoke);
    CHECK(editor.liveEmitter() == QPointF(smokeAt.x() + 4, smokeAt.y() - 3));
    editor.undo(); // both back in one step
    editor.setCurrentLayer(fire);
    CHECK(editor.liveEmitter() == fireAt);
    editor.setCurrentLayer(smoke);
    CHECK(editor.liveEmitter() == smokeAt);

    // A layer that is locked stays where it is, the other goes.
    editor.setCurrentLayer(smoke);
    editor.setLayerLocked(smoke, true);
    editor.setCurrentLayer(fire);
    editor.selectCels(std::min(fire, smoke), 0, std::max(fire, smoke), 0);
    editor.beginStroke({10, 10}, false, {});
    editor.moveStroke({11, 10}, {});
    editor.endStroke();
    CHECK(editor.liveEmitter() == QPointF(fireAt.x() + 1, fireAt.y()));
    editor.setCurrentLayer(smoke);
    CHECK(editor.liveEmitter() == smokeAt);
    editor.undo();
    editor.setLayerLocked(smoke, false);

    // One of them alone, as before.
    editor.clearRange();
    editor.setCurrentLayer(smoke);
    editor.beginStroke({10, 10}, false, {});
    editor.moveStroke({12, 10}, {});
    editor.endStroke();
    CHECK(editor.liveEmitter() == QPointF(smokeAt.x() + 2, smokeAt.y()));
    editor.setCurrentLayer(fire);
    CHECK(editor.liveEmitter() == fireAt);
}

TEST_CASE("transform on a selection: Cmd+T lifts several cels into one transform that lands in each")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.addFrame();
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.addLayer();
    const int lower = 0, upper = editor.currentLayer();
    for (int f = 0; f < 3; ++f) {
        editor.setCurrentFrame(f);
        editor.setCurrentLayer(lower);
        editor.beginStroke({4, 4}, false, {});
        editor.endStroke();
        editor.setCurrentLayer(upper);
        editor.beginStroke({10, 4}, false, {});
        editor.endStroke();
    }
    const auto at = [&](int layer, int frame, int x, int y) {
        editor.setCurrentFrame(frame);
        editor.setCurrentLayer(layer);
        return editor.composite().pixelColor(x, y).alpha() > 0;
    };

    // Frames 0 and 1, every layer: Cmd+T shows the handles (a float over the content of them all).
    editor.setCurrentFrame(0);
    editor.selectFrames(0, 1);
    editor.startTransform();
    REQUIRE(editor.floating());
    CHECK(editor.floatingLifted());
    CHECK(editor.transformBox() == QRect(4, 4, 7, 1)); // the pixels of the two layers, together
    // The arrows move the box; so does a drag; and they go on from where the other left off.
    CHECK(editor.nudge(1, 0, false));
    CHECK(editor.nudge(0, 1, false));
    CHECK(editor.transformBox() == QRect(5, 5, 7, 1));
    editor.beginStroke({20, 20}, false, {}); // the Move tool drags the box
    editor.moveStroke({22, 20}, {});
    editor.endStroke();
    CHECK(editor.transformBox() == QRect(7, 5, 7, 1));
    CHECK(editor.nudge(0, 10, false));
    CHECK(editor.transformBox() == QRect(7, 15, 7, 1)); // from where the drag got to
    editor.commitFloating();
    CHECK_FALSE(editor.floating());
    for (int f = 0; f < 2; ++f) { // both frames, both layers, one step
        CHECK(at(lower, f, 7, 15));
        CHECK(at(upper, f, 13, 15));
        CHECK_FALSE(at(lower, f, 4, 4));
    }
    CHECK(at(lower, 2, 4, 4)); // the third frame was not selected
    editor.undo();
    CHECK(at(lower, 0, 4, 4));
    CHECK(at(lower, 1, 4, 4));
    CHECK(at(upper, 1, 10, 4));

    // Cancel puts them back.
    editor.selectCels(lower, 0, upper, 0);
    editor.startTransform();
    REQUIRE(editor.floating());
    editor.nudge(5, 0, false);
    editor.cancelFloating();
    CHECK_FALSE(editor.floating());
    CHECK(at(lower, 0, 4, 4));
    CHECK(at(upper, 0, 10, 4));

    // Scale: twice as wide takes the two pixels twice as far apart, in every cel together.
    editor.selectFrames(0, 1);
    editor.setCurrentFrame(0);
    editor.startTransform();
    editor.setTransformBox(QRect(4, 4, 14, 1));
    editor.commitFloating();
    for (int f = 0; f < 2; ++f) {
        int lowerX = -1, upperX = -1;
        for (int x = 0; x < 32; ++x) {
            if (at(lower, f, x, 4) && lowerX < 0)
                lowerX = x;
            if (at(upper, f, x, 4))
                upperX = x;
        }
        CHECK(upperX - lowerX >= 12); // about 12 apart: 6 doubled
    }
    editor.undo();
    CHECK(at(upper, 1, 10, 4));

    // Flipping inside the session flips them all, about the middle of what is there.
    editor.selectCels(lower, 0, upper, 0);
    editor.setCurrentFrame(0);
    editor.startTransform();
    editor.flipHorizontal();
    editor.commitFloating();
    CHECK(at(lower, 0, 10, 4));
    CHECK(at(upper, 0, 4, 4));
    editor.undo();

    // Turning a quarter: the two pixels, a row apart before, end up a column apart, in one line.
    editor.selectCels(lower, 0, upper, 0);
    editor.startTransform();
    editor.rotate90(true);
    editor.commitFloating();
    editor.setCurrentFrame(0);
    std::vector<QPoint> lit;
    const QImage turned = editor.composite();
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (turned.pixelColor(x, y).alpha() > 0)
                lit.push_back({x, y});
    REQUIRE(lit.size() == 2);
    CHECK(lit[0].x() == lit[1].x());                // one column
    CHECK(std::abs(lit[0].y() - lit[1].y()) == 6);  // as far apart as they were
    editor.undo();

    // A single cel keeps its own transform.
    editor.clearRange();
    editor.setCurrentLayer(lower);
    editor.setCurrentFrame(0);
    editor.startTransform();
    CHECK(editor.floating());
    CHECK(editor.transformBox() == QRect(4, 4, 1, 1));
    editor.cancelFloating();
}

TEST_CASE("flip on a selection: every selected cel flips about the middle of all of them")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.addLayer();
    const int lower = 0, upper = editor.currentLayer();
    // The lower layer has a pixel at x 4, the upper one at x 10, both at y 4, on frame 0 and 1.
    for (int f = 0; f < 2; ++f) {
        editor.setCurrentFrame(f);
        editor.setCurrentLayer(lower);
        editor.beginStroke({4, 4}, false, {});
        editor.endStroke();
        editor.setCurrentLayer(upper);
        editor.beginStroke({10, 4}, false, {});
        editor.endStroke();
    }
    const auto at = [&](int layer, int frame, int x, int y) {
        editor.setCurrentFrame(frame);
        editor.setCurrentLayer(layer);
        return editor.composite().pixelColor(x, y).alpha() > 0;
    };

    // Both layers on frame 0 (their cels cover the canvas, 32 wide): x 4 -> 27, x 10 -> 21, side by side as before.
    editor.selectCels(lower, 0, upper, 0);
    editor.flipHorizontal();
    CHECK(at(lower, 0, 27, 4));
    CHECK(at(upper, 0, 21, 4));
    CHECK_FALSE(at(lower, 0, 4, 4));
    CHECK(at(lower, 1, 4, 4)); // another frame, not selected
    editor.undo();
    CHECK(at(lower, 0, 4, 4));
    CHECK(at(upper, 0, 10, 4));

    // Vertically, every frame: y 4 -> 27 on both layers.
    editor.selectFrames(0, 1);
    editor.flipVertical();
    for (int f = 0; f < 2; ++f) {
        CHECK(at(lower, f, 4, 27));
        CHECK(at(upper, f, 10, 27));
    }
    editor.undo();

    // One cel is the plain flip as before.
    editor.clearRange();
    editor.setCurrentLayer(upper);
    editor.setCurrentFrame(0);
    editor.flipHorizontal();
    CHECK(at(lower, 0, 4, 4)); // the other layer is untouched
    CHECK(at(upper, 0, 10, 4)); // a lone pixel flips about itself
    editor.undo();
}

TEST_CASE("transform on a selection: live layers follow the box, and a free angle turns every cel the same")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addQuickEffect("campfire");
    int fire = -1, smoke = -1;
    for (int i = 0; i < int(editor.layers().size()); ++i) {
        editor.setCurrentLayer(i);
        if (!editor.liveLayer())
            continue;
        (editor.particleSettings().value("presetName").toString() == "Fire" ? fire : smoke) = i;
    }
    REQUIRE(fire >= 0);
    REQUIRE(smoke >= 0);
    editor.setCurrentLayer(fire);
    const QPointF fireAt = editor.liveEmitter();
    editor.setCurrentLayer(smoke);
    const QPointF smokeAt = editor.liveEmitter();

    // Only live layers selected: there is a box for the handles, and nudging it moves their emitters.
    editor.selectCels(std::min(fire, smoke), 0, std::max(fire, smoke), 0);
    editor.startTransform();
    REQUIRE(editor.floating());
    CHECK(editor.nudge(3, 0, false));
    CHECK(editor.nudge(0, -2, false));
    editor.commitFloating();
    editor.setCurrentLayer(fire);
    CHECK(editor.liveEmitter() == QPointF(fireAt.x() + 3, fireAt.y() - 2));
    editor.setCurrentLayer(smoke);
    CHECK(editor.liveEmitter() == QPointF(smokeAt.x() + 3, smokeAt.y() - 2));
    editor.undo(); // one step for both
    editor.setCurrentLayer(fire);
    CHECK(editor.liveEmitter() == fireAt);
    editor.setCurrentLayer(smoke);
    CHECK(editor.liveEmitter() == smokeAt);

    // And cancelled, they are where they were.
    editor.selectCels(std::min(fire, smoke), 0, std::max(fire, smoke), 0);
    editor.startTransform();
    editor.nudge(7, 7, false);
    editor.cancelFloating();
    editor.setCurrentLayer(fire);
    CHECK(editor.liveEmitter() == fireAt);
    editor.setCurrentLayer(smoke);
    CHECK(editor.liveEmitter() == smokeAt);
}

TEST_CASE("transform on a selection: a free angle turns the cels of every frame about the same point")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFrame();
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.addLayer();
    const int lower = 0, upper = editor.currentLayer();
    // The same two pixels, 10 apart in a row, on both layers' two frames.
    for (int f = 0; f < 2; ++f) {
        editor.setCurrentFrame(f);
        editor.setCurrentLayer(lower);
        editor.beginStroke({8, 16}, false, {});
        editor.endStroke();
        editor.setCurrentLayer(upper);
        editor.beginStroke({18, 16}, false, {});
        editor.endStroke();
    }
    editor.setCurrentFrame(0);
    editor.selectFrames(0, 1);
    editor.startTransform();
    editor.setTransformAngle(90);
    editor.commitFloating();
    // Both frames got the same turn: the pixels stand a column, 10 apart.
    for (int f = 0; f < 2; ++f) {
        editor.setCurrentFrame(f);
        const QImage image = editor.composite();
        std::vector<QPoint> lit;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if (image.pixelColor(x, y).alpha() > 0)
                    lit.push_back({x, y});
        REQUIRE(lit.size() == 2);
        CHECK(lit[0].x() == lit[1].x());
        CHECK(std::abs(lit[0].y() - lit[1].y()) == 10);
        if (f == 1)
            CHECK(lit[0].x() == lit[1].x());
    }
    // The two frames are identical to each other, so they turned alike.
    editor.setCurrentFrame(0);
    const QImage first = editor.composite();
    editor.setCurrentFrame(1);
    CHECK(editor.composite() == first);
}

TEST_CASE("transform on a selection: cels cropped to their content grow instead of cutting what moves past them")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("trimmed.aseprite");
    {
        Editor editor;
        editor.newSprite({{"width", 32}, {"height", 32}});
        editor.addFrame();
        editor.setPrimaryColor(QColor(255, 0, 0));
        editor.setTool(Editor::Pencil);
        editor.addLayer();
        for (int f = 0; f < 2; ++f) {
            editor.setCurrentFrame(f);
            editor.setCurrentLayer(0);
            for (int x = 6; x < 10; ++x) { // a 4 x 3 block
                for (int y = 8; y < 11; ++y) {
                    editor.beginStroke({x, y}, false, {});
                    editor.endStroke();
                }
            }
            editor.setCurrentLayer(1);
            editor.beginStroke({12, 9}, false, {});
            editor.endStroke();
        }
        REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    }
    Editor editor; // the file has cels the size of what is in them
    REQUIRE(editor.openFile(QUrl::fromLocalFile(path)));
    const auto lit = [&](int frame) {
        editor.setCurrentFrame(frame);
        const QImage image = editor.composite();
        int n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                n += image.pixelColor(x, y).alpha() > 0;
        return n;
    };
    REQUIRE(lit(0) == 13);
    REQUIRE(lit(1) == 13);

    // Both layers, both frames, moved 15 to the right: well past the cels' own edges.
    editor.setCurrentFrame(0);
    editor.selectFrames(0, 1);
    editor.startTransform();
    REQUIRE(editor.floating());
    for (int i = 0; i < 15; ++i)
        editor.nudge(1, 0, false);
    editor.commitFloating();
    CHECK(lit(0) == 13); // none lost
    CHECK(lit(1) == 13);
    editor.setCurrentFrame(0);
    CHECK(editor.composite().pixelColor(21, 9).alpha() > 0);  // the block arrived
    CHECK(editor.composite().pixelColor(27, 9).alpha() > 0);  // and the single pixel
    CHECK(editor.composite().pixelColor(8, 9).alpha() == 0);

    // One undo puts the cels back as they were.
    editor.undo();
    CHECK(lit(0) == 13);
    editor.setCurrentFrame(0);
    CHECK(editor.composite().pixelColor(6, 8).alpha() > 0);
    CHECK(editor.composite().pixelColor(21, 9).alpha() == 0);
    editor.redo();
    editor.setCurrentFrame(1);
    CHECK(editor.composite().pixelColor(21, 9).alpha() > 0);
}

TEST_CASE("live layer: pre-roll, a rate and running to the end are settings, and the layer follows the sprite's length")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    for (int i = 0; i < 19; ++i)
        editor.addFrame();
    editor.addParticleLayer();
    editor.setParticleField("startFrame", -30, false);
    editor.setParticleField("perFrame", 1, false);
    editor.setParticleField("rate", 3, false);
    editor.setParticleField("untilEnd", 1, false);
    const auto r = editor.particleRecipe();
    CHECK(r.startFrame == -30);
    CHECK(r.perFrame);
    CHECK(r.rate == 3);
    CHECK(r.untilEnd);
    editor.setParticleField("startFrame", -99999, false);
    CHECK(editor.particleRecipe().startFrame == -5000); // clamped
    editor.setParticleField("startFrame", -30, false);
    editor.setParticleField("rate", 9999, false);
    CHECK(editor.particleRecipe().rate == 500);
    editor.setParticleField("rate", 3, false);

    // It is under way at the first frame, and it goes on to the last, however many there are.
    editor.setCurrentFrame(0);
    const auto lit = [&](int frame) {
        editor.setCurrentFrame(frame);
        const QImage image = editor.composite();
        int n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                n += image.pixelColor(x, y).alpha() > 0;
        return n;
    };
    CHECK(lit(0) > 5);
    const int busyAtEnd = lit(19);
    CHECK(busyAtEnd > 5);
    editor.addFramesUpTo(60); // a longer sprite: the layer is drawn again for it
    CHECK(lit(59) > 5);
    CHECK(lit(30) > 5);
    // The recipe survives the file with its negative start.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("preroll.aseprite");
    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(path)));
    Editor reopened;
    REQUIRE(reopened.openFile(QUrl::fromLocalFile(path)));
    for (int i = 0; i < int(reopened.layers().size()); ++i) {
        reopened.setCurrentLayer(i);
        if (reopened.liveLayer())
            break;
    }
    REQUIRE(reopened.liveLayer());
    CHECK(reopened.particleRecipe().startFrame == -30);
    CHECK(reopened.particleRecipe().untilEnd);
    CHECK(reopened.particleRecipe().perFrame);
}

TEST_CASE("live layer: a collider is a roof from the panel, a line from the field tool, and its corners are edited")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.addParticleLayer();
    const core::ParticleRecipe start = editor.particleRecipe();

    // From the panel: a roof below the emitter.
    const int index = editor.addParticleField(4);
    REQUIRE(index == 0);
    core::ParticleRecipe r = editor.particleRecipe();
    REQUIRE(r.fields.size() == 1);
    CHECK(r.fields[0].type == core::ParticleField::Type::Collider);
    REQUIRE(r.fields[0].points.size() == 3);
    CHECK(r.fields[0].y > start.y);
    CHECK(r.fields[0].points[1].y < r.fields[0].points[0].y); // its middle is the top
    const QVariantList points = editor.particleSettings()["fields"].toList()[0].toMap()["points"].toList();
    REQUIRE(points.size() == 3);
    CHECK(points[1].toPointF().x() == r.fields[0].x); // in canvas pixels

    // A corner moved; one inserted and dragged is one undo step; one removed; never fewer than two.
    editor.setColliderPoint(0, 0, 10.5, 50.5, false);
    r = editor.particleRecipe();
    CHECK(r.fields[0].x + r.fields[0].points[0].x == 10.5);
    CHECK(r.fields[0].y + r.fields[0].points[0].y == 50.5);
    const core::ParticleRecipe beforeInsert = r;
    CHECK(editor.insertColliderPoint(0, 1, 20.5, 40.5) == 1);
    editor.setColliderPoint(0, 1, 22.5, 38.5, true);
    editor.setColliderPoint(0, 1, 24.5, 36.5, true);
    r = editor.particleRecipe();
    REQUIRE(r.fields[0].points.size() == 4);
    CHECK(r.fields[0].x + r.fields[0].points[1].x == 24.5);
    editor.undo();
    CHECK(editor.particleRecipe() == beforeInsert);
    editor.removeColliderPoint(0, 0);
    editor.removeColliderPoint(0, 0);
    CHECK(editor.particleRecipe().fields[0].points.size() == 2);

    // The field tool: a drag draws a straight collider from the press to the release.
    editor.setTool(Editor::FieldTool);
    editor.setLiveFieldKind(4);
    editor.beginStroke({5, 30}, false, {});
    editor.moveStroke({20, 33}, {});
    editor.moveStroke({30, 40}, {});
    editor.endStroke();
    r = editor.particleRecipe();
    REQUIRE(r.fields.size() == 2);
    const core::ParticleField& drawn = r.fields[1];
    CHECK(drawn.type == core::ParticleField::Type::Collider);
    REQUIRE(drawn.points.size() == 2);
    CHECK(drawn.x + drawn.points[0].x == 5.5);
    CHECK(drawn.y + drawn.points[0].y == 30.5);
    CHECK(drawn.x + drawn.points[1].x == 30.5);
    CHECK(drawn.y + drawn.points[1].y == 40.5);
}

TEST_CASE("smear group: Layer > Smear wraps the layer, shows it as drawn while it is painted, and its recipe is undoable")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setPrimaryColor(QColor(255, 255, 255));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({10, 20}, false, {});
    editor.moveStroke({10, 26}, {});
    editor.endStroke();
    const core::Layer* art = editor.document().root().children[0].get();

    REQUIRE(editor.smearLayers());
    const core::Layer* group = editor.document().root().children[0].get();
    REQUIRE(group->isSmear());
    REQUIRE(group->children.size() == 1);
    CHECK(group->children[0].get() == art);
    CHECK(editor.smearGroup() == group); // the group is current
    CHECK(group->smear->pivotX == doctest::Approx(10.5)); // the middle of the line
    CHECK(group->smear->pivotY == doctest::Approx(23.5));
    CHECK_FALSE(group->smearAsDrawn);

    // Posed: a quarter turn about the pivot lays the line flat.
    core::SmearRecipe r = *editor.smearRecipe();
    core::SmearKey k;
    k.pose.rotation = 90;
    r.keys = {k};
    r.trail = 0;
    editor.setSmearRecipe(r, "Pose", false);
    CHECK(editor.composite().pixelColor(7, 23).alpha() == 255);
    CHECK(editor.composite().pixelColor(10, 20).alpha() == 0);

    // Painting the art inside it: it shows as drawn, where the brush is.
    editor.setCurrentLayer(0);
    REQUIRE(editor.smearGroup() == group);
    CHECK(group->smearAsDrawn);
    CHECK(editor.composite().pixelColor(10, 20).alpha() == 255);
    CHECK(editor.frameImage(editor.currentFrame()).pixelColor(10, 20).alpha() == 0); // the preview: posed
    // Back on the group, posed again.
    editor.setCurrentLayer(1);
    CHECK_FALSE(group->smearAsDrawn);
    CHECK(editor.composite().pixelColor(10, 20).alpha() == 0);

    // A drag of the recipe is one step; undo puts the recipe back, then the group goes.
    r.keys[0].pose.rotation = 45;
    editor.setSmearRecipe(r, "Pose", true);
    r.keys[0].pose.rotation = 30;
    editor.setSmearRecipe(r, "Pose", true);
    editor.undo();
    CHECK(editor.smearRecipe()->keys[0].pose.rotation == 90);
    editor.undo();
    editor.undo();
    CHECK_FALSE(editor.document().root().children[0]->isSmear());
    CHECK(editor.document().root().children[0].get() == art);
}

TEST_CASE("smear group: posing on a frame sets its key, the canvas state follows, and keys can be eased or removed")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFramesUpTo(8);
    editor.setCurrentFrame(0);
    editor.setPrimaryColor(QColor(255, 255, 255));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({10, 20}, false, {});
    editor.moveStroke({13, 20}, {});
    editor.endStroke();
    REQUIRE(editor.smearLayers());
    QVariantMap state = editor.smearState();
    REQUIRE(state["current"].toBool());
    CHECK_FALSE(state["key"].toBool());
    CHECK(state["box"].toRectF() == QRectF(10, 20, 4, 1));
    CHECK(state["pivot"].toPointF() == QPointF(12, 20.5));

    // Frame 3: moved 6 right. A key is made there; the drag is one undo step.
    editor.setCurrentFrame(3);
    editor.setSmearPose({{"x", 2}}, true);
    editor.setSmearPose({{"x", 6}}, true);
    core::SmearRecipe r = *editor.smearRecipe();
    REQUIRE(r.keys.size() == 1);
    CHECK(r.keys[0].frame == 3);
    CHECK(r.keys[0].pose.x == 6);
    state = editor.smearState();
    CHECK(state["key"].toBool());
    // (The line is drawn on frame 0 only: here the box is a small one around the pivot.)
    CHECK(state["corners"].toList()[0].toPointF() == state["box"].toRectF().topLeft() + QPointF(6, 0));
    CHECK(state["pivotPosed"].toPointF() == QPointF(18, 20.5));
    editor.undo();
    CHECK(editor.smearRecipe()->keys.empty());
    editor.redo();

    // Frame 7: a quarter turn. Frame 5 is between, tweened; posing there makes a key from that pose.
    editor.setCurrentFrame(7);
    editor.setSmearPose({{"rotation", 90}}, false);
    editor.setCurrentFrame(5);
    state = editor.smearState();
    CHECK_FALSE(state["key"].toBool());
    CHECK(state["pose"].toMap()["rotation"].toDouble() == doctest::Approx(45));
    CHECK(state["pose"].toMap()["x"].toDouble() == doctest::Approx(6)); // the move held from frame 3
    editor.setSmearPose({{"scaleX", 2}}, false);
    r = *editor.smearRecipe();
    REQUIRE(r.keys.size() == 3);
    CHECK(r.keys[1].frame == 5);
    CHECK(r.keys[1].pose.rotation == doctest::Approx(45));
    CHECK(r.keys[1].pose.scaleX == 2);

    // A scale never reaches 0.
    editor.setSmearPose({{"scaleY", 0}}, false);
    CHECK(editor.smearRecipe()->keys[1].pose.scaleY == doctest::Approx(0.05));

    editor.setSmearKeyEase(5, 2);
    CHECK(editor.smearRecipe()->keys[1].ease == core::SmearKey::Ease::Out);
    editor.removeSmearKey(5);
    CHECK(editor.smearRecipe()->keys.size() == 2);

    // The pivot.
    editor.setSmearPivot(11.5, 21, false);
    CHECK(editor.smearRecipe()->pivotX == 11.5);

    // The timeline knows the keys.
    TimelineModel model;
    model.setEditor(&editor);
    const int row = model.rowForLayer(editor.currentLayer());
    CHECK(model.smearKeys(row) == QList<int>{3, 7});
}

TEST_CASE("smear group: the panel's settings change the smear, one undo step each")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    REQUIRE(editor.smearLayers());
    editor.setSmearSetting("trail", 0.5, false);
    editor.setSmearSetting("behind", 0, false);
    editor.setSmearSetting("style", 1, false);
    editor.setSmearSetting("color", QColor(255, 0, 0), false);
    editor.setSmearSetting("taper", 2.0, false); // clamped
    editor.setSmearSetting("fade", 0.25, true);
    editor.setSmearSetting("fade", 0.3, true);
    editor.setSmearSetting("fadePattern", 3, false);
    editor.setSmearSetting("outline", 1, false);
    editor.setSmearSetting("outlineColor", QColor(0, 0, 255), false);
    editor.setSmearSetting("nonsense", 1, false);
    const core::SmearRecipe r = *editor.smearRecipe();
    CHECK(r.trail == 0.5);
    CHECK_FALSE(r.behind);
    CHECK(r.style == core::SmearRecipe::Style::Flat);
    CHECK(r.color == core::Rgba{255, 0, 0, 255});
    CHECK(r.taper == 1);
    CHECK(r.fade == doctest::Approx(0.3));
    CHECK(r.fadePattern == 3);
    CHECK(r.outline);
    CHECK(r.outlineColor == core::Rgba{0, 0, 255, 255});
    const QVariantMap state = editor.smearState();
    CHECK(state["trail"].toDouble() == 0.5);
    CHECK(state["color"].value<QColor>() == QColor(255, 0, 0));
    editor.undo(); // the outline color
    editor.undo(); // the outline
    editor.undo(); // the pattern
    editor.undo(); // the fade, both steps of the drag
    CHECK(editor.smearRecipe()->fade == 0);
}

TEST_CASE("smear group: Bake Layer makes it an image layer with its frames, a preset adds the frames it needs")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setPrimaryColor(QColor(255, 255, 255));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({15, 10}, false, {});
    editor.moveStroke({15, 20}, {});
    editor.endStroke();
    editor.addFramesUpTo(4);
    REQUIRE(editor.smearLayers());

    // A preset on frame 2: Swing needs 10 frames from there, so the sprite gets 12.
    editor.setCurrentFrame(2);
    editor.applySmearPreset("swing");
    CHECK(editor.frameCount() == 12);
    core::SmearRecipe r = *editor.smearRecipe();
    REQUIRE(r.keys.size() == 4);
    CHECK(r.keys.front().frame == 2);
    CHECK(r.keys.back().frame == 11);
    editor.undo();
    CHECK(editor.frameCount() == 4);
    CHECK(editor.smearRecipe()->keys.empty());
    editor.redo();
    CHECK(editor.smearPresetList().size() >= 4);

    std::vector<QImage> shown;
    for (int f = 0; f < editor.frameCount(); ++f)
        shown.push_back(editor.frameImage(f));

    // Bake to New Layer: the group stays, a baked copy above it.
    editor.bakeToNewLayer();
    REQUIRE(editor.document().root().children.size() == 2); // (the sprite's one layer is in the group)
    CHECK(editor.document().root().children[0]->isSmear());
    CHECK(editor.document().root().children[1]->kind == core::LayerKind::Image);
    editor.undo();

    // Bake Layer: the group goes, an image layer with the same frames is in its place.
    editor.bakeLayer();
    REQUIRE(editor.document().root().children.size() == 1);
    const core::Layer& baked = *editor.document().root().children[0];
    CHECK(baked.kind == core::LayerKind::Image);
    CHECK_FALSE(baked.isSmear());
    for (int f = 0; f < editor.frameCount(); ++f)
        CHECK(editor.frameImage(f) == shown[size_t(f)]);
    editor.undo();
    CHECK(editor.document().root().children[0]->isSmear());
}

TEST_CASE("smear group: keys move along the timeline, copy, replace, and their poses copy and paste")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.addFramesUpTo(10);
    REQUIRE(editor.smearLayers());
    const auto pose = [&](int frame, double rotation) {
        editor.setCurrentFrame(frame);
        QVariantMap fields;
        fields["rotation"] = rotation;
        editor.setSmearPose(fields, false);
    };
    pose(1, 10);
    pose(4, 40);
    pose(7, 70);
    const auto frames = [&] {
        const core::SmearRecipe r = *editor.smearRecipe(); // (a copy: looping over a temporary's keys would dangle)
        std::vector<int> out;
        for (const core::SmearKey& k : r.keys)
            out.push_back(k.frame);
        return out;
    };

    editor.moveSmearKey(4, 5, false); // moved
    CHECK(frames() == std::vector<int>{1, 5, 7});
    CHECK(editor.currentFrame() == 5);
    editor.moveSmearKey(5, 2, true); // copied
    CHECK(frames() == std::vector<int>{1, 2, 5, 7});
    CHECK(editor.smearRecipe()->keys[1].pose.rotation == 40);
    editor.moveSmearKey(1, 7, false); // onto a key: it replaces it
    CHECK(frames() == std::vector<int>{2, 5, 7});
    CHECK(editor.smearRecipe()->keys[2].pose.rotation == 10);
    editor.undo();
    CHECK(frames() == std::vector<int>{1, 2, 5, 7});
    CHECK(editor.smearRecipe()->keys[3].pose.rotation == 70);

    // A pose copied from a frame between keys (tweened) and pasted where there is no key.
    CHECK_FALSE(editor.canPasteSmearPose());
    editor.copySmearPose(6); // between 40 at 5 and 70 at 7
    CHECK(editor.canPasteSmearPose());
    editor.pasteSmearPose(9);
    CHECK(frames() == std::vector<int>{1, 2, 5, 7, 9});
    CHECK(editor.smearRecipe()->keys[4].pose.rotation == doctest::Approx(55));

    // Delete: the current frame's key.
    editor.setCurrentFrame(5);
    editor.deleteSelection();
    CHECK(frames() == std::vector<int>{1, 2, 7, 9});
}

TEST_CASE("smear group: moving the pivot keeps the posed art where it is on every key")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFramesUpTo(6);
    REQUIRE(editor.smearLayers());
    const auto setPose = [&](int frame, double rotation, double scale) {
        editor.setCurrentFrame(frame);
        QVariantMap fields;
        fields["rotation"] = rotation;
        fields["scaleX"] = scale;
        fields["skewY"] = 10;
        fields["x"] = 3;
        editor.setSmearPose(fields, false);
    };
    setPose(0, 30, 1.5);
    setPose(5, -70, 0.75);
    const core::SmearRecipe before = *editor.smearRecipe();
    editor.setSmearPivot(4.5, 22, false);
    const core::SmearRecipe after = *editor.smearRecipe();
    CHECK(after.pivotX == 4.5);
    CHECK(after.pivotY == 22);
    // Points of the art land where they did, at both keys.
    for (size_t k = 0; k < 2; ++k)
        for (const auto& [x, y] : {std::pair{10.0, 10.0}, std::pair{14.0, 18.0}, std::pair{0.0, 31.0}}) {
            double bx, by, ax, ay;
            core::smearMapPoint(before, before.keys[k].pose, x, y, bx, by);
            core::smearMapPoint(after, after.keys[k].pose, x, y, ax, ay);
            CHECK(ax == doctest::Approx(bx));
            CHECK(ay == doctest::Approx(by));
        }
}

TEST_CASE("smear group: the path shows on the canvas while there are two keys, smooth when asked")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.addFramesUpTo(5);
    REQUIRE(editor.smearLayers());
    CHECK(editor.smearState()["path"].toList().isEmpty());
    for (const auto& [frame, x, y] : {std::tuple{0, 0, 0}, std::tuple{2, 10, 10}, std::tuple{4, 20, 0}}) {
        editor.setCurrentFrame(frame);
        QVariantMap fields;
        fields["x"] = x;
        fields["y"] = y;
        editor.setSmearPose(fields, false);
    }
    QVariantMap state = editor.smearState();
    CHECK(state["pathDots"].toList().size() == 5); // a dot a frame, from the first key to the last
    CHECK(state["path"].toList().size() == 33);    // 8 points a frame
    const QPointF straight = state["pathDots"].toList()[1].toPointF();
    editor.setSmearSetting("smoothPath", 1, false);
    state = editor.smearState();
    CHECK(state["smoothPath"].toInt() == 1);
    CHECK(state["pathDots"].toList()[1].toPointF() != straight);
    CHECK(state["pathDots"].toList()[2].toPointF() == state["pivot"].toPointF() + QPointF(10, 10)); // through the key
}

TEST_CASE("easing curves: Custom starts from the easing it had, and the curve setters make it custom")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.addParticleLayer();
    editor.setParticleField("sizeEase", 2, false); // Out
    editor.setParticleField("sizeEase", 4, false); // Custom: Out's curve
    CHECK(editor.particleRecipe().sizeCurve == core::namedEasing(2));
    editor.setParticleCurve("opacityCurve", 0.2, 1.5, 0.8, 0.9, true);
    CHECK(editor.particleRecipe().opacityEase == core::ParticleRecipe::Ease::Custom);
    CHECK(editor.particleRecipe().opacityCurve == core::EasingCurve{0.2, 1.5, 0.8, 0.9});
    CHECK(editor.particleSettings()["opacityCurve"].toList().size() == 4);

    editor.addLayer();
    editor.addFramesUpTo(4);
    REQUIRE(editor.smearLayers());
    editor.setCurrentFrame(0);
    editor.setSmearPose({{"x", 1}}, false);
    editor.setCurrentFrame(3);
    editor.setSmearPose({{"x", 5}}, false);
    editor.setSmearKeyEase(0, 3); // In-out
    editor.setSmearKeyEase(0, 4); // Custom: In-out's curve
    CHECK(editor.smearRecipe()->keys[0].curve == core::namedEasing(3));
    editor.setSmearKeyCurve(0, 0.1, 1.2, 0.5, 1, true);
    CHECK(editor.smearRecipe()->keys[0].curve == core::EasingCurve{0.1, 1.2, 0.5, 1});
    CHECK(editor.smearState()["keyList"].toList()[0].toMap()["curve"].toList().size() == 4);
}

TEST_CASE("smear group: its own tools while it is current, and Move moves all of it")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setPrimaryColor(QColor(255, 255, 255));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({10, 10}, false, {});
    editor.moveStroke({10, 16}, {});
    editor.endStroke();
    editor.addFramesUpTo(3);
    REQUIRE(editor.smearLayers());

    // The strip swaps: Pose in hand, painting tools refused, Move allowed.
    CHECK(editor.tool() == Editor::PoseTool);
    editor.setTool(Editor::Pencil);
    CHECK(editor.tool() == Editor::PoseTool);
    editor.setTool(Editor::Move);
    CHECK(editor.tool() == Editor::Move);
    CHECK(Editor::toolUsableOnSmear(Editor::Eyedropper));
    CHECK_FALSE(Editor::toolUsableOnSmear(Editor::Fill));

    // A turned key on frame 2.
    editor.setCurrentFrame(2);
    editor.setSmearPose({{"rotation", 90}}, false);
    const core::SmearRecipe before = *editor.smearRecipe();
    const QImage turned = editor.frameImage(2);

    // Move drags all of it: art, pivot, keys kept, so the turned frame moves exactly by the drag.
    editor.beginStroke({5, 5}, false, {});
    editor.moveStroke({7, 6}, {});
    editor.moveStroke({9, 8}, {});
    editor.endStroke();
    const core::SmearRecipe after = *editor.smearRecipe();
    CHECK(after.pivotX == before.pivotX + 4);
    CHECK(after.pivotY == before.pivotY + 3);
    CHECK(after.keys == before.keys);
    const QImage moved = editor.frameImage(2);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const QColor was = x - 4 >= 0 && y - 3 >= 0 ? turned.pixelColor(x - 4, y - 3) : QColor(0, 0, 0, 0);
            if (moved.pixelColor(x, y) != was)
                FAIL_CHECK("differs at " << x << ", " << y);
        }
    editor.undo(); // the whole drag
    CHECK(*editor.smearRecipe() == before);
    CHECK(editor.frameImage(2) == turned);

    // Inside the group the drawing tools are back; off the group the pencil (it was in hand) returns.
    editor.setCurrentLayer(0); // the art
    editor.setTool(Editor::Pencil);
    CHECK(editor.tool() == Editor::Pencil);
    editor.setCurrentLayer(1); // the group
    CHECK(editor.tool() == Editor::PoseTool);
    editor.setCurrentLayer(0);
    CHECK(editor.tool() == Editor::Pencil);
}

TEST_CASE("smear presets: your own motion is saved, fits other art, can be renamed and deleted; previews play")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto draw = [](Editor& editor, QPoint from, QPoint to) {
        editor.setPrimaryColor(QColor(255, 255, 255));
        editor.setTool(Editor::Pencil);
        editor.beginStroke(from, false, {});
        editor.moveStroke(to, {});
        editor.endStroke();
    };
    QString id;
    {
        Editor editor;
        editor.setPresetFolder(dir.path());
        editor.newSprite({{"width", 32}, {"height", 32}});
        draw(editor, {10, 10}, {10, 17}); // 1 x 8
        editor.addFramesUpTo(6);
        REQUIRE(editor.smearLayers());
        CHECK_FALSE(editor.saveSmearPreset("Nothing")); // no keys yet
        editor.setCurrentFrame(1);
        editor.setSmearPose({{"x", 4}}, false);
        editor.setCurrentFrame(5);
        editor.setSmearPose({{"rotation", 45}, {"x", 8}}, false);
        CHECK(editor.saveSmearPreset("Hop"));
        CHECK(editor.saveSmearPreset("hop")); // the same name: replaces
        int mine = 0;
        for (const QVariant& p : editor.smearPresetList())
            if (p.toMap()["user"].toBool()) {
                ++mine;
                id = p.toMap()["id"].toString();
            }
        CHECK(mine == 1);

        // Previews: a picture a frame, from its first key to its last.
        CHECK(editor.smearPresetPreview(id)["count"].toInt() == 5);
        const QVariantMap spin = editor.smearPresetPreview("spin");
        CHECK(spin["count"].toInt() == 7);
        CHECK_FALSE(editor.smearPresetFrame("spin", 3).isNull());
        CHECK(editor.smearPresetFrame("spin", 3) == editor.smearPresetFrame("spin", 3)); // as often as asked
        CHECK(editor.smearPresetFrame("nonsense", 0).isNull());
        // At rest it shows the frame where the smear covers most: not the first, still.
        CHECK(spin["still"].toInt() > 0);
        CHECK(editor.smearPresetPreview("nonsense").isEmpty());
    }
    {
        // Another sprite, art twice as long, on frame 2: twice as far, frames added.
        Editor editor;
        editor.setPresetFolder(dir.path());
        editor.newSprite({{"width", 64}, {"height", 64}});
        draw(editor, {20, 20}, {20, 35}); // 1 x 16
        editor.addFramesUpTo(3);
        REQUIRE(editor.smearLayers());
        editor.setCurrentFrame(2);
        editor.applySmearPreset(id);
        const core::SmearRecipe r = *editor.smearRecipe();
        REQUIRE(r.keys.size() == 2);
        CHECK(r.keys[0].frame == 2);
        CHECK(r.keys[1].frame == 6);
        CHECK(r.keys[0].pose.x == doctest::Approx(8));
        CHECK(r.keys[1].pose.x == doctest::Approx(16));
        CHECK(r.keys[1].pose.rotation == 45);
        CHECK(editor.frameCount() == 7);

        CHECK(editor.renameSmearPreset(id, "Leap"));
        bool renamed = false;
        for (const QVariant& p : editor.smearPresetList())
            renamed = renamed || (p.toMap()["id"].toString() == id && p.toMap()["name"].toString() == "Leap");
        CHECK(renamed);
        editor.deleteSmearPreset(id);
        for (const QVariant& p : editor.smearPresetList())
            CHECK_FALSE(p.toMap()["user"].toBool());
    }
}

TEST_CASE("smear group: settings reset to their built-in values, the pose on this frame's key, in one step")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.addFramesUpTo(4);
    REQUIRE(editor.smearLayers());
    editor.setCurrentFrame(2);
    QVariantMap fields;
    fields["x"] = 5;
    fields["rotation"] = 30;
    fields["scaleY"] = 2;
    editor.setSmearPose(fields, false);
    editor.setSmearSetting("trail", 0.4, false);
    editor.setSmearSetting("fade", 0.5, false);
    editor.setSmearSetting("smoothPath", 1, false);

    editor.resetSmearSettings({"x", "rotation", "trail", "smoothPath"});
    core::SmearRecipe r = *editor.smearRecipe();
    REQUIRE(r.keys.size() == 1);
    CHECK(r.keys[0].pose.x == 0);
    CHECK(r.keys[0].pose.rotation == 0);
    CHECK(r.keys[0].pose.scaleY == 2); // not asked
    CHECK(r.trail == 1);
    CHECK(r.fade == 0.5);              // not asked
    CHECK_FALSE(r.smoothPath);
    editor.undo(); // all of it at once
    r = *editor.smearRecipe();
    CHECK(r.keys[0].pose.x == 5);
    CHECK(r.trail == doctest::Approx(0.4));
    CHECK(r.smoothPath);
}

TEST_CASE("playback: frames come from a cache that any edit drops")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.addFrame();
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    const auto dot = [&](int frame, QPoint p) {
        editor.setCurrentFrame(frame);
        editor.beginStroke(p, false, {});
        editor.endStroke();
    };
    dot(0, {1, 1});
    dot(1, {2, 2});

    editor.setCurrentFrame(0);
    editor.setPlaying(true);
    for (int loop = 0; loop < 3; ++loop) { // the second and third loops show cached frames
        editor.setCurrentFrame(1);
        CHECK(editor.composite().pixelColor(2, 2).alpha() == 255);
        CHECK(editor.composite().pixelColor(1, 1).alpha() == 0);
        editor.setCurrentFrame(0);
        CHECK(editor.composite().pixelColor(1, 1).alpha() == 255);
        CHECK(editor.composite().pixelColor(2, 2).alpha() == 0);
    }
    editor.setPlaying(false);

    dot(0, {4, 4}); // an edit between two plays
    editor.setCurrentFrame(1);
    editor.setPlaying(true);
    editor.setCurrentFrame(0);
    CHECK(editor.composite().pixelColor(4, 4).alpha() == 255);
    editor.setCurrentFrame(1);
    editor.setCurrentFrame(0);
    CHECK(editor.composite().pixelColor(4, 4).alpha() == 255);
    editor.undo(); // undoing while playing drops the cache too
    editor.setCurrentFrame(1);
    editor.setCurrentFrame(0);
    CHECK(editor.composite().pixelColor(4, 4).alpha() == 0);
}
