#include <doctest/doctest.h>

#include "Editor.h"

#include <QClipboard>
#include <QGuiApplication>

namespace {

const QColor kRed(255, 0, 0);

bool painted(const Editor& editor, int x, int y) { return editor.composite().pixelColor(x, y).alpha() > 0; }

void click(Editor& editor, QPoint p) { editor.beginStroke(p, false); editor.endStroke(); }

void drag(Editor& editor, QPoint from, QPoint to)
{
    editor.beginStroke(from, false);
    editor.moveStroke(to);
    editor.endStroke();
}

struct Canvas {
    Editor editor;
    Canvas()
    {
        editor.newDocument(16, 16);
        editor.setPrimaryColor(kRed);
        click(editor, {3, 3});
    }
    void select(QRect r)
    {
        editor.setTool(Editor::Select);
        drag(editor, r.topLeft(), r.bottomRight());
    }
};

} // namespace

TEST_CASE("clipboard: copy, paste floats, move, place")
{
    Canvas c;
    c.select({2, 2, 3, 3});
    c.editor.copy();
    CHECK(c.editor.canPaste());
    c.editor.deselect();

    c.editor.paste();
    CHECK(c.editor.floating());
    CHECK(c.editor.tool() == Editor::Select); // select / move stay; others switch to move
    CHECK(c.editor.selection() == QRect(2, 2, 3, 3)); // back where it came from
    CHECK(c.editor.overlay().pixelColor(3, 3) == kRed);

    // Dragging inside the selection moves the paste, not the document.
    drag(c.editor, {3, 3}, {9, 3});
    CHECK(c.editor.floating());
    CHECK(c.editor.selection() == QRect(8, 2, 3, 3));
    CHECK_FALSE(painted(c.editor, 9, 3));
    CHECK(c.editor.overlay().pixelColor(9, 3) == kRed);

    c.editor.commitFloating();
    CHECK_FALSE(c.editor.floating());
    CHECK(painted(c.editor, 9, 3));
    CHECK(painted(c.editor, 3, 3)); // a copy, the original stays
    CHECK(c.editor.overlay().pixelColor(9, 3).alpha() == 0);
    c.editor.undo();
    CHECK_FALSE(painted(c.editor, 9, 3));
    CHECK(painted(c.editor, 3, 3));
}

TEST_CASE("clipboard: cut, and what places or drops a paste")
{
    Canvas c;
    c.select({2, 2, 3, 3});
    c.editor.cut();
    CHECK_FALSE(painted(c.editor, 3, 3));

    // Undo drops a paste first (the document is untouched by it).
    c.editor.paste();
    REQUIRE(c.editor.floating());
    c.editor.undo();
    CHECK_FALSE(c.editor.floating());
    CHECK_FALSE(painted(c.editor, 3, 3)); // the cut is still in place

    // Drawing places it.
    c.editor.paste();
    c.editor.setTool(Editor::Pencil);
    CHECK_FALSE(c.editor.floating()); // switching to a drawing tool places it
    CHECK(painted(c.editor, 3, 3));

    // A click on the paste with the select tool places it too.
    c.editor.setTool(Editor::Select);
    c.editor.paste();
    click(c.editor, {3, 3});
    CHECK_FALSE(c.editor.floating());

    // Delete drops it.
    c.editor.paste();
    c.editor.deleteSelection();
    CHECK_FALSE(c.editor.floating());
}

TEST_CASE("clipboard: images from elsewhere are centered and converted")
{
    QImage blue(2, 2, QImage::Format_RGBA8888);
    blue.fill(QColor(0, 0, 255));
    QGuiApplication::clipboard()->setImage(blue);

    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}, {"colorMode", "indexed"}});
    editor.paste();
    REQUIRE(editor.floating());
    CHECK(editor.tool() == Editor::Move);
    CHECK(editor.selection() == QRect(7, 7, 2, 2));
    editor.commitFloating();
    // Nearest palette color in the indexed sprite (PICO-8 blue).
    CHECK(editor.composite().pixelColor(7, 7) == QColor(0x29, 0xAD, 0xFF));
}

TEST_CASE("clipboard: the size of the image on it, for the New Sprite dialog")
{
    Editor editor;
    QImage image(37, 21, QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30));
    QGuiApplication::clipboard()->setImage(image);
    CHECK(editor.clipboardImageSize() == QSize(37, 21));

    // Too big for a sprite (the dialog's limit is 4096): none.
    QGuiApplication::clipboard()->setImage(QImage(5000, 10, QImage::Format_RGBA8888));
    CHECK_FALSE(editor.clipboardImageSize().isValid());

    QGuiApplication::clipboard()->clear();
    CHECK_FALSE(editor.clipboardImageSize().isValid());
}

TEST_CASE("clipboard: lifted pixels are told apart from a paste")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({2, 2}, false);
    editor.endStroke();
    editor.selectAll();
    editor.startTransform();
    REQUIRE(editor.floating());
    CHECK(editor.floatingLifted()); // Esc lands these
    editor.commitFloating();
    CHECK_FALSE(editor.floatingLifted());

    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(QColor(0, 0, 255));
    QGuiApplication::clipboard()->setImage(image);
    editor.paste();
    REQUIRE(editor.floating());
    CHECK_FALSE(editor.floatingLifted()); // Esc drops a paste
    editor.cancelFloating();
}

TEST_CASE("clipboard: timeline frames and cels")
{
    Editor editor;
    editor.newDocument(8, 4);
    editor.setPrimaryColor(kRed);
    for (int f = 0; f < 3; ++f) {
        if (f > 0)
            editor.addFrame();
        click(editor, {f, 0});
    }
    auto marker = [&](int f) {
        editor.setCurrentFrame(f);
        for (int x = 0; x < 8; ++x)
            if (painted(editor, x, 0))
                return x;
        return -1;
    };

    editor.selectFrames(0, 1);
    editor.copyTimeline();
    CHECK(editor.canPasteTimeline());
    editor.clearRange();
    editor.setCurrentFrame(2);
    editor.pasteTimeline(); // after frame 3
    REQUIRE(editor.frameCount() == 5);
    CHECK(marker(3) == 0);
    CHECK(marker(4) == 1);
    CHECK(editor.range() == QRect(3, 0, 2, 1));
    editor.undo();
    CHECK(editor.frameCount() == 3);

    editor.selectFrames(1, 1);
    editor.cutTimeline();
    CHECK(editor.frameCount() == 2);
    CHECK(marker(1) == 2);

    // Cels: a block lands with its top-left on the current cel.
    editor.addLayer(); // layer 1
    editor.selectCels(0, 0, 0, 0);
    editor.copyTimeline();
    editor.clearRange();
    editor.setCurrentLayer(0);
    editor.setCurrentFrame(1);
    editor.pasteTimeline();
    CHECK(marker(1) == 0); // frame 2 now shows frame 1's cel content
    const auto& layer = *editor.layerRows()[0];
    CHECK(layer.cel(1) != layer.cel(0)); // a copy
}

TEST_CASE("clipboard: copy merged, paste as a new layer or sprite")
{
    Editor editor;
    editor.newDocument(8, 4);
    editor.setPrimaryColor(kRed);
    click(editor, {1, 1});
    editor.addLayer();
    editor.setPrimaryColor(QColor(0, 0, 255));
    click(editor, {2, 1});

    // Copy takes the current layer only; copy merged takes both.
    editor.copy();
    QImage copied = QGuiApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
    CHECK(copied.pixelColor(1, 1).alpha() == 0);
    editor.copyMerged();
    copied = QGuiApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
    CHECK(copied.pixelColor(1, 1) == kRed);
    CHECK(copied.pixelColor(2, 1) == QColor(0, 0, 255));

    // As a new layer: a third layer, floating until it lands.
    const int layers = int(editor.layers().size());
    REQUIRE(editor.pasteAsNewLayer());
    CHECK(int(editor.layers().size()) == layers + 1);
    CHECK(editor.layers()[editor.currentLayer()].toMap().value("name").toString() == "Pasted");
    CHECK(editor.floating());
    editor.commitFloating();
    CHECK(editor.composite().pixelColor(1, 1) == kRed);

    // As a new sprite: a tab of the image's size, unmodified.
    const int tabs = int(editor.documents().size());
    REQUIRE(editor.pasteAsNewSprite());
    CHECK(int(editor.documents().size()) == tabs + 1);
    CHECK(editor.canvasWidth() == 8);
    CHECK(editor.canvasHeight() == 4);
    CHECK(editor.composite().pixelColor(2, 1) == QColor(0, 0, 255));
    CHECK_FALSE(editor.canUndo());

    QGuiApplication::clipboard()->clear();
    CHECK_FALSE(editor.pasteAsNewLayer());
    CHECK_FALSE(editor.pasteAsNewSprite());
}
