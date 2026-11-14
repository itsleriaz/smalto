#include <doctest/doctest.h>

#include "Editor.h"

namespace {

int countPainted(const QImage& image, QRect area = {})
{
    if (area.isNull())
        area = image.rect();
    int n = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            n += image.pixelColor(x, y).alpha() > 0;
    return n;
}

// A pen press (and release) at p with the given pressure.
void dab(Editor& editor, QPoint p, double pressure, bool secondary = false)
{
    editor.setPenState(true, pressure);
    editor.beginStroke(p, secondary);
    editor.endStroke();
}

void setup(Editor& editor, int size = 9)
{
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setTool(Editor::Pencil);
    editor.setBrushSize(size);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setSecondaryColor(QColor(0, 0, 255));
}

} // namespace

TEST_CASE("pen pressure: the brush size follows it, a mouse paints at full size")
{
    Editor editor;
    setup(editor);
    CHECK(editor.pressureSize()); // on by default: it does nothing without a pen

    editor.setPenState(false, 0);
    editor.beginStroke({16, 16}, false);
    editor.endStroke();
    CHECK(countPainted(editor.composite()) == 81);
    editor.undo();

    dab(editor, {16, 16}, 1);
    CHECK(countPainted(editor.composite()) == 81);
    editor.undo();
    dab(editor, {16, 16}, 0);
    CHECK(countPainted(editor.composite()) == 1);
    editor.undo();
    dab(editor, {16, 16}, 0.5);
    CHECK(countPainted(editor.composite()) == 25);
    editor.undo();

    editor.setPressureMinSize(100);
    dab(editor, {16, 16}, 0);
    CHECK(countPainted(editor.composite()) == 81);
    editor.undo();
    editor.setPressureMinSize(0);

    editor.setPressureSize(false);
    dab(editor, {16, 16}, 0);
    CHECK(countPainted(editor.composite()) == 81);
}

TEST_CASE("pen pressure: it changes smoothly along a drag, in one undo step")
{
    Editor editor;
    setup(editor);
    editor.setPenState(true, 0);
    editor.beginStroke({4, 16}, false);
    editor.setPenState(true, 1);
    editor.moveStroke({28, 16});
    editor.endStroke();
    const QImage image = editor.composite();
    // Thin where the press was light, the full 9 px where it was firm.
    CHECK(countPainted(image, QRect(5, 0, 1, 32)) <= 3);
    CHECK(countPainted(image, QRect(28, 0, 1, 32)) == 9);
    CHECK(countPainted(image, QRect(16, 0, 1, 32)) > 3);
    CHECK(countPainted(image, QRect(16, 0, 1, 32)) < 9);
    editor.undo();
    CHECK(countPainted(editor.composite()) == 0);
}

TEST_CASE("pen pressure: opacity, smooth or dithered; the firmest press in a stroke wins")
{
    Editor editor;
    setup(editor, 1);
    editor.setPressureOpacity(true);
    dab(editor, {4, 4}, 0.5);
    QColor c = editor.composite().pixelColor(4, 4);
    CHECK(c.red() == 255);
    CHECK(c.alpha() == doctest::Approx(128).epsilon(0.02));

    // Over the same pixel lighter, then firmer, then lighter again in one stroke: the firmest stays.
    editor.setPenState(true, 0.2);
    editor.beginStroke({8, 4}, false);
    editor.setPenState(true, 0.8);
    editor.moveStroke({9, 4});
    editor.moveStroke({8, 4});
    editor.setPenState(true, 0.1);
    editor.moveStroke({9, 4});
    editor.moveStroke({8, 4});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(8, 4).alpha() == doctest::Approx(204).epsilon(0.03));

    // Over an opaque color it blends (from what was there before the stroke).
    editor.setPressureOpacity(false);
    editor.setPrimaryColor(QColor(0, 0, 255));
    dab(editor, {12, 4}, 1);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setPressureOpacity(true);
    dab(editor, {12, 4}, 0.5);
    c = editor.composite().pixelColor(12, 4);
    CHECK(c.alpha() == 255);
    CHECK(c.red() == doctest::Approx(128).epsilon(0.03));
    CHECK(c.blue() == doctest::Approx(127).epsilon(0.03));

    // Dithered: whole pixels, about half of them at half pressure.
    editor.setBrushSize(8);
    editor.setPressureSize(false);
    editor.setPressureDither(2); // Bayer 4 x 4
    dab(editor, {24, 24}, 0.5);
    const QImage image = editor.composite();
    const QRect area(20, 20, 8, 8);
    int painted = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const int a = image.pixelColor(x, y).alpha();
            CHECK((a == 0 || a == 255));
            painted += a > 0;
        }
    CHECK(painted == 32);
}

TEST_CASE("pen pressure: the color runs from the secondary (light) to the primary (firm)")
{
    Editor editor;
    setup(editor, 1);
    editor.setPressureSize(false);
    editor.setPressureColor(true);
    dab(editor, {2, 2}, 1);
    CHECK(editor.composite().pixelColor(2, 2) == QColor(255, 0, 0));
    dab(editor, {4, 2}, 0);
    CHECK(editor.composite().pixelColor(4, 2) == QColor(0, 0, 255));
    dab(editor, {6, 2}, 0.5);
    const QColor mid = editor.composite().pixelColor(6, 2);
    CHECK(mid.red() == doctest::Approx(128).epsilon(0.03));
    CHECK(mid.blue() == doctest::Approx(127).epsilon(0.03));
    // The other button swaps the ends.
    dab(editor, {8, 2}, 1, true);
    CHECK(editor.composite().pixelColor(8, 2) == QColor(0, 0, 255));

    // Dithered: only the two colors, about half each at half pressure.
    editor.setBrushSize(8);
    editor.setPressureDither(2);
    dab(editor, {20, 20}, 0.5);
    const QImage image = editor.composite();
    int red = 0, blue = 0;
    for (int y = 16; y < 24; ++y)
        for (int x = 16; x < 24; ++x) {
            const QColor c = image.pixelColor(x, y);
            red += c == QColor(255, 0, 0);
            blue += c == QColor(0, 0, 255);
        }
    CHECK(red + blue == 64);
    CHECK(red == 32);

    // A mouse paints the primary color, as without pressure.
    editor.setPenState(false, 0);
    editor.beginStroke({4, 28}, false);
    editor.endStroke();
    CHECK(editor.composite().pixelColor(4, 28) == QColor(255, 0, 0));
}

TEST_CASE("pen pressure: the eraser takes away part of the alpha")
{
    Editor editor;
    setup(editor, 1);
    editor.setPenState(false, 1);
    editor.beginStroke({3, 3}, false);
    editor.endStroke();
    editor.setTool(Editor::Eraser);
    editor.setPressureOpacity(true);
    dab(editor, {3, 3}, 0.5);
    const QColor c = editor.composite().pixelColor(3, 3);
    CHECK(c.red() == 255);
    CHECK(c.alpha() == doctest::Approx(127).epsilon(0.03));
    dab(editor, {3, 3}, 1);
    CHECK(editor.composite().pixelColor(3, 3).alpha() == 0);
}

TEST_CASE("pen tilt turns the brush the way the pen leans")
{
    Editor editor;
    setup(editor, 5);
    editor.setPressureSize(false);

    // The brush turned 45 degrees by its angle, painted with a mouse: the reference shape.
    editor.setBrushAngle(45);
    editor.setPenState(false, 1);
    editor.beginStroke({16, 16}, false);
    editor.endStroke();
    const QImage turned = editor.composite();
    editor.undo();
    editor.setBrushAngle(0);

    // Tilt off: the pen paints the plain square.
    editor.setPenState(true, 1, 30, 30);
    editor.beginStroke({16, 16}, false);
    editor.endStroke();
    CHECK(countPainted(editor.composite()) == 25);
    editor.undo();

    // On: leaning toward the bottom right turns it 45 degrees.
    editor.setTiltAngle(true);
    editor.setPenState(true, 1, 30, 30);
    editor.beginStroke({16, 16}, false);
    editor.endStroke();
    CHECK(editor.composite() == turned);
    editor.undo();

    // On top of the brush's own angle: leaning right (0) with the angle at 45 is the same.
    editor.setBrushAngle(45);
    editor.setPenState(true, 1, 40, 0);
    editor.beginStroke({16, 16}, false);
    editor.endStroke();
    CHECK(editor.composite() == turned);
}

TEST_CASE("pen pressure: a custom brush, in its own colors or in one color")
{
    Editor editor;
    setup(editor, 1);
    editor.setPressureSize(false);
    QImage two(2, 1, QImage::Format_RGBA8888);
    two.setPixelColor(0, 0, QColor(0, 255, 0));
    two.setPixelColor(1, 0, QColor(255, 255, 0));
    REQUIRE(editor.setCustomBrush(two));
    REQUIRE(editor.brushColors()); // its own colors
    const QPoint at(10, 10);       // the brush covers (9, 10) and (10, 10)

    // Opacity: its own colors, fainter.
    editor.setPressureOpacity(true);
    dab(editor, at, 0.5);
    QColor left = editor.composite().pixelColor(9, 10), right = editor.composite().pixelColor(10, 10);
    CHECK(left.green() == 255);
    CHECK(left.alpha() == doctest::Approx(128).epsilon(0.02));
    CHECK(right.red() == 255);
    CHECK(right.alpha() == doctest::Approx(128).epsilon(0.02));
    editor.undo();
    dab(editor, at, 1);
    CHECK(editor.composite().pixelColor(9, 10) == QColor(0, 255, 0));
    editor.undo();

    // Color: light is the secondary color, firm the brush's own colors.
    editor.setPressureOpacity(false);
    editor.setPressureColor(true);
    dab(editor, at, 0);
    CHECK(editor.composite().pixelColor(9, 10) == QColor(0, 0, 255));
    CHECK(editor.composite().pixelColor(10, 10) == QColor(0, 0, 255));
    editor.undo();
    dab(editor, at, 1);
    CHECK(editor.composite().pixelColor(9, 10) == QColor(0, 255, 0));
    CHECK(editor.composite().pixelColor(10, 10) == QColor(255, 255, 0));
    editor.undo();

    // Its shape in one color: the color runs from the secondary to the primary, as with the pencil.
    editor.setBrushColors(false);
    dab(editor, at, 1);
    CHECK(editor.composite().pixelColor(9, 10) == QColor(255, 0, 0));
    CHECK(editor.composite().pixelColor(10, 10) == QColor(255, 0, 0));
    editor.undo();
    dab(editor, at, 0);
    CHECK(editor.composite().pixelColor(9, 10) == QColor(0, 0, 255));
}

TEST_CASE("pen tilt turns the brush preview while a pen hovers")
{
    Editor editor;
    setup(editor, 5);
    const qint64 plain = editor.brushKey();
    editor.setPenState(true, 0, 30, 30); // tilt off: nothing turns
    CHECK(editor.brushKey() == plain);

    editor.setTiltAngle(true);
    editor.setPenState(true, 0, 30, 30);
    CHECK(editor.brushKey() != plain);
    CHECK(editor.brushExtent().width() > 5);
    CHECK(editor.brushAngle() == 0); // the setting itself stays

    editor.setPenState(false, 1); // a mouse: as set
    CHECK(editor.brushKey() == plain);
    CHECK(editor.brushExtent() == QSize(5, 5));
}
