#include <doctest/doctest.h>

#include "BrushLibrary.h"
#include "Editor.h"

#include <QTemporaryDir>

namespace {

void click(Editor& editor, QPoint p)
{
    editor.beginStroke(p, false);
    editor.endStroke();
}

QImage dotImage(int w, int h, QColor color)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    image.setPixelColor(w / 2, h / 2, color);
    return image;
}

} // namespace

TEST_CASE("brush library: save, find, remove, and read back")
{
    QTemporaryDir dir;
    const QString folder = dir.filePath("brushes"); // made on the first save
    BrushLibrary library(folder);
    CHECK(library.brushes().isEmpty());

    const QImage red = dotImage(3, 3, QColor(255, 0, 0));
    const QImage blue = dotImage(5, 2, QColor(0, 0, 255));
    REQUIRE(library.add(red));
    CHECK_FALSE(library.add(red)); // the same pixels once
    REQUIRE(library.add(blue));
    REQUIRE(library.brushes().size() == 2);
    CHECK(library.contains(blue));
    // Oldest first, read back from the files by a new library.
    BrushLibrary again(folder);
    REQUIRE(again.brushes().size() == 2);
    const QVariantMap first = again.brushes()[0].toMap();
    CHECK(first.value("width").toInt() == 3);
    CHECK(again.image(first.value("path").toString()) == red);
    CHECK(again.image("/not/a/brush.png").isNull());

    CHECK(again.remove(first.value("path").toString()));
    CHECK(again.brushes().size() == 1);
    CHECK_FALSE(again.contains(red));
    CHECK_FALSE(again.remove("/somewhere/else.png")); // only its own files
}

TEST_CASE("custom brush: flip and scale")
{
    Editor editor;
    QImage two(2, 1, QImage::Format_RGBA8888);
    two.setPixelColor(0, 0, QColor(255, 0, 0));
    two.setPixelColor(1, 0, QColor(0, 0, 255));
    REQUIRE(editor.setCustomBrush(two));
    editor.flipCustomBrush(true);
    CHECK(editor.customBrushImage().pixelColor(0, 0) == QColor(0, 0, 255));
    CHECK(editor.customBrushImage().pixelColor(1, 0) == QColor(255, 0, 0));
    editor.flipCustomBrush(false); // one row: the same
    CHECK(editor.customBrushImage().pixelColor(0, 0) == QColor(0, 0, 255));

    editor.setCustomBrushScale(1); // x2
    CHECK(editor.customBrushScaleText() == "×2");
    CHECK(editor.customBrushSize() == QSize(4, 2));
    CHECK(editor.customBrushImage().pixelColor(1, 1) == QColor(0, 0, 255));
    CHECK(editor.customBrushImage().pixelColor(2, 0) == QColor(255, 0, 0));
    editor.setCustomBrushScale(-1); // half of the source
    CHECK(editor.customBrushScaleText() == "÷2");
    CHECK(editor.customBrushSize() == QSize(1, 1));
    editor.setCustomBrushScale(1); // back up: nothing was lost
    CHECK(editor.customBrushSize() == QSize(4, 2));
    CHECK(editor.customBrushImage().pixelColor(2, 0) == QColor(255, 0, 0));

    CHECK(editor.customBrushEdited());
    editor.resetCustomBrush();
    CHECK_FALSE(editor.customBrushEdited());
    CHECK(editor.customBrushSize() == QSize(2, 1));
    CHECK(editor.customBrushImage().pixelColor(0, 0) == QColor(255, 0, 0)); // not flipped

    editor.setCustomBrushScale(16); // x17
    CHECK(editor.customBrushSize() == QSize(34, 17));
}

TEST_CASE("custom brush: blend lays semi-transparent pixels over the cel")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}});
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    QImage half(1, 1, QImage::Format_RGBA8888);
    half.setPixelColor(0, 0, QColor(255, 0, 0, 128));
    REQUIRE(editor.setCustomBrush(half));
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(1, 1) == QColor(255, 0, 0, 128)); // replaced
    editor.undo();
    editor.setBrushBlend(true);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    const QColor mixed = editor.composite().pixelColor(1, 1);
    CHECK(mixed.alpha() == 255);
    CHECK(mixed.red() > 100);
    CHECK(mixed.blue() > 100);
}

TEST_CASE("brush library: a saved brush becomes the custom brush, trimmed")
{
    Editor editor;
    CHECK_FALSE(editor.setCustomBrush(QImage()));
    QImage empty(4, 4, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    CHECK_FALSE(editor.setCustomBrush(empty));

    REQUIRE(editor.setCustomBrush(dotImage(5, 5, QColor(0, 255, 0))));
    CHECK(editor.hasCustomBrush());
    CHECK(editor.customBrushSize() == QSize(1, 1));
    CHECK(editor.brushShape() == Editor::CustomBrush);
    CHECK(editor.customBrushImage().pixelColor(0, 0) == QColor(0, 255, 0));

    QImage huge(Editor::kMaxCustomBrush + 1, 1, QImage::Format_RGBA8888);
    huge.fill(QColor(255, 255, 255));
    CHECK_FALSE(editor.setCustomBrush(huge));
    CHECK(editor.customBrushSize() == QSize(1, 1)); // the old one stays
}

TEST_CASE("brush angle: one setting turns the square and custom brushes, not the round one")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.setTool(Editor::Pencil);
    editor.setBrushSize(5);
    CHECK(editor.brushAngle() == 0); // as drawn at first
    CHECK(editor.brushExtent() == QSize(5, 5));
    const qint64 plainKey = editor.brushKey();

    // A square turned 45 degrees is a diamond in a bigger box; the preview follows.
    editor.setBrushAngle(45);
    CHECK(editor.brushAngle() == 45);
    CHECK(editor.brushExtent().width() > 5);
    CHECK(editor.brushKey() != plainKey);
    const QImage preview = editor.brushImage();
    CHECK(preview.pixelColor(0, 0).alpha() == 0); // a corner of the box is empty
    CHECK(preview.pixelColor(preview.width() / 2, preview.height() / 2).alpha() > 0);

    // What it paints is the same shape, around the click.
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({8, 8}, false);
    editor.endStroke();
    CHECK(editor.composite().pixelColor(8, 8) == QColor(255, 0, 0));
    // The painted pixels are the previewed ones, and not the plain square's corners.
    int painted = 0, previewed = 0;
    const QImage shown = editor.composite();
    for (int y = 0; y < shown.height(); ++y)
        for (int x = 0; x < shown.width(); ++x)
            painted += shown.pixelColor(x, y).alpha() > 0;
    for (int y = 0; y < preview.height(); ++y)
        for (int x = 0; x < preview.width(); ++x)
            previewed += preview.pixelColor(x, y).alpha() > 0;
    CHECK(painted == previewed);
    int emptyCorners = 0;
    for (const QPoint c : {QPoint(6, 6), QPoint(10, 6), QPoint(6, 10), QPoint(10, 10)})
        emptyCorners += shown.pixelColor(c).alpha() == 0;
    CHECK(emptyCorners >= 1);

    // Angles wrap into -180..180.
    editor.setBrushAngle(270);
    CHECK(editor.brushAngle() == -90);
    editor.setBrushAngle(-540);
    CHECK(editor.brushAngle() == 180);

    // A round brush looks the same turned: nothing changes for it.
    editor.setBrushShape(Editor::RoundBrush);
    editor.setBrushAngle(45);
    editor.setBrushShape(Editor::RoundBrush);
    CHECK(editor.brushExtent() == QSize(5, 5));
}

TEST_CASE("brush angle: a custom brush turns a quarter at 90 degrees")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    QImage bar(5, 1, QImage::Format_RGBA8888); // a horizontal bar
    bar.fill(QColor(0, 255, 0));
    REQUIRE(editor.setCustomBrush(bar));
    CHECK(editor.brushExtent() == QSize(5, 1));
    editor.setBrushAngle(90);
    CHECK(editor.brushExtent() == QSize(1, 5)); // now a vertical bar
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({8, 8}, false);
    editor.endStroke();
    CHECK(editor.composite().pixelColor(8, 6).alpha() > 0);
    CHECK(editor.composite().pixelColor(6, 8).alpha() == 0);
}

TEST_CASE("brush angle: blur and jumble take the turned shape")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 8; ++x)
            click(editor, {x, y});
    editor.setBrushSize(5);
    editor.setBrushAngle(45);
    CHECK(editor.brushImage().pixelColor(0, 0).alpha() == 0); // the soft brushes show the turned shape too
    editor.setTool(Editor::Blur);
    CHECK(editor.brushExtent().width() > 5);
    click(editor, {8, 8});
}
