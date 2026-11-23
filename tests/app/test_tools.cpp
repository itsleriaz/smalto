#include <doctest/doctest.h>

#include "Editor.h"
#include "core/Raster.h"

#include <set>
#include "ColorModeHelper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

const QColor kRed(255, 0, 0);

bool painted(const Editor& editor, int x, int y) { return editor.composite().pixelColor(x, y).alpha() > 0; }

int paintedCount(const Editor& editor)
{
    int n = 0;
    for (int y = 0; y < editor.canvasHeight(); ++y)
        for (int x = 0; x < editor.canvasWidth(); ++x)
            n += painted(editor, x, y);
    return n;
}

void drag(Editor& editor, std::initializer_list<QPoint> points, Qt::KeyboardModifiers mods = {})
{
    auto it = points.begin();
    editor.beginStroke(*it, false, mods);
    for (++it; it != points.end(); ++it)
        editor.moveStroke(*it, mods);
    editor.endStroke();
}

struct Fixture {
    Editor editor;
    Fixture()
    {
        editor.newDocument(16, 16);
        editor.setPrimaryColor(kRed);
    }
};

} // namespace

TEST_CASE("tools: line, with Shift snapping")
{
    Fixture f;
    f.editor.setTool(Editor::Line);
    drag(f.editor, {{1, 1}, {3, 6}, {6, 3}});
    CHECK(painted(f.editor, 1, 1));
    CHECK(painted(f.editor, 6, 3));
    CHECK_FALSE(painted(f.editor, 3, 6)); // the preview moved on
    f.editor.undo();
    CHECK(paintedCount(f.editor) == 0);

    drag(f.editor, {{1, 1}, {9, 3}}, Qt::ShiftModifier); // snaps horizontal
    CHECK(painted(f.editor, 9, 1));
    CHECK_FALSE(painted(f.editor, 9, 3));
    CHECK(paintedCount(f.editor) == 9);
}

TEST_CASE("tools: rectangle and ellipse previews, fill and modifiers")
{
    Fixture f;
    f.editor.setTool(Editor::Rectangle);
    drag(f.editor, {{2, 2}, {10, 10}, {5, 6}});
    CHECK(paintedCount(f.editor) == 2 * 4 + 2 * 3); // 4x5 outline only
    CHECK_FALSE(painted(f.editor, 10, 10));
    CHECK_FALSE(painted(f.editor, 3, 3)); // hollow
    f.editor.undo();
    CHECK(paintedCount(f.editor) == 0);
    CHECK_FALSE(f.editor.canUndo()); // one stroke, one undo step

    f.editor.setFillShapes(true);
    drag(f.editor, {{2, 2}, {5, 6}});
    CHECK(paintedCount(f.editor) == 4 * 5);
    f.editor.undo();

    // Shift pressed mid-drag turns it into a square.
    f.editor.beginStroke({2, 2}, false);
    f.editor.moveStroke({5, 8});
    f.editor.setStrokeModifiers(Qt::ShiftModifier);
    f.editor.endStroke();
    CHECK(paintedCount(f.editor) == 7 * 7);
    f.editor.undo();

    // Alt: from the center.
    f.editor.setTool(Editor::Ellipse);
    drag(f.editor, {{8, 8}, {11, 10}}, Qt::AltModifier);
    CHECK(painted(f.editor, 5, 8));
    CHECK(painted(f.editor, 11, 8));
    CHECK(painted(f.editor, 8, 6));
    CHECK_FALSE(painted(f.editor, 4, 8));
}

TEST_CASE("tools: pixel-perfect pencil")
{
    // A staircase drawn as small 4-connected steps.
    const std::initializer_list<QPoint> steps{{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 2}};
    Fixture on;
    on.editor.setTool(Editor::Pencil);
    REQUIRE(on.editor.pixelPerfect());
    drag(on.editor, steps);
    CHECK_FALSE(painted(on.editor, 1, 0));
    CHECK_FALSE(painted(on.editor, 2, 1));
    CHECK(painted(on.editor, 1, 1));
    CHECK(paintedCount(on.editor) == 4);

    Fixture off;
    off.editor.setPixelPerfect(false);
    drag(off.editor, steps);
    CHECK(paintedCount(off.editor) == 6);

    // Only for 1px brushes.
    Fixture big;
    big.editor.setBrushSize(2);
    drag(big.editor, steps);
    CHECK(painted(big.editor, 1, 0));
}

TEST_CASE("tools: selection rect, deselect and clipping")
{
    Fixture f;
    QSignalSpy changed(&f.editor, &Editor::selectionChanged);
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{3, 3}, {6, 5}});
    CHECK(f.editor.selection() == QRect(3, 3, 4, 3));
    // Shift pressed during the drag: a square (held at the start it adds).
    f.editor.beginStroke({1, 1}, false, {});
    f.editor.moveStroke({4, 2}, Qt::ShiftModifier);
    f.editor.endStroke();
    CHECK(f.editor.selection() == QRect(1, 1, 4, 4));
    drag(f.editor, {{0, 0}, {40, 40}}); // clipped to the canvas
    CHECK(f.editor.selection() == QRect(0, 0, 16, 16));
    drag(f.editor, {{5, 5}}); // click
    CHECK_FALSE(f.editor.hasSelection());
    CHECK(changed.count() == 4);

    f.editor.setTool(Editor::Select);
    drag(f.editor, {{4, 4}, {7, 7}});
    f.editor.setTool(Editor::Line);
    drag(f.editor, {{0, 5}, {15, 5}});
    CHECK(paintedCount(f.editor) == 4); // only inside
    CHECK(painted(f.editor, 4, 5));
    CHECK_FALSE(painted(f.editor, 3, 5));

    // The line splits the selection; the fill stays below it and inside.
    f.editor.setTool(Editor::Fill);
    drag(f.editor, {{6, 6}});
    CHECK(paintedCount(f.editor) == 4 + 2 * 4);
    CHECK_FALSE(painted(f.editor, 5, 4));
    drag(f.editor, {{0, 0}}); // outside: nothing
    CHECK(paintedCount(f.editor) == 12);

    f.editor.deleteSelection();
    CHECK(paintedCount(f.editor) == 0);
    f.editor.undo();
    CHECK(paintedCount(f.editor) == 12);
    f.editor.selectAll();
    CHECK(f.editor.selection() == QRect(0, 0, 16, 16));
}

TEST_CASE("tools: Delete with nothing selected clears the whole cel")
{
    Fixture f;
    f.editor.setTool(Editor::Pencil);
    drag(f.editor, {{3, 3}});
    drag(f.editor, {{10, 10}});
    REQUIRE(paintedCount(f.editor) == 2);
    REQUIRE_FALSE(f.editor.hasSelection());
    f.editor.deleteSelection();
    CHECK(paintedCount(f.editor) == 0);
    f.editor.undo();
    CHECK(paintedCount(f.editor) == 2);
}

TEST_CASE("tools: move selected pixels and whole cels")
{
    Fixture f;
    f.editor.setTool(Editor::Pencil);
    drag(f.editor, {{3, 3}});
    drag(f.editor, {{10, 10}});

    f.editor.setTool(Editor::Select);
    drag(f.editor, {{2, 2}, {4, 4}});
    f.editor.setTool(Editor::Move);
    drag(f.editor, {{3, 3}, {5, 4}, {6, 3}});
    CHECK(painted(f.editor, 6, 3));
    CHECK_FALSE(painted(f.editor, 3, 3));
    CHECK(painted(f.editor, 10, 10)); // outside the selection: untouched
    CHECK(f.editor.selection() == QRect(5, 2, 3, 3));
    f.editor.undo();
    CHECK(painted(f.editor, 3, 3));
    CHECK_FALSE(painted(f.editor, 6, 3));

    // Moved transparent pixels don't cover what's below.
    f.editor.deselect();
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{9, 9}, {11, 11}});
    f.editor.setTool(Editor::Move);
    drag(f.editor, {{10, 10}, {3, 3}}); // the block lands on (3,3)'s pixel
    CHECK(painted(f.editor, 3, 3));
    f.editor.undo();

    // No selection: the whole cel moves, losslessly, even off the canvas.
    f.editor.deselect();
    drag(f.editor, {{0, 0}, {-12, 0}});
    CHECK_FALSE(painted(f.editor, 3, 3));
    CHECK_FALSE(painted(f.editor, 10, 10));
    drag(f.editor, {{0, 0}, {12, 0}});
    CHECK(painted(f.editor, 3, 3));
    CHECK(painted(f.editor, 10, 10));
    f.editor.undo();
    f.editor.undo();
    CHECK(painted(f.editor, 3, 3));
}

TEST_CASE("tools: dragging inside a selection with the select tool moves it")
{
    Fixture f;
    f.editor.setTool(Editor::Pencil);
    drag(f.editor, {{3, 3}});
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{2, 2}, {4, 4}});
    drag(f.editor, {{3, 3}, {8, 3}}); // inside: moves
    CHECK(painted(f.editor, 8, 3));
    CHECK_FALSE(painted(f.editor, 3, 3));
    CHECK(f.editor.selection() == QRect(7, 2, 3, 3));
    drag(f.editor, {{8, 3}, {9, 3}, {8, 3}}); // dragged back to the start: still a move
    CHECK(f.editor.selection() == QRect(7, 2, 3, 3));
    drag(f.editor, {{0, 10}, {2, 12}}); // outside: a new selection
    CHECK(f.editor.selection() == QRect(0, 10, 3, 3));
    CHECK(painted(f.editor, 8, 3));
    drag(f.editor, {{1, 11}}); // click inside, no drag: deselects
    CHECK_FALSE(f.editor.hasSelection());
}

TEST_CASE("tools: Shift-click draws a line from the last painted pixel, with a preview")
{
    Fixture f;
    f.editor.setTool(Editor::Pencil);
    CHECK_FALSE(f.editor.lastPainted());
    drag(f.editor, {{2, 2}});
    CHECK(f.editor.lastPainted() == QPoint(2, 2));

    // Preview lives in the overlay only.
    f.editor.setLinePreview({6, 6}, true);
    CHECK(f.editor.overlay().pixelColor(4, 4).alpha() > 0);
    CHECK_FALSE(painted(f.editor, 4, 4));
    CHECK(f.editor.takeOverlayDirty().contains(QRect(2, 2, 5, 5)));
    f.editor.setTool(Editor::Fill); // not a line tool: preview goes away
    CHECK(f.editor.overlay().pixelColor(4, 4).alpha() == 0);
    f.editor.setTool(Editor::Pencil);
    CHECK(f.editor.overlay().pixelColor(4, 4).alpha() > 0);
    f.editor.setLinePreview({6, 6}, false);
    CHECK(f.editor.overlay().pixelColor(4, 4).alpha() == 0);

    drag(f.editor, {{8, 2}}, Qt::ShiftModifier);
    CHECK(paintedCount(f.editor) == 7);
    CHECK(f.editor.lastPainted() == QPoint(8, 2));
    f.editor.undo(); // the line alone
    CHECK(paintedCount(f.editor) == 1);
}

TEST_CASE("tools: pixel-perfect never erases earlier parts of the same stroke")
{
    // One stroke: down column x=2, around the bottom, then a staircase that
    // crosses the column. The staircase's corner at (2,3) lands on a pixel
    // the stroke painted earlier, so it must stay.
    Fixture f;
    f.editor.setTool(Editor::Pencil);
    drag(f.editor, {{2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {1, 7}, {0, 6}, {0, 5},
                    {1, 5}, {1, 4}, {2, 4}, {2, 3}, {3, 3}, {3, 2}});
    for (int y = 0; y <= 6; ++y) {
        CAPTURE(y);
        CHECK(painted(f.editor, 2, y));
    }
    // Fresh corners are still removed.
    CHECK_FALSE(painted(f.editor, 0, 5));
    CHECK_FALSE(painted(f.editor, 1, 4));
}

namespace {

// A 3x1 red bar at (2..4, 5) on a 16x16 sprite.
struct Bar {
    Editor editor;
    Bar()
    {
        editor.newDocument(16, 16);
        editor.setPrimaryColor(kRed);
        editor.setTool(Editor::Line);
        drag(editor, {{2, 5}, {4, 5}});
    }
    void selectBar()
    {
        editor.deselect(); // a fresh selection, not a drag of the last one
        editor.setTool(Editor::Select);
        drag(editor, {{2, 5}, {4, 5}});
    }
};

} // namespace

TEST_CASE("transform: lift, cancel puts everything back untouched")
{
    Bar b;
    b.selectBar();
    const bool undoBefore = b.editor.canUndo();
    b.editor.startTransform();
    REQUIRE(b.editor.floating());
    CHECK(b.editor.transformBox() == QRect(2, 5, 3, 1));
    CHECK_FALSE(painted(b.editor, 3, 5)); // lifted out of the layer
    CHECK(b.editor.overlay().pixelColor(3, 5) == kRed);
    b.editor.cancelFloating();
    CHECK_FALSE(b.editor.floating());
    CHECK(paintedCount(b.editor) == 3);
    CHECK(b.editor.canUndo() == undoBefore);
}

TEST_CASE("transform: scale, then apply as one undo step")
{
    Bar b;
    b.selectBar();
    b.editor.startTransform();
    b.editor.setTransformBox({2, 5, 6, 2}); // 2x wide, 2 tall
    CHECK(b.editor.transformSourceSize() == QSize(3, 1));
    b.editor.commitFloating();
    CHECK_FALSE(b.editor.floating());
    CHECK(paintedCount(b.editor) == 12);
    CHECK(painted(b.editor, 7, 6));
    b.editor.undo();
    CHECK(paintedCount(b.editor) == 3);
    CHECK(painted(b.editor, 2, 5));
}

TEST_CASE("transform: quick flips and quarter turns apply at once")
{
    Bar b; // no selection: the layer's pixels
    b.editor.rotate90(true);
    CHECK_FALSE(b.editor.floating());
    // 3x1 at x 2..4 turns into 1x3 around the same center (x 3, y 4..6).
    CHECK(paintedCount(b.editor) == 3);
    CHECK(painted(b.editor, 3, 4));
    CHECK(painted(b.editor, 3, 6));
    CHECK_FALSE(painted(b.editor, 2, 5));
    b.editor.undo();
    CHECK(painted(b.editor, 2, 5));

    // Flip a lopsided shape: an L.
    b.editor.setTool(Editor::Pencil);
    drag(b.editor, {{2, 6}});
    b.editor.flipHorizontal();
    CHECK(painted(b.editor, 4, 6));
    CHECK_FALSE(painted(b.editor, 2, 6));
    b.editor.undo();
    CHECK(painted(b.editor, 2, 6));
}

TEST_CASE("transform: free rotation adds no new colors")
{
    Bar b;
    b.editor.setPrimaryColor(QColor(0, 0, 255));
    b.editor.setTool(Editor::Rectangle);
    b.editor.setFillShapes(true);
    drag(b.editor, {{6, 6}, {10, 10}});
    b.editor.setTool(Editor::Select);
    drag(b.editor, {{6, 6}, {10, 10}});
    b.editor.startTransform();
    b.editor.setTransformAngle(30);
    CHECK(b.editor.transformAngle() == doctest::Approx(30));
    b.editor.setTransformAngle(390); // normalized
    CHECK(b.editor.transformAngle() == doctest::Approx(30));
    b.editor.commitFloating();
    int blue = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const QColor c = b.editor.composite().pixelColor(x, y);
            if (c.alpha() == 0 || c == kRed)
                continue;
            CHECK(c == QColor(0, 0, 255));
            ++blue;
        }
    CHECK(blue >= 20);
    CHECK(blue <= 30);
}

TEST_CASE("transform: pastes scale too; moving; delete and cut")
{
    Bar b;
    b.selectBar();
    b.editor.copy();
    b.editor.deselect();
    b.editor.paste();
    REQUIRE(b.editor.floating());
    b.editor.setTransformBox({0, 0, 3, 3});
    b.editor.commitFloating();
    CHECK(painted(b.editor, 1, 2)); // the 3x1 bar stretched to 3x3
    CHECK(paintedCount(b.editor) == 3 + 9);

    // Move tool drags the float.
    b.editor.undo();
    b.selectBar();
    b.editor.startTransform();
    b.editor.setTool(Editor::Move);
    drag(b.editor, {{3, 5}, {3, 9}});
    CHECK(b.editor.transformBox() == QRect(2, 9, 3, 1));
    b.editor.commitFloating();
    CHECK(painted(b.editor, 3, 9));
    CHECK_FALSE(painted(b.editor, 3, 5));
    b.editor.undo();

    // Delete while transforming removes the pixels (undoable).
    b.selectBar();
    b.editor.startTransform();
    b.editor.deleteSelection();
    CHECK_FALSE(b.editor.floating());
    CHECK(paintedCount(b.editor) == 0);
    CHECK_FALSE(b.editor.hasSelection());
    b.editor.undo();
    CHECK(paintedCount(b.editor) == 3);
    CHECK(b.editor.selection() == QRect(2, 5, 3, 1)); // back with the pixels

    // Cut while transforming: gone from the layer, on the clipboard.
    b.selectBar();
    b.editor.startTransform();
    b.editor.cut();
    CHECK(paintedCount(b.editor) == 0);
    b.editor.paste();
    b.editor.commitFloating();
    CHECK(paintedCount(b.editor) == 3);
}

TEST_CASE("transform: without a selection it takes the layer's visible pixels")
{
    Bar b;
    b.editor.startTransform();
    REQUIRE(b.editor.floating());
    CHECK(b.editor.transformBox() == QRect(2, 5, 3, 1));
    b.editor.cancelFloating();

    Editor empty;
    empty.startTransform(); // nothing to lift
    CHECK_FALSE(empty.floating());
}

TEST_CASE("tools: fill contiguous or global, with tolerance")
{
    Fixture f;
    f.editor.setTool(Editor::Pencil);
    // Two separate dark pixels and a slightly lighter one.
    f.editor.setPrimaryColor(QColor(10, 10, 10));
    drag(f.editor, {{1, 1}});
    drag(f.editor, {{8, 8}});
    f.editor.setPrimaryColor(QColor(20, 10, 10));
    drag(f.editor, {{1, 2}});

    f.editor.setTool(Editor::Fill);
    f.editor.setPrimaryColor(kRed);
    CHECK(f.editor.fillContiguous());
    drag(f.editor, {{1, 1}});
    CHECK(f.editor.composite().pixelColor(1, 1) == kRed);
    CHECK(f.editor.composite().pixelColor(8, 8) == QColor(10, 10, 10)); // not connected
    CHECK(f.editor.composite().pixelColor(1, 2) == QColor(20, 10, 10)); // not the same color
    f.editor.undo();

    f.editor.setFillContiguous(false);
    f.editor.setFillTolerance(15);
    drag(f.editor, {{1, 1}});
    CHECK(f.editor.composite().pixelColor(8, 8) == kRed); // global
    CHECK(f.editor.composite().pixelColor(1, 2) == kRed); // within tolerance
    CHECK(f.editor.composite().pixelColor(5, 5).alpha() == 0); // empty pixels untouched
    f.editor.setFillTolerance(999);
    CHECK(f.editor.fillTolerance() == 255);
}

TEST_CASE("selection: select, select all and deselect are undo steps")
{
    Fixture f;
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{3, 3}, {6, 5}});
    f.editor.selectAll();
    f.editor.deselect();
    CHECK_FALSE(f.editor.hasSelection());
    f.editor.undo();
    CHECK(f.editor.selection() == QRect(0, 0, 16, 16));
    f.editor.undo();
    CHECK(f.editor.selection() == QRect(3, 3, 4, 3));
    f.editor.undo();
    CHECK_FALSE(f.editor.hasSelection());
    CHECK_FALSE(f.editor.canUndo());
    f.editor.redo();
    CHECK(f.editor.selection() == QRect(3, 3, 4, 3));
}

TEST_CASE("selection: undoing a pixel move puts the selection back too")
{
    Bar b;
    b.selectBar();
    b.editor.setTool(Editor::Move);
    drag(b.editor, {{3, 5}, {3, 8}});
    CHECK(b.editor.selection() == QRect(2, 8, 3, 1));
    CHECK(painted(b.editor, 3, 8));
    b.editor.undo();
    CHECK(b.editor.selection() == QRect(2, 5, 3, 1));
    CHECK(painted(b.editor, 3, 5));
    CHECK_FALSE(painted(b.editor, 3, 8));
    b.editor.redo();
    CHECK(b.editor.selection() == QRect(2, 8, 3, 1));
}

TEST_CASE("selection: arrow nudges move the pixels as one step, or just the outline")
{
    Bar b;
    b.selectBar();
    CHECK(b.editor.nudge(1, 0));
    CHECK(b.editor.floating());
    CHECK(b.editor.nudge(0, 2));
    CHECK(b.editor.nudge(0, 2));
    b.editor.commitFloating();
    CHECK(b.editor.selection() == QRect(3, 9, 3, 1));
    CHECK(painted(b.editor, 5, 9));
    CHECK_FALSE(painted(b.editor, 2, 5));
    b.editor.undo(); // all nudges at once
    CHECK(b.editor.selection() == QRect(2, 5, 3, 1));
    CHECK(painted(b.editor, 2, 5));
    CHECK_FALSE(painted(b.editor, 5, 9));

    // Outline only: pixels stay; consecutive nudges merge.
    CHECK(b.editor.nudge(-1, 0, true));
    CHECK(b.editor.nudge(-1, 0, true));
    CHECK(b.editor.selection() == QRect(0, 5, 3, 1));
    CHECK(painted(b.editor, 2, 5));
    b.editor.undo();
    CHECK(b.editor.selection() == QRect(2, 5, 3, 1));

    b.editor.deselect();
    CHECK_FALSE(b.editor.nudge(1, 0)); // nothing to move: arrows change frames
    CHECK_FALSE(b.editor.nudge(1, 0, true));
}

TEST_CASE("selection: cancelling a paste keeps the selection that was there")
{
    Bar b;
    b.selectBar();
    b.editor.copy();
    b.editor.setTool(Editor::Select);
    drag(b.editor, {{8, 8}, {10, 10}});
    b.editor.paste();
    REQUIRE(b.editor.floating());
    b.editor.cancelFloating();
    CHECK(b.editor.selection() == QRect(8, 8, 3, 3));
}

namespace {

// A 16 x 16 red canvas with a ring selected: 2..7 without 4..5.
struct Ring {
    Editor editor;
    Ring()
    {
        editor.newDocument(16, 16);
        editor.setPrimaryColor(kRed);
        editor.setTool(Editor::Fill);
        drag(editor, {{0, 0}});
        editor.setTool(Editor::Select);
        drag(editor, {{2, 2}, {7, 7}});
        drag(editor, {{4, 4}, {5, 5}}, Qt::AltModifier);
    }
};

} // namespace

TEST_CASE("selection: Shift adds, Alt subtracts, each one undo step")
{
    Fixture f;
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{2, 2}, {5, 5}});
    drag(f.editor, {{8, 8}, {9, 9}}, Qt::ShiftModifier);
    CHECK(f.editor.selection() == QRect(2, 2, 8, 8)); // the bounds
    CHECK(f.editor.selectionContains({9, 9}));
    CHECK_FALSE(f.editor.selectionContains({6, 6}));

    drag(f.editor, {{3, 3}, {4, 4}}, Qt::AltModifier); // starts inside: still subtracts
    CHECK_FALSE(f.editor.selectionContains({3, 3}));
    CHECK(f.editor.selectionContains({2, 2}));
    CHECK(f.editor.selectionContains({5, 5}));

    drag(f.editor, {{12, 12}}, Qt::ShiftModifier); // a Shift click changes nothing
    CHECK(f.editor.selectionContains({2, 2}));

    // Shift held from the start adds; Alt pressed meanwhile still centers.
    f.editor.beginStroke({12, 2}, false, Qt::ShiftModifier);
    f.editor.moveStroke({13, 3}, Qt::ShiftModifier | Qt::AltModifier);
    f.editor.endStroke();
    CHECK(f.editor.selectionContains({11, 1}));
    CHECK(f.editor.selectionContains({2, 2}));

    f.editor.undo();
    f.editor.undo();
    CHECK(f.editor.selectionContains({3, 3})); // the hole is gone again
    f.editor.undo();
    CHECK(f.editor.selection() == QRect(2, 2, 4, 4));
}

TEST_CASE("selection: delete and fill only touch the selected pixels")
{
    Ring r;
    CHECK(r.editor.selection() == QRect(2, 2, 6, 6));
    r.editor.deleteSelection();
    CHECK_FALSE(painted(r.editor, 2, 2));
    CHECK(painted(r.editor, 4, 4)); // the hole keeps its pixels
    CHECK(painted(r.editor, 0, 0));
    CHECK(paintedCount(r.editor) == 256 - 32);

    // Fill the ring again: only the ring.
    r.editor.setPrimaryColor(QColor(0, 0, 255));
    r.editor.setTool(Editor::Fill);
    drag(r.editor, {{2, 2}});
    CHECK(r.editor.composite().pixelColor(7, 7) == QColor(0, 0, 255));
    CHECK(r.editor.composite().pixelColor(4, 4) == kRed);
    CHECK(paintedCount(r.editor) == 256);
}

TEST_CASE("selection: moving a ring moves only its pixels; the hole stays")
{
    Ring r;
    r.editor.setTool(Editor::Move);
    drag(r.editor, {{2, 2}, {10, 2}});
    CHECK_FALSE(painted(r.editor, 2, 2)); // the ring left
    CHECK(painted(r.editor, 4, 4));       // the hole's pixels didn't move
    CHECK(r.editor.selectionContains({10, 2}));
    CHECK_FALSE(r.editor.selectionContains({12, 4}));
    r.editor.undo();
    CHECK(painted(r.editor, 2, 2));
    CHECK(r.editor.selection() == QRect(2, 2, 6, 6));
    CHECK_FALSE(r.editor.selectionContains({4, 4}));
}

TEST_CASE("selection: a transform carries the selection's shape")
{
    Fixture f;
    f.editor.setPrimaryColor(kRed);
    f.editor.setTool(Editor::Fill);
    drag(f.editor, {{0, 0}});
    // An L: the top row and the left column of 0..3.
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{0, 0}, {3, 0}});
    drag(f.editor, {{0, 0}, {0, 3}}, Qt::ShiftModifier);
    f.editor.flipHorizontal(); // lifts, flips and lands
    CHECK(f.editor.selectionContains({3, 3}));
    CHECK_FALSE(f.editor.selectionContains({0, 3}));
    CHECK(f.editor.selection() == QRect(0, 0, 4, 4));
    CHECK(painted(f.editor, 1, 1)); // not selected: untouched
}

TEST_CASE("selection: selecting alone doesn't mark the document modified")
{
    Fixture f;
    REQUIRE_FALSE(f.editor.modified());
    f.editor.selectAll();
    CHECK_FALSE(f.editor.modified());
    CHECK(f.editor.canUndo());
    f.editor.deselect();
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{1, 1}, {3, 3}});
    CHECK(f.editor.nudge(1, 0, true));
    CHECK_FALSE(f.editor.modified());

    f.editor.setPrimaryColor(kRed);
    f.editor.setTool(Editor::Fill);
    drag(f.editor, {{2, 2}});
    CHECK(f.editor.modified());
    f.editor.undo();
    CHECK_FALSE(f.editor.modified());
}

TEST_CASE("brush: round brushes leave the corners out")
{
    Fixture f;
    f.editor.setBrushSize(3);
    f.editor.setBrushShape(Editor::RoundBrush);
    drag(f.editor, {{5, 5}});
    CHECK(paintedCount(f.editor) == 5); // a plus
    CHECK(painted(f.editor, 5, 4));
    CHECK_FALSE(painted(f.editor, 4, 4));
    CHECK(f.editor.brushImage().size() == QSize(3, 3));
    CHECK(f.editor.brushImage().pixelColor(0, 0).alpha() == 0);
    f.editor.setBrushShape(Editor::SquareBrush);
    drag(f.editor, {{10, 10}});
    CHECK(paintedCount(f.editor) == 5 + 9);
}

TEST_CASE("symmetry: strokes, shapes and fills are mirrored around the center")
{
    Fixture f; // 16 x 16: the axes lie between pixels 7 and 8
    f.editor.setSymmetryHorizontal(true);
    drag(f.editor, {{1, 2}});
    CHECK(painted(f.editor, 1, 2));
    CHECK(painted(f.editor, 14, 2));
    CHECK(paintedCount(f.editor) == 2);
    f.editor.undo(); // both at once
    CHECK(paintedCount(f.editor) == 0);

    f.editor.setSymmetryVertical(true); // four ways
    drag(f.editor, {{1, 2}});
    CHECK(painted(f.editor, 1, 13));
    CHECK(painted(f.editor, 14, 13));
    CHECK(paintedCount(f.editor) == 4);
    f.editor.undo();

    // An even brush mirrors onto the same columns mirrored.
    f.editor.setSymmetryVertical(false);
    f.editor.setBrushSize(2);
    drag(f.editor, {{1, 1}}); // covers 0..1
    CHECK(painted(f.editor, 0, 0));
    CHECK(painted(f.editor, 15, 0));
    CHECK(painted(f.editor, 14, 1));
    CHECK(paintedCount(f.editor) == 8);
    f.editor.undo();
    f.editor.setBrushSize(1);

    // Lines.
    f.editor.setTool(Editor::Line);
    drag(f.editor, {{0, 8}, {3, 8}});
    CHECK(painted(f.editor, 12, 8));
    CHECK(painted(f.editor, 15, 8));
    CHECK(paintedCount(f.editor) == 8);
    f.editor.undo();

    // Fill: the mirrored point fills too, without filling twice.
    f.editor.setTool(Editor::Pencil);
    for (int y = 0; y < 16; ++y)
        drag(f.editor, {{7, y}}); // a wall at 7 (and its mirror at 8)
    f.editor.setTool(Editor::Fill);
    f.editor.setPrimaryColor(QColor(0, 0, 255));
    drag(f.editor, {{0, 0}});
    CHECK(f.editor.composite().pixelColor(0, 0) == QColor(0, 0, 255));
    CHECK(f.editor.composite().pixelColor(15, 15) == QColor(0, 0, 255));
    CHECK(f.editor.composite().pixelColor(7, 3) == kRed);
}

TEST_CASE("symmetry: odd canvas sizes mirror through the middle column")
{
    Editor editor;
    editor.newDocument(15, 15);
    editor.setPrimaryColor(kRed);
    editor.setSymmetryHorizontal(true);
    drag(editor, {{0, 0}});
    CHECK(painted(editor, 14, 0));
    drag(editor, {{7, 3}}); // on the axis: its own mirror
    CHECK(paintedCount(editor) == 3);

    // Pixel-perfect corners are taken out on both sides.
    editor.newDocument(15, 15);
    editor.setPrimaryColor(kRed);
    drag(editor, {{1, 1}, {2, 1}, {2, 2}}); // an L: the corner (2, 1) goes
    CHECK_FALSE(painted(editor, 2, 1));
    CHECK_FALSE(painted(editor, 12, 1));
    CHECK(painted(editor, 13, 1));
    CHECK(painted(editor, 12, 2));
}

TEST_CASE("selection: Shift and Alt together intersect")
{
    Fixture f;
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{0, 0}, {5, 5}});
    drag(f.editor, {{3, 3}, {9, 9}}, Qt::ShiftModifier | Qt::AltModifier);
    CHECK(f.editor.selection() == QRect(3, 3, 3, 3));
    CHECK_FALSE(f.editor.selectionContains({7, 7}));
    f.editor.undo();
    CHECK(f.editor.selection() == QRect(0, 0, 6, 6));
}

TEST_CASE("lasso: selects inside the drawn outline, adds and subtracts, click deselects")
{
    Fixture f;
    f.editor.setTool(Editor::Lasso);
    // A right triangle, closed back to the start when let go.
    drag(f.editor, {{0, 0}, {4, 0}, {8, 0}, {0, 8}});
    CHECK(f.editor.selection() == QRect(0, 0, 9, 9));
    CHECK(f.editor.selectionContains({2, 2}));
    CHECK(f.editor.selectionContains({4, 4})); // on the closing line
    CHECK_FALSE(f.editor.selectionContains({7, 7}));

    drag(f.editor, {{12, 12}, {14, 12}, {14, 14}}, Qt::ShiftModifier);
    CHECK(f.editor.selectionContains({14, 13}));
    CHECK(f.editor.selectionContains({2, 2}));

    drag(f.editor, {{0, 0}, {2, 0}, {2, 2}, {0, 2}}, Qt::AltModifier);
    CHECK_FALSE(f.editor.selectionContains({1, 1}));
    CHECK(f.editor.selectionContains({3, 3}));

    // The outline is clipped to the canvas.
    drag(f.editor, {{-5, -5}, {3, -5}, {3, 3}});
    CHECK(f.editor.selection().top() == 0);
    CHECK(f.editor.selectionContains({3, 3}));

    drag(f.editor, {{6, 6}}); // a click
    CHECK_FALSE(f.editor.hasSelection());
    f.editor.undo();
    f.editor.undo();
    CHECK_FALSE(f.editor.selectionContains({1, 1})); // the subtracted corner
    CHECK(f.editor.selectionContains({14, 13}));
}

TEST_CASE("magic wand: selects matching pixels of the current cel")
{
    Fixture f;
    // A red 4x4 square at 2..5.
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{2, 2}, {5, 5}});
    f.editor.setTool(Editor::Fill);
    drag(f.editor, {{3, 3}});
    f.editor.deselect();

    f.editor.setTool(Editor::MagicWand);
    drag(f.editor, {{3, 3}});
    CHECK(f.editor.selection() == QRect(2, 2, 4, 4));
    CHECK(f.editor.selectionContains({5, 5}));

    // The transparent rest: a canvas with a hole.
    drag(f.editor, {{0, 0}});
    CHECK(f.editor.selection() == QRect(0, 0, 16, 16));
    CHECK_FALSE(f.editor.selectionContains({3, 3}));

    drag(f.editor, {{3, 3}}, Qt::ShiftModifier); // add the square
    CHECK(f.editor.selectionContains({3, 3}));
    drag(f.editor, {{0, 0}}, Qt::AltModifier); // take away the transparent pixels
    CHECK(f.editor.selection() == QRect(2, 2, 4, 4));

    // Global: a second square, not touching the first, counts too.
    f.editor.deselect();
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{10, 10}, {11, 11}});
    f.editor.setTool(Editor::Fill);
    drag(f.editor, {{10, 10}});
    f.editor.deselect();
    f.editor.setTool(Editor::MagicWand);
    drag(f.editor, {{3, 3}});
    CHECK_FALSE(f.editor.selectionContains({10, 10}));
    f.editor.setWandContiguous(false);
    drag(f.editor, {{3, 3}});
    CHECK(f.editor.selectionContains({10, 10}));
    CHECK(f.editor.selection() == QRect(2, 2, 10, 10));

    // Each click is one undo step; a click off the canvas deselects.
    f.editor.undo();
    CHECK_FALSE(f.editor.selectionContains({10, 10}));
    CHECK(f.editor.hasSelection());
    drag(f.editor, {{-1, 3}});
    CHECK_FALSE(f.editor.hasSelection());
}

TEST_CASE("selection: invert, and select a layer's pixels")
{
    Fixture f;
    f.editor.invertSelection(); // nothing selected: everything
    CHECK(f.editor.selection() == QRect(0, 0, 16, 16));
    f.editor.deselect();
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{0, 0}, {15, 7}});
    f.editor.invertSelection();
    CHECK(f.editor.selection() == QRect(0, 8, 16, 8));
    f.editor.undo();
    CHECK(f.editor.selection() == QRect(0, 0, 16, 8));

    // An L of pixels.
    f.editor.setTool(Editor::Pencil);
    drag(f.editor, {{3, 3}, {3, 6}, {5, 6}});
    f.editor.deselect();
    f.editor.selectLayerPixels(f.editor.currentLayer());
    CHECK(f.editor.selection() == QRect(3, 3, 3, 4));
    CHECK(f.editor.selectionContains({3, 4}));
    CHECK_FALSE(f.editor.selectionContains({4, 4}));
    f.editor.undo();
    CHECK_FALSE(f.editor.hasSelection());
}

namespace {

QColor paletteColor(const Editor& editor, int i) { return editor.palette().at(i).value<QColor>(); }

QColor at(const Editor& editor, int x, int y) { return editor.composite().pixelColor(x, y); }

void dot(Editor& editor, QPoint p, const QColor& color)
{
    editor.setPrimaryColor(color);
    drag(editor, {p});
}

} // namespace

TEST_CASE("shading: steps colors along the palette once per stroke, other pixels stay")
{
    for (const char* mode : {"rgba", "indexed"}) {
        CAPTURE(mode);
        Fixture f;
        const int last = int(f.editor.palette().size()) - 1;
        dot(f.editor, {2, 2}, paletteColor(f.editor, 1));
        dot(f.editor, {3, 2}, paletteColor(f.editor, 5));
        dot(f.editor, {6, 2}, paletteColor(f.editor, last));
        if (QString(mode) == "rgba")
            dot(f.editor, {4, 2}, QColor(1, 2, 3)); // not in the palette
        else
            REQUIRE(toMode(f.editor, "indexed"));
        const QColor at4 = at(f.editor, 4, 2);

        f.editor.setShading(true);
        drag(f.editor, {{2, 2}, {7, 2}, {2, 2}}); // over every pixel twice
        CHECK(at(f.editor, 2, 2) == paletteColor(f.editor, 2));
        CHECK(at(f.editor, 3, 2) == paletteColor(f.editor, 6));
        CHECK(at(f.editor, 4, 2) == at4);
        CHECK(at(f.editor, 5, 2).alpha() == 0); // transparent stays
        CHECK(at(f.editor, 6, 2) == paletteColor(f.editor, last)); // clamped
        f.editor.undo(); // one step
        CHECK(at(f.editor, 2, 2) == paletteColor(f.editor, 1));
        CHECK(at(f.editor, 3, 2) == paletteColor(f.editor, 5));

        // Right mouse steps back.
        f.editor.beginStroke({3, 2}, true);
        f.editor.endStroke();
        CHECK(at(f.editor, 3, 2) == paletteColor(f.editor, 4));
    }
}

TEST_CASE("shading: the line and the outlines of rectangles and ellipses shade too")
{
    Fixture f;
    dot(f.editor, {2, 2}, paletteColor(f.editor, 1));
    dot(f.editor, {3, 2}, paletteColor(f.editor, 1));
    dot(f.editor, {3, 5}, paletteColor(f.editor, 1)); // on the rectangle's bottom edge
    f.editor.setShading(true);
    f.editor.setTool(Editor::Line);
    drag(f.editor, {{2, 2}, {6, 2}});
    CHECK(at(f.editor, 2, 2) == paletteColor(f.editor, 2));
    CHECK(at(f.editor, 3, 2) == paletteColor(f.editor, 2)); // once, however the line crosses it
    CHECK(at(f.editor, 5, 2).alpha() == 0);
    f.editor.undo();
    CHECK(at(f.editor, 2, 2) == paletteColor(f.editor, 1));

    f.editor.setTool(Editor::Rectangle);
    f.editor.setFillShapes(false);
    drag(f.editor, {{3, 2}, {6, 5}});
    CHECK(at(f.editor, 3, 5) == paletteColor(f.editor, 2));
    CHECK(at(f.editor, 6, 3).alpha() == 0); // only pixels with a color of the ramp shade
    f.editor.undo();
    // Filled: the shape paints as usual.
    f.editor.setFillShapes(true);
    drag(f.editor, {{3, 2}, {6, 5}});
    CHECK(at(f.editor, 6, 3).alpha() == 255);
}

TEST_CASE("symmetry: a selection takes the mirrored areas too")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.setTool(Editor::Select);
    editor.setSymmetryHorizontal(true);
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({1, 1}, {});
    editor.endStroke();
    CHECK(editor.selectionContains({0, 0}));
    CHECK(editor.selectionContains({7, 1})); // the mirror across the vertical axis
    CHECK_FALSE(editor.selectionContains({0, 7}));
    editor.setSymmetryVertical(true);
    editor.deselect();
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({1, 1}, {});
    editor.endStroke();
    CHECK(editor.selectionContains({7, 7}));
    CHECK(editor.selectionContains({0, 7}));
}

TEST_CASE("symmetry: radial copies turn round the crossing point")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setSymmetryRadial(4);
    // The crossing point is the middle, (8, 8): a dot at (11, 8) turns to the other three quarters.
    editor.beginStroke({11, 8}, false, {});
    editor.endStroke();
    const auto painted = [&](int x, int y) { return editor.composite().pixelColor(x, y).alpha() == 255; };
    CHECK(painted(11, 8));
    CHECK(painted(7, 11));
    CHECK(painted(4, 7));
    CHECK(painted(8, 4));
    int count = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            count += painted(x, y);
    CHECK(count == 4);
    editor.undo();

    // With a mirror: eight copies.
    editor.setSymmetryRadialMirror(true);
    editor.beginStroke({11, 5}, false, {}); // off the mirror line of the turns
    editor.endStroke();
    count = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            count += painted(x, y);
    CHECK(count == 8);
    CHECK(painted(4, 5)); // the mirror across the vertical axis
    editor.undo();
    editor.setSymmetryRadial(1); // off
    CHECK(editor.symmetryRadial() == 0);

    // A filled rectangle fills the turned copies too.
    editor.setSymmetryRadial(2);
    editor.setSymmetryRadialMirror(false);
    editor.setTool(Editor::Rectangle);
    editor.setFillShapes(true);
    editor.beginStroke({10, 2}, false, {});
    editor.moveStroke({11, 3}, {});
    editor.endStroke();
    CHECK(painted(10, 2));
    CHECK(painted(5, 13)); // half a turn about (8, 8)
}

TEST_CASE("shading: a palette range, strokes that change nothing, empty cels")
{
    Fixture f;
    dot(f.editor, {2, 2}, paletteColor(f.editor, 1));
    dot(f.editor, {3, 2}, paletteColor(f.editor, 5));
    dot(f.editor, {4, 2}, paletteColor(f.editor, 6));
    f.editor.setShading(true);
    f.editor.setShadeRange({6, 4}); // either order
    CHECK(f.editor.shadeRange() == QPoint(4, 6));
    drag(f.editor, {{2, 2}, {4, 2}});
    CHECK(at(f.editor, 2, 2) == paletteColor(f.editor, 1)); // not in the range
    CHECK(at(f.editor, 3, 2) == paletteColor(f.editor, 6));
    CHECK(at(f.editor, 4, 2) == paletteColor(f.editor, 6)); // the range's end
    f.editor.undo();
    CHECK(at(f.editor, 3, 2) == paletteColor(f.editor, 5));

    // Nothing of the ramp under the stroke: no undo step.
    drag(f.editor, {{2, 2}, {2, 8}});
    f.editor.undo(); // the last dot, not an empty shading step
    CHECK(at(f.editor, 4, 2).alpha() == 0);

    // An empty frame gets no cel.
    f.editor.addFrame();
    drag(f.editor, {{2, 2}});
    CHECK(paintedCount(f.editor) == 0);
    f.editor.undo(); // the new frame, nothing in between
    CHECK(f.editor.frameCount() == 1);

    // Palette edits reset the range.
    f.editor.setShadeRange({2, 3});
    f.editor.setPaletteColor(0, kRed);
    CHECK(f.editor.shadeRange() == QPoint(-1, -1));
}

TEST_CASE("shading: mirrored with symmetry and clipped to the selection")
{
    Fixture f;
    for (int x : {1, 14, 2, 13})
        dot(f.editor, {x, 1}, paletteColor(f.editor, 3));
    f.editor.setShading(true);
    f.editor.setSymmetryHorizontal(true);
    drag(f.editor, {{1, 1}});
    CHECK(at(f.editor, 1, 1) == paletteColor(f.editor, 4));
    CHECK(at(f.editor, 14, 1) == paletteColor(f.editor, 4));
    CHECK(at(f.editor, 2, 1) == paletteColor(f.editor, 3));
    f.editor.undo();

    f.editor.setSymmetryHorizontal(false);
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{0, 0}, {1, 1}});
    f.editor.setTool(Editor::Pencil);
    drag(f.editor, {{1, 1}, {2, 1}});
    CHECK(at(f.editor, 1, 1) == paletteColor(f.editor, 4));
    CHECK(at(f.editor, 2, 1) == paletteColor(f.editor, 3)); // outside the selection
}

namespace {

const QColor kBlue(0, 0, 255);

// Paints an L-shaped brush at (1, 1): red red / blue . and makes it the brush
// from a selection with a transparent margin around it.
void makeLBrush(Editor& editor)
{
    dot(editor, {1, 1}, kRed);
    dot(editor, {2, 1}, kRed);
    dot(editor, {1, 2}, kBlue);
    editor.setTool(Editor::Select);
    drag(editor, {{0, 0}, {4, 4}});
    REQUIRE(editor.brushFromSelection());
    editor.deselect();
    editor.setTool(Editor::Pencil);
}

} // namespace

TEST_CASE("custom brush: from the selection, trimmed, stamps its own colors")
{
    Fixture f;
    f.editor.setBrushShape(Editor::CustomBrush); // none yet
    CHECK(f.editor.brushShape() == Editor::SquareBrush);
    CHECK_FALSE(f.editor.hasCustomBrush());

    makeLBrush(f.editor);
    CHECK(f.editor.hasCustomBrush());
    CHECK(f.editor.brushShape() == Editor::CustomBrush);
    CHECK(f.editor.customBrushImage().size() == QSize(2, 2));
    CHECK(f.editor.brushExtent() == QSize(2, 2));

    drag(f.editor, {{9, 9}}); // a 2 x 2 brush's handle: its bottom-right pixel
    CHECK(at(f.editor, 8, 8) == kRed);
    CHECK(at(f.editor, 9, 8) == kRed);
    CHECK(at(f.editor, 8, 9) == kBlue);
    CHECK(at(f.editor, 9, 9).alpha() == 0); // transparent in the brush
    f.editor.undo();
    CHECK(at(f.editor, 8, 8).alpha() == 0);

    // A drag is one undo step.
    drag(f.editor, {{9, 9}, {12, 9}});
    CHECK(at(f.editor, 11, 8) == kRed);
    f.editor.undo();
    CHECK(at(f.editor, 11, 8).alpha() == 0);

    // Its shape in the primary color (right mouse: secondary).
    f.editor.setBrushColors(false);
    f.editor.setPrimaryColor(QColor(0, 255, 0));
    drag(f.editor, {{9, 9}});
    CHECK(at(f.editor, 8, 9) == QColor(0, 255, 0));
    CHECK(at(f.editor, 9, 9).alpha() == 0);
    f.editor.undo();

    // Shape outlines stamp it too.
    f.editor.setTool(Editor::Line);
    f.editor.setBrushColors(true);
    drag(f.editor, {{9, 9}, {13, 9}});
    CHECK(at(f.editor, 12, 8) == kRed);
    CHECK(at(f.editor, 12, 9) == kBlue);
    f.editor.undo();
    f.editor.setTool(Editor::Pencil);

    // Square again and back.
    f.editor.setBrushShape(Editor::SquareBrush);
    CHECK(f.editor.brushExtent() == QSize(1, 1));
    f.editor.setBrushShape(Editor::CustomBrush);
    CHECK(f.editor.brushShape() == Editor::CustomBrush);
}

TEST_CASE("custom brush: the whole cel without a selection; nothing to take; too big")
{
    Fixture f;
    CHECK_FALSE(f.editor.brushFromSelection()); // an empty frame
    dot(f.editor, {3, 3}, kRed);
    dot(f.editor, {5, 4}, kBlue);
    REQUIRE(f.editor.brushFromSelection());
    CHECK(f.editor.customBrushImage().size() == QSize(3, 2));
    CHECK(f.editor.customBrushImage().pixelColor(0, 0) == kRed);
    CHECK(f.editor.customBrushImage().pixelColor(2, 1) == kBlue);

    Editor big;
    big.newDocument(300, 8);
    big.setTool(Editor::Fill);
    big.beginStroke({0, 0}, false);
    big.endStroke();
    QSignalSpy errors(&big, &Editor::errorOccurred);
    CHECK_FALSE(big.brushFromSelection());
    CHECK(errors.count() == 1);
    CHECK_FALSE(big.hasCustomBrush());
}

TEST_CASE("custom brush: symmetry stamps flipped copies")
{
    Fixture f; // 16 wide: x mirrors to 15 - x for single pixels
    makeLBrush(f.editor); // its source pixels stay at 1..2 x 1..2
    f.editor.setSymmetryHorizontal(true);
    drag(f.editor, {{3, 3}}); // covers 2..3 x 2..3
    CHECK(at(f.editor, 2, 3) == kBlue);
    CHECK(at(f.editor, 3, 3).alpha() == 0);
    // Mirrored onto 12..13, flipped: red red / . blue
    CHECK(at(f.editor, 12, 2) == kRed);
    CHECK(at(f.editor, 13, 2) == kRed);
    CHECK(at(f.editor, 13, 3) == kBlue);
    CHECK(at(f.editor, 12, 3).alpha() == 0);
    CHECK(paintedCount(f.editor) == 3 + 6);
}

TEST_CASE("custom brush: eraser, shading and indexed sprites use it")
{
    Fixture f;
    makeLBrush(f.editor);

    // Eraser: the brush's shape.
    f.editor.setTool(Editor::Eraser);
    drag(f.editor, {{2, 2}}); // over the brush's own pixels at 1..2
    CHECK(paintedCount(f.editor) == 0);
    f.editor.undo();
    CHECK(paintedCount(f.editor) == 3);

    // Shading: the brush's shape steps palette colors.
    f.editor.setTool(Editor::Pencil);
    f.editor.setBrushShape(Editor::SquareBrush);
    dot(f.editor, {8, 8}, paletteColor(f.editor, 1));
    dot(f.editor, {9, 9}, paletteColor(f.editor, 1));
    f.editor.setBrushShape(Editor::CustomBrush);
    f.editor.setShading(true);
    drag(f.editor, {{9, 9}});
    CHECK(at(f.editor, 8, 8) == paletteColor(f.editor, 2));
    CHECK(at(f.editor, 9, 9) == paletteColor(f.editor, 1)); // outside the footprint
    f.editor.setShading(false);

    // Indexed: the brush maps to palette entries.
    REQUIRE(toMode(f.editor, "indexed"));
    const QColor red = at(f.editor, 1, 1);
    drag(f.editor, {{13, 13}});
    CHECK(at(f.editor, 12, 12) == red);
}

namespace {

// Whether every painted pixel of row y in [x0, x1] is on the checker (x + y
// even) and every checker pixel there is painted.
bool checkerRow(const Editor& editor, int y, int x0, int x1)
{
    for (int x = x0; x <= x1; ++x)
        if (painted(editor, x, y) != ((x + y) % 2 == 0))
            return false;
    return true;
}

// Whether nothing in row y is painted off the checker.
bool onCheckerOnly(const Editor& editor, int y)
{
    for (int x = 0; x < editor.canvasWidth(); ++x)
        if (painted(editor, x, y) && (x + y) % 2 != 0)
            return false;
    return true;
}

} // namespace

TEST_CASE("dither: the pencil paints the pattern, anchored to the canvas, once per pixel")
{
    Fixture f;
    f.editor.setDither(true); // checker by default
    f.editor.setBrushSize(3);
    drag(f.editor, {{2, 4}, {12, 4}, {2, 4}}); // back and forth
    for (int y = 3; y <= 5; ++y)
        CHECK(checkerRow(f.editor, y, 1, 13));
    f.editor.undo(); // one step
    CHECK(paintedCount(f.editor) == 0);

    // A second stroke lines up with the first.
    drag(f.editor, {{2, 4}, {6, 4}});
    drag(f.editor, {{5, 5}, {9, 5}});
    for (int y = 3; y <= 6; ++y)
        CHECK(onCheckerOnly(f.editor, y));
    CHECK(checkerRow(f.editor, 4, 1, 10)); // covered by both, gap-free

    // Over existing pixels: the skipped ones stay as they were.
    f.editor.setDither(false);
    f.editor.setPrimaryColor(kBlue);
    drag(f.editor, {{2, 12}, {6, 12}});
    f.editor.setDither(true);
    f.editor.setPrimaryColor(kRed);
    drag(f.editor, {{2, 12}, {6, 12}});
    CHECK(at(f.editor, 2, 12) == kRed); // 14 even: on
    CHECK(at(f.editor, 3, 12) == kBlue);
}

TEST_CASE("dither: two colors; the right mouse swaps them; patterns and density")
{
    Fixture f;
    f.editor.setDither(true);
    f.editor.setDitherOtherColor(true);
    f.editor.setSecondaryColor(kBlue);
    f.editor.setBrushSize(1);
    drag(f.editor, {{2, 2}, {5, 2}});
    CHECK(at(f.editor, 2, 2) == kRed);
    CHECK(at(f.editor, 3, 2) == kBlue);
    f.editor.beginStroke({2, 6}, true); // right mouse: secondary on the pattern
    f.editor.moveStroke({3, 6});
    f.editor.endStroke();
    CHECK(at(f.editor, 2, 6) == kBlue);
    CHECK(at(f.editor, 3, 6) == kRed);

    // Other patterns.
    f.editor.setDitherOtherColor(false);
    f.editor.setDitherPattern(2); // horizontal lines
    f.editor.setTool(Editor::Rectangle);
    f.editor.setFillShapes(true);
    drag(f.editor, {{8, 8}, {11, 11}});
    CHECK(painted(f.editor, 9, 8));
    CHECK_FALSE(painted(f.editor, 9, 9));
    CHECK(painted(f.editor, 9, 10));
    f.editor.undo();
    f.editor.setDitherPattern(1); // ordered
    f.editor.setDitherDensity(4);
    drag(f.editor, {{8, 8}, {11, 11}});
    int n = 0;
    for (int y = 8; y < 12; ++y)
        for (int x = 8; x < 12; ++x)
            n += painted(f.editor, x, y);
    CHECK(n == 4); // 4 of 16
    CHECK(f.editor.ditherPreview(1, 4, 4).pixelColor(0, 0).alpha() == 255);
}

TEST_CASE("dither: fill, eraser and the shape preview")
{
    Fixture f;
    f.editor.setDither(true);
    f.editor.setTool(Editor::Fill);
    f.editor.beginStroke({0, 0}, false);
    f.editor.endStroke();
    CHECK(paintedCount(f.editor) == 16 * 16 / 2);
    CHECK(painted(f.editor, 0, 0));
    CHECK_FALSE(painted(f.editor, 1, 0));
    f.editor.undo();

    // Erase every other pixel of a solid area.
    f.editor.setDither(false);
    f.editor.beginStroke({0, 0}, false);
    f.editor.endStroke();
    f.editor.setDither(true);
    f.editor.setTool(Editor::Eraser);
    f.editor.setBrushSize(4);
    drag(f.editor, {{4, 4}});
    CHECK_FALSE(painted(f.editor, 2, 2)); // on: erased
    CHECK(painted(f.editor, 3, 2));       // off: kept
    CHECK(painted(f.editor, 8, 8));       // outside the brush

    // A shape dragged around leaves nothing of its earlier previews.
    f.editor.undo();
    f.editor.undo();
    f.editor.setTool(Editor::Line);
    f.editor.setBrushSize(1);
    drag(f.editor, {{1, 1}, {14, 1}, {1, 14}, {7, 1}});
    CHECK(paintedCount(f.editor) == 4); // (1,1) (3,1) (5,1) (7,1)
    CHECK(checkerRow(f.editor, 1, 0, 8));
}

TEST_CASE("dither: symmetry lines up, shading only on the pattern")
{
    Fixture f; // 16 wide: x mirrors to 15 - x, which flips the checker's parity
    f.editor.setDither(true);
    f.editor.setSymmetryHorizontal(true);
    drag(f.editor, {{2, 2}, {5, 2}});
    CHECK(painted(f.editor, 2, 2));
    CHECK_FALSE(painted(f.editor, 13, 2)); // the canvas's pattern, not a mirrored one
    CHECK(painted(f.editor, 12, 2));
    CHECK(onCheckerOnly(f.editor, 2));
    f.editor.undo();
    f.editor.setSymmetryHorizontal(false);

    f.editor.setDither(false);
    f.editor.setPrimaryColor(paletteColor(f.editor, 1));
    drag(f.editor, {{2, 6}, {5, 6}});
    f.editor.setDither(true);
    f.editor.setShading(true);
    drag(f.editor, {{2, 6}, {5, 6}});
    CHECK(at(f.editor, 2, 6) == paletteColor(f.editor, 2));
    CHECK(at(f.editor, 3, 6) == paletteColor(f.editor, 1));
}

TEST_CASE("symmetry: diagonal axes, and every copy the axes make together")
{
    Fixture f; // 16 x 16: the axes cross at (8, 8)
    f.editor.setSymmetryFalling(true); // "\": (x, y) -> (y, x)
    drag(f.editor, {{3, 5}});
    CHECK(painted(f.editor, 3, 5));
    CHECK(painted(f.editor, 5, 3));
    CHECK(paintedCount(f.editor) == 2);
    f.editor.undo();

    f.editor.setSymmetryFalling(false);
    f.editor.setSymmetryRising(true); // "/": pixel (3, 5) -> (10, 12)
    drag(f.editor, {{3, 5}});
    CHECK(painted(f.editor, 10, 12));
    CHECK(paintedCount(f.editor) == 2);
    f.editor.undo();

    f.editor.setSymmetryHorizontal(true); // with "/": all eight
    drag(f.editor, {{2, 5}});
    CHECK(paintedCount(f.editor) == 8);
    for (const QPoint p : {QPoint(2, 5), QPoint(13, 5), QPoint(5, 2), QPoint(10, 2), QPoint(2, 10), QPoint(13, 10),
                           QPoint(5, 13), QPoint(10, 13)})
        CHECK(painted(f.editor, p.x(), p.y()));
    f.editor.undo(); // one step

    // A filled row mirrored across "\" is a column.
    f.editor.setSymmetryHorizontal(false);
    f.editor.setSymmetryRising(false);
    f.editor.setSymmetryFalling(true);
    f.editor.setTool(Editor::Rectangle);
    f.editor.setFillShapes(true);
    drag(f.editor, {{2, 1}, {5, 1}});
    for (int i = 2; i <= 5; ++i) {
        CHECK(painted(f.editor, i, 1));
        CHECK(painted(f.editor, 1, i));
    }
    CHECK(paintedCount(f.editor) == 8);
}

TEST_CASE("symmetry: a moved crossing point, in half pixels, per tab, reset by a canvas resize")
{
    Fixture f;
    CHECK(f.editor.symmetryCenter() == QPointF(8, 8));
    f.editor.setSymmetryHorizontal(true);
    f.editor.setSymmetryCenter({5, 5}); // on the edge between pixels 4 and 5
    drag(f.editor, {{2, 1}});
    CHECK(painted(f.editor, 7, 1));
    f.editor.undo();
    f.editor.setSymmetryCenter({4.4, 9.8}); // snaps to half pixels: through pixel 4's middle
    CHECK(f.editor.symmetryCenter() == QPointF(4.5, 10));
    drag(f.editor, {{2, 1}});
    CHECK(painted(f.editor, 6, 1));
    f.editor.undo();
    f.editor.setSymmetryCenter({-3, 99}); // kept on the canvas
    CHECK(f.editor.symmetryCenter() == QPointF(0, 16));

    f.editor.setSymmetryCenter({5, 5});
    f.editor.newDocument(10, 6); // a second tab: centered
    CHECK(f.editor.symmetryCenter() == QPointF(5, 3));
    f.editor.setCurrentDocument(0);
    CHECK(f.editor.symmetryCenter() == QPointF(5, 5)); // the first tab kept its own
    f.editor.resizeCanvas(QRect(0, 0, 20, 16));
    CHECK(f.editor.symmetryCenter() == QPointF(10, 8));
    f.editor.setSymmetryCenter({3, 3});
    f.editor.centerSymmetry();
    CHECK(f.editor.symmetryCenter() == QPointF(10, 8));
}

TEST_CASE("symmetry: a custom brush is turned across a diagonal")
{
    Fixture f;
    makeLBrush(f.editor); // red red / blue . ; its source pixels stay at 1..2 x 1..2
    f.editor.setSymmetryFalling(true);
    drag(f.editor, {{3, 6}}); // covers 2..3 x 5..6, mirrored onto 5..6 x 2..3, transposed
    CHECK(at(f.editor, 2, 5) == kRed);
    CHECK(at(f.editor, 3, 5) == kRed);
    CHECK(at(f.editor, 2, 6) == kBlue);
    CHECK(at(f.editor, 5, 2) == kRed);
    CHECK(at(f.editor, 5, 3) == kRed);
    CHECK(at(f.editor, 6, 2) == kBlue);
    CHECK(at(f.editor, 6, 3).alpha() == 0);
}

namespace {

// A double-click as the canvas delivers it: press, release, press, double-click, release.
void doubleClick(Editor& editor, QPoint p, Qt::KeyboardModifiers mods = {})
{
    editor.beginStroke(p, false, mods);
    editor.endStroke();
    editor.beginStroke(p, false, mods);
    editor.selectGridCell(p, mods);
    editor.endStroke();
}

} // namespace

TEST_CASE("grid cell select: the cell under the pointer, offset grids, cut to the canvas")
{
    Fixture f; // 16 x 16
    f.editor.setTool(Editor::Select);
    f.editor.setGrid(0, 0, 4, 4);
    doubleClick(f.editor, {5, 6});
    CHECK(f.editor.selection() == QRect(4, 4, 4, 4));
    doubleClick(f.editor, {15, 15});
    CHECK(f.editor.selection() == QRect(12, 12, 4, 4));

    f.editor.setGrid(1, 2, 5, 5); // pixels left of / above the offset are in the cells before it
    doubleClick(f.editor, {0, 0});
    CHECK(f.editor.selection() == QRect(0, 0, 1, 2)); // cell -4..0 x -3..1, cut
    doubleClick(f.editor, {15, 15});
    CHECK(f.editor.selection() == QRect(11, 12, 5, 4)); // 11..15 x 12..16, cut
}

TEST_CASE("grid cell select: Shift adds, Alt subtracts; one undo step after a deselecting click")
{
    Fixture f;
    f.editor.setTool(Editor::Select);
    f.editor.setGrid(0, 0, 4, 4);
    drag(f.editor, {{1, 1}, {10, 2}});
    const QRect dragged = f.editor.selection();

    doubleClick(f.editor, {5, 9}); // the first click deselects; the cell replaces its undo step
    CHECK(f.editor.selection() == QRect(4, 8, 4, 4));
    CHECK_FALSE(f.editor.canRedo()); // the Deselect is gone, not waiting as a redo
    f.editor.undo();
    CHECK(f.editor.selection() == dragged);

    doubleClick(f.editor, {5, 9});
    doubleClick(f.editor, {13, 9}, Qt::ShiftModifier);
    CHECK(f.editor.selection() == QRect(4, 8, 12, 4));
    CHECK(f.editor.selectionContains({13, 9}));
    CHECK_FALSE(f.editor.selectionContains({9, 9}));
    doubleClick(f.editor, {13, 9}, Qt::AltModifier);
    CHECK(f.editor.selection() == QRect(4, 8, 4, 4));
    f.editor.undo(); // each Shift / Alt cell is its own step
    CHECK(f.editor.selection() == QRect(4, 8, 12, 4));
}

TEST_CASE("grid cell select: a double-click inside a selection moves nothing")
{
    Fixture f;
    dot(f.editor, {5, 5}, kRed);
    f.editor.setTool(Editor::Select);
    f.editor.setGrid(0, 0, 8, 8);
    drag(f.editor, {{4, 4}, {7, 7}});
    doubleClick(f.editor, {5, 5});
    CHECK(f.editor.selection() == QRect(0, 0, 8, 8));
    CHECK(at(f.editor, 5, 5) == kRed);
    CHECK(paintedCount(f.editor) == 1);
}

namespace {

void click(Editor& editor, QPoint p, Qt::KeyboardModifiers mods = {})
{
    editor.beginStroke(p, false, mods);
    editor.endStroke();
}

} // namespace

TEST_CASE("lasso: only the outline while dragging, the shape when let go")
{
    Fixture f;
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{10, 10}, {12, 12}});
    const QRect before = f.editor.selection();
    f.editor.setTool(Editor::Lasso);
    f.editor.beginStroke({0, 0}, false, Qt::ShiftModifier);
    f.editor.moveStroke({8, 0}, Qt::ShiftModifier);
    f.editor.moveStroke({0, 8}, Qt::ShiftModifier);
    CHECK(f.editor.selection() == before); // unchanged mid-drag
    f.editor.endStroke();
    CHECK(f.editor.selectionContains({2, 2}));
    CHECK(f.editor.selectionContains({11, 11}));
    f.editor.undo(); // one step
    CHECK(f.editor.selection() == before);
}

TEST_CASE("lasso: Alt draws a straight segment to where it is let go")
{
    Fixture f;
    f.editor.setTool(Editor::Lasso);
    f.editor.beginStroke({0, 0}, false, {});
    f.editor.moveStroke({1, 0}, {});
    // Alt held: the pointer wanders, only where it ends counts.
    f.editor.moveStroke({0, 10}, Qt::AltModifier);
    f.editor.moveStroke({10, 0}, Qt::AltModifier);
    f.editor.moveStroke({10, 0}, {}); // Alt let go: that point stays
    f.editor.moveStroke({10, 10}, {});
    f.editor.endStroke();
    CHECK(f.editor.selectionContains({5, 2}));  // inside the triangle (0,0) (10,0) (10,10)
    CHECK(f.editor.selectionContains({9, 5}));
    CHECK_FALSE(f.editor.selectionContains({1, 8})); // the wander to (0,10) isn't a corner
}

TEST_CASE("polygon lasso: corners, closing on the first one, by Enter or double-click")
{
    Fixture f;
    f.editor.setTool(Editor::Lasso);
    f.editor.setLassoPolygon(true);
    click(f.editor, {0, 0});
    CHECK(f.editor.polygonActive());
    click(f.editor, {8, 0});
    click(f.editor, {0, 8});
    CHECK_FALSE(f.editor.hasSelection()); // nothing until it's closed
    click(f.editor, {0, 0}); // the first corner closes it
    CHECK_FALSE(f.editor.polygonActive());
    CHECK(f.editor.selection() == QRect(0, 0, 9, 9));
    CHECK(f.editor.selectionContains({2, 2}));
    CHECK_FALSE(f.editor.selectionContains({7, 7}));
    f.editor.undo();
    CHECK_FALSE(f.editor.hasSelection()); // one step

    // Closed by Enter (closePolygon) after a corner too many was taken back.
    click(f.editor, {2, 2});
    click(f.editor, {6, 2});
    click(f.editor, {15, 15});
    f.editor.removePolygonCorner();
    click(f.editor, {6, 6});
    click(f.editor, {2, 6});
    f.editor.closePolygon();
    CHECK(f.editor.selection() == QRect(2, 2, 5, 5));

    // Shift on later corners snaps them to 45° steps.
    click(f.editor, {1, 10});
    click(f.editor, {9, 11}, Qt::ShiftModifier); // snapped to (9, 10)
    click(f.editor, {9, 14});
    f.editor.closePolygon();
    CHECK(f.editor.selectionContains({9, 10}));
    CHECK_FALSE(f.editor.selectionContains({9, 9}));
    CHECK(f.editor.selection().top() == 10);
}

TEST_CASE("polygon lasso: modes from the first click, cancelling, a lone click deselects")
{
    Fixture f;
    f.editor.setTool(Editor::Select);
    drag(f.editor, {{0, 0}, {3, 3}});
    f.editor.setTool(Editor::Lasso);
    f.editor.setLassoPolygon(true);

    click(f.editor, {8, 8}, Qt::ShiftModifier); // adds
    click(f.editor, {12, 8}, Qt::ShiftModifier); // Shift picked the mode: no snapping
    click(f.editor, {12, 12});
    f.editor.closePolygon();
    CHECK(f.editor.selectionContains({1, 1}));
    CHECK(f.editor.selectionContains({11, 9}));

    click(f.editor, {0, 0}, Qt::AltModifier); // subtracts
    click(f.editor, {1, 0});
    click(f.editor, {1, 1});
    click(f.editor, {0, 1});
    f.editor.closePolygon();
    CHECK_FALSE(f.editor.selectionContains({0, 0}));
    CHECK(f.editor.selectionContains({2, 2}));
    const QRect kept = f.editor.selection();

    // Esc, undo, a tool switch or removing the last corner cancel it; nothing changes.
    click(f.editor, {5, 5});
    click(f.editor, {9, 5});
    f.editor.cancelPolygon();
    CHECK_FALSE(f.editor.polygonActive());
    click(f.editor, {5, 5});
    f.editor.undo();
    CHECK_FALSE(f.editor.polygonActive());
    click(f.editor, {5, 5});
    f.editor.setTool(Editor::Pencil);
    CHECK_FALSE(f.editor.polygonActive());
    f.editor.setTool(Editor::Lasso);
    click(f.editor, {5, 5});
    f.editor.removePolygonCorner();
    CHECK_FALSE(f.editor.polygonActive());
    CHECK(f.editor.selection() == kept);

    // A lone click (closed with one corner) deselects, as a freehand lasso click does.
    click(f.editor, {5, 5});
    click(f.editor, {5, 5}); // a double-click's second press adds nothing
    f.editor.closePolygon();
    CHECK_FALSE(f.editor.hasSelection());
}

TEST_CASE("lasso path: the line to draw while a lasso is being made")
{
    Fixture f;
    f.editor.setTool(Editor::Lasso);
    CHECK(f.editor.lassoPath().isEmpty());
    f.editor.beginStroke({1, 1}, false);
    f.editor.moveStroke({5, 1});
    CHECK(f.editor.lassoPath() == QList<QPointF>{{1.5, 1.5}, {5.5, 1.5}}); // pixel centers
    f.editor.endStroke();
    CHECK(f.editor.lassoPath().isEmpty());

    f.editor.setLassoPolygon(true);
    f.editor.beginStroke({2, 2}, false);
    f.editor.endStroke();
    f.editor.setLinePreview({7, 3}, true); // Shift: snapped to (7, 2)
    CHECK(f.editor.lassoPath() == QList<QPointF>{{2.5, 2.5}, {7.5, 2.5}});
    f.editor.setLinePreview({7, 3}, false);
    CHECK(f.editor.lassoPath().last() == QPointF(7.5, 3.5));
    f.editor.cancelPolygon();
    CHECK(f.editor.lassoPath().isEmpty());
}

TEST_CASE("tools: effects preview live, then apply as one step or put back")
{
    Editor editor;
    editor.newSprite({{"width", 5}, {"height", 5}});
    CHECK_FALSE(editor.beginEffect()); // nothing on the cel yet
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    const QImage drawn = editor.composite().copy();

    const QVariantMap blueOutline{{"kind", "outline"}, {"color", QColor(0, 0, 255)}, {"inside", false},
                                  {"square", false}};
    REQUIRE(editor.beginEffect());
    editor.previewEffect(blueOutline);
    CHECK(editor.composite().pixelColor(2, 1) == QColor(0, 0, 255));
    editor.cancelEffect();
    CHECK(editor.composite() == drawn);
    CHECK_FALSE(editor.effectActive());

    // Previews replace each other; apply is one undo step.
    REQUIRE(editor.beginEffect());
    editor.previewEffect({{"kind", "adjust"}, {"hue", 120}, {"saturation", 0}, {"lightness", 0}});
    editor.previewEffect(blueOutline);
    CHECK(editor.composite().pixelColor(2, 2) == QColor(255, 0, 0)); // the adjust preview is gone
    editor.applyEffect("Outline");
    CHECK(editor.composite().pixelColor(1, 2) == QColor(0, 0, 255));
    editor.undo();
    CHECK(editor.composite() == drawn);

    // Undo while previewing only puts the preview back.
    REQUIRE(editor.beginEffect());
    editor.previewEffect({{"kind", "replace"}, {"from", QColor(255, 0, 0)}, {"to", QColor(0, 255, 0)}, {"tolerance", 0}});
    CHECK(editor.composite().pixelColor(2, 2) == QColor(0, 255, 0));
    editor.undo();
    CHECK(editor.composite() == drawn);
    CHECK(editor.canRedo()); // the outline is still there to redo
}

TEST_CASE("tools: effects apply to more cels; another edit ends the preview")
{
    Editor editor;
    editor.newSprite({{"width", 3}, {"height", 1}, {"frames", 3}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    for (int f = 0; f < 3; ++f) {
        editor.setCurrentFrame(f);
        editor.beginStroke({0, 0}, false, {});
        editor.endStroke();
    }
    const QVariantMap redToBlue{{"kind", "replace"}, {"from", QColor(255, 0, 0)}, {"to", QColor(0, 0, 255)},
                                {"tolerance", 0}};
    const auto colorAt = [&](int frame) { return editor.frameImage(frame).pixelColor(0, 0); };

    editor.setCurrentFrame(1);
    REQUIRE(editor.beginEffect());
    editor.previewEffect(redToBlue);
    editor.applyEffect("Replace Color", "cel");
    CHECK(colorAt(0) == QColor(255, 0, 0));
    CHECK(colorAt(1) == QColor(0, 0, 255));
    editor.undo();

    REQUIRE(editor.beginEffect());
    editor.previewEffect(redToBlue);
    editor.applyEffect("Replace Color", "layer");
    for (int f = 0; f < 3; ++f)
        CHECK(colorAt(f) == QColor(0, 0, 255));
    editor.undo(); // one step for all of them
    for (int f = 0; f < 3; ++f)
        CHECK(colorAt(f) == QColor(255, 0, 0));

    // Another edit while previewing puts the preview back first.
    REQUIRE(editor.beginEffect());
    editor.previewEffect(redToBlue);
    editor.addLayer();
    CHECK_FALSE(editor.effectActive());
    CHECK(colorAt(1) == QColor(255, 0, 0));
    editor.undo();

    // Picking reads the cel as it was before the preview.
    editor.setCurrentLayer(0);
    REQUIRE(editor.beginEffect());
    editor.previewEffect(redToBlue);
    CHECK(editor.effectColorAt({0, 0}) == QColor(255, 0, 0));
    CHECK(editor.effectColorAt({2, 0}).alpha() == 0);
    editor.cancelEffect();
}

TEST_CASE("tools: the effect preview covers the other layers of the frame in its scope")
{
    Editor editor;
    editor.newSprite({{"width", 3}, {"height", 1}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    editor.addLayer();
    editor.beginStroke({1, 0}, false, {});
    editor.endStroke();
    const QImage drawn = editor.composite().copy();
    const QVariantMap redToBlue{{"kind", "replace"}, {"from", QColor(255, 0, 0)}, {"to", QColor(0, 0, 255)},
                                {"tolerance", 0}};

    REQUIRE(editor.beginEffect());
    editor.setEffectScope("sprite");
    editor.previewEffect(redToBlue);
    CHECK(editor.composite().pixelColor(0, 0) == QColor(0, 0, 255)); // the layer below
    CHECK(editor.composite().pixelColor(1, 0) == QColor(0, 0, 255));
    editor.setEffectScope("cel"); // back to just this one
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 0, 0));
    CHECK(editor.composite().pixelColor(1, 0) == QColor(0, 0, 255));
    editor.setEffectScope("sprite");
    editor.cancelEffect();
    CHECK(editor.composite() == drawn);

    REQUIRE(editor.beginEffect());
    editor.setEffectScope("sprite");
    editor.previewEffect(redToBlue);
    editor.applyEffect("Replace Color", "sprite");
    CHECK(editor.composite().pixelColor(0, 0) == QColor(0, 0, 255));
    editor.undo(); // one step for both layers
    CHECK(editor.composite() == drawn);
}

TEST_CASE("tools: the curves effect inverts a channel")
{
    Editor editor;
    editor.newSprite({{"width", 2}, {"height", 1}});
    editor.setPrimaryColor(QColor(10, 100, 200));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    const QVariantList identity{0, 0, 255, 255};
    CHECK(editor.curveTable({0, 255, 255, 0}).size() == 256);
    CHECK(editor.curveTable({0, 255, 255, 0})[0].toInt() == 255);
    REQUIRE(editor.beginEffect());
    editor.previewEffect({{"kind", "curves"}, {"master", identity}, {"red", QVariantList{0, 255, 255, 0}},
                          {"green", identity}, {"blue", identity}});
    CHECK(editor.composite().pixelColor(0, 0) == QColor(245, 100, 200));
    editor.applyEffect("Color Curves");
    editor.undo();
    CHECK(editor.composite().pixelColor(0, 0) == QColor(10, 100, 200));
}

TEST_CASE("tools: alpha lock keeps painting on the layer's own pixels")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 1}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({1, 0}, false, {});
    editor.endStroke();

    editor.setLayerAlphaLock(0, true);
    CHECK(editor.layers()[0].toMap().value("alphaLock").toBool());
    editor.setPrimaryColor(QColor(0, 0, 255));
    for (int x = 0; x < 4; ++x) {
        editor.beginStroke({x, 0}, false, {});
        editor.endStroke();
    }
    QImage shown = editor.composite();
    CHECK(shown.pixelColor(0, 0).alpha() == 0);
    CHECK(shown.pixelColor(1, 0) == QColor(0, 0, 255));
    CHECK(shown.pixelColor(2, 0).alpha() == 0);

    // A fill over only empty pixels changes nothing; the eraser can't erase.
    const bool couldUndo = editor.canUndo();
    editor.setTool(Editor::Fill);
    editor.setFillContiguous(true);
    editor.beginStroke({3, 0}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(3, 0).alpha() == 0);
    CHECK(editor.canUndo() == couldUndo);
    editor.setTool(Editor::Eraser);
    editor.beginStroke({1, 0}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(1, 0) == QColor(0, 0, 255));

    // Undoable, and saved with the file.
    QTemporaryDir dir;
    const QUrl file = QUrl::fromLocalFile(dir.filePath("locked.aseprite"));
    REQUIRE(editor.saveFileAs(file));
    Editor reopened;
    REQUIRE(reopened.openPath(file.toLocalFile()));
    CHECK(reopened.layers()[0].toMap().value("alphaLock").toBool());
    editor.undo(); // the one stroke that changed a pixel (the others were no-ops)
    CHECK(editor.composite().pixelColor(1, 0) == QColor(255, 0, 0));
    editor.undo(); // the lock
    CHECK_FALSE(editor.layers()[0].toMap().value("alphaLock").toBool());
}

TEST_CASE("tools: fill and magic wand can sample every visible layer")
{
    // Line art on the bottom layer: a red ring around (2, 2).
    Editor editor;
    editor.newSprite({{"width", 5}, {"height", 5}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    for (const QPoint p : {QPoint(1, 1), QPoint(2, 1), QPoint(3, 1), QPoint(1, 2), QPoint(3, 2), QPoint(1, 3),
                           QPoint(2, 3), QPoint(3, 3)}) {
        editor.beginStroke(p, false, {});
        editor.endStroke();
    }
    editor.addLayer(); // colors go on a layer of their own
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.setTool(Editor::Fill);
    editor.setFillContiguous(true);

    // This layer only: it's empty, so the fill takes all of it.
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    CHECK(editor.frameImage(0).pixelColor(0, 0) == QColor(0, 0, 255));
    editor.undo();

    // All layers: only inside the ring.
    editor.setSampleMerged(true);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    const QImage shown = editor.frameImage(0);
    CHECK(shown.pixelColor(2, 2) == QColor(0, 0, 255));
    CHECK(shown.pixelColor(0, 0).alpha() == 0);
    CHECK(shown.pixelColor(1, 1) == QColor(255, 0, 0)); // the ring is on the other layer, untouched
    editor.undo();

    editor.setTool(Editor::MagicWand);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    CHECK(editor.selection() == QRect(2, 2, 1, 1));
}

TEST_CASE("tools: Alt-click picks a color, Cmd / Ctrl-click picks the layer")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 1}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    editor.addLayer();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({1, 0}, false, {});
    editor.endStroke();
    const bool couldUndo = editor.canUndo();

    // Alt with the pencil: picks, drags keep picking, nothing is painted.
    editor.setPrimaryColor(QColor(0, 255, 0));
    editor.beginStroke({0, 0}, false, Qt::AltModifier);
    CHECK(editor.primaryColor() == QColor(255, 0, 0));
    editor.moveStroke({1, 0}, Qt::AltModifier);
    CHECK(editor.primaryColor() == QColor(0, 0, 255));
    editor.endStroke();
    CHECK(editor.composite().pixelColor(2, 0).alpha() == 0);
    editor.setTool(Editor::Fill);
    editor.beginStroke({0, 0}, true, Qt::AltModifier); // right button: the secondary
    editor.endStroke();
    CHECK(editor.secondaryColor() == QColor(255, 0, 0));
    CHECK(editor.canUndo() == couldUndo);

    // Cmd / Ctrl-click: the topmost layer with a pixel there.
    CHECK(editor.currentLayer() == 1);
    editor.beginStroke({0, 0}, false, Qt::ControlModifier);
    editor.endStroke();
    CHECK(editor.currentLayer() == 0);
    CHECK(editor.selectLayerAt({1, 0}));
    CHECK(editor.currentLayer() == 1);
    CHECK(editor.layerNameAt({0, 0}) == editor.layers()[0].toMap().value("name").toString());
    CHECK(editor.layerNameAt({3, 0}).isEmpty());
    CHECK_FALSE(editor.selectLayerAt({3, 0})); // nothing there
    CHECK(editor.currentLayer() == 1);
}

TEST_CASE("tools: the tiled view paints on copies and wraps across edges")
{
    Editor editor;
    editor.newSprite({{"width", 4}, {"height", 4}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    const auto red = [&](int x, int y) { return editor.composite().pixelColor(x, y) == QColor(255, 0, 0); };

    // Off: a click past the edge paints nothing.
    editor.beginStroke({5, 1}, false, {});
    editor.endStroke();
    CHECK_FALSE(red(1, 1));

    editor.setTileMode(3);
    // On a copy: lands in the sprite.
    editor.beginStroke({5, 1}, false, {});
    editor.endStroke();
    CHECK(red(1, 1));
    editor.undo();
    CHECK_FALSE(red(1, 1));

    // A drag across the right edge comes back on the left.
    editor.beginStroke({2, 2}, false, {});
    editor.moveStroke({5, 2}, {});
    editor.endStroke();
    for (int x = 0; x < 4; ++x)
        CHECK(red(x, 2));
    editor.undo();

    // A 3 px brush on the corner wraps on both axes.
    editor.setBrushSize(3);
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    CHECK(red(3, 3));
    CHECK(red(0, 3));
    CHECK(red(3, 0));
    editor.undo();
    CHECK_FALSE(red(3, 3));

    // Only X: the top edge doesn't wrap.
    editor.setTileMode(1);
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    CHECK(red(3, 0));
    CHECK_FALSE(red(0, 3));
    CHECK(editor.wrapPixel({-1, -1}) == QPoint(3, -1));
}

TEST_CASE("tools: blur previews through the effect flow")
{
    Editor editor;
    editor.newSprite({{"width", 3}, {"height", 1}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({1, 0}, false, {});
    editor.endStroke();

    REQUIRE(editor.beginEffect());
    editor.previewEffect({{"kind", "blur"}, {"radius", 1}, {"gaussian", false}, {"keepShape", true}});
    const QColor mixed = editor.composite().pixelColor(0, 0);
    CHECK(mixed.red() > 0);
    CHECK(mixed.blue() > 0);
    CHECK(editor.composite().pixelColor(2, 0).alpha() == 0); // the shape is kept
    editor.applyEffect("Blur");
    editor.undo();
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 0, 0));
}

TEST_CASE("tools: the blur and jumble brushes")
{
    Editor editor;
    editor.newSprite({{"width", 3}, {"height", 1}});
    editor.setTool(Editor::Blur);
    CHECK(editor.usesBrush(Editor::Blur));
    editor.beginStroke({1, 0}, false, {}); // an empty cel: nothing to blur, no undo step
    editor.endStroke();
    CHECK_FALSE(editor.canUndo());

    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({1, 0}, false, {});
    editor.endStroke();

    editor.setTool(Editor::Blur);
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({1, 0}, {});
    editor.endStroke();
    const QColor left = editor.composite().pixelColor(0, 0);
    CHECK(left.red() > 0);
    CHECK(left.blue() > 0);
    editor.undo(); // one step
    CHECK(editor.composite().pixelColor(0, 0) == QColor(255, 0, 0));

    // A custom brush picked for the pencil doesn't change the blur brush's size.
    editor.setBrushShape(Editor::CustomBrush);
    CHECK(editor.brushExtent() == QSize(1, 1));
}

TEST_CASE("tools: text floats while typed, lands on commit")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 16}});
    REQUIRE(editor.fonts().size() == 6); // three files, 3 x 5 and two bold
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Text);
    editor.beginStroke({2, 3}, false, {});
    editor.endStroke();
    CHECK(editor.textActive());
    CHECK_FALSE(editor.floating()); // nothing typed yet

    editor.setText("Hi");
    REQUIRE(editor.floating());
    const QRect box = editor.transformBox();
    CHECK(box.topLeft() == QPoint(2, 3));
    CHECK(box.height() == 7); // 5 x 7
    editor.setTextFont(2);    // 6 x 10: re-rendered where it was
    CHECK(editor.transformBox().topLeft() == QPoint(2, 3));
    CHECK(editor.transformBox().height() == 10);
    editor.setText("");
    CHECK_FALSE(editor.floating());
    CHECK(editor.textActive());
    editor.setText("Hi");

    editor.commitFloating();
    CHECK_FALSE(editor.textActive());
    bool inked = false;
    const QImage shown = editor.composite();
    for (int y = 0; y < shown.height(); ++y)
        for (int x = 0; x < shown.width(); ++x)
            inked = inked || shown.pixelColor(x, y) == QColor(255, 0, 0);
    CHECK(inked);
    editor.undo();
    CHECK(editor.composite().pixelColor(3, 5).alpha() == 0);

    // Esc drops it.
    editor.beginStroke({0, 0}, false, {});
    editor.endStroke();
    editor.setText("x");
    editor.cancelFloating();
    CHECK_FALSE(editor.textActive());
    CHECK_FALSE(editor.floating());
}

TEST_CASE("tools: transform sizes and angles by number keep the pivot")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({10, 10}, false, {});
    editor.moveStroke({13, 11}, {});
    editor.endStroke();
    editor.startTransform();
    REQUIRE(editor.floating());
    const QRect start = editor.transformBox();

    // Top-left pivot: a new size grows right and down.
    editor.setTransformPivot(0);
    editor.resizeTransform(start.width() * 2, start.height() * 2);
    CHECK(editor.transformBox().topLeft() == start.topLeft());
    // Bottom-right pivot: that corner stays.
    editor.setTransformPivot(8);
    const QPoint corner = editor.transformBox().bottomRight();
    editor.resizeTransform(start.width(), start.height());
    CHECK(editor.transformBox().bottomRight() == corner);

    // Center pivot (the default): rotating doesn't move the box.
    editor.setTransformPivot(4);
    const QRect before = editor.transformBox();
    editor.setTransformAngle(90);
    CHECK(editor.transformBox() == before);
    // Top-left pivot: a half turn moves the center to the other side of it.
    editor.setTransformAngle(0);
    editor.setTransformPivot(0);
    const QRect flat = editor.transformBox();
    editor.setTransformAngle(180);
    const QRect turned = editor.transformBox();
    CHECK(turned.x() == flat.x() - flat.width());
    CHECK(turned.y() == flat.y() - flat.height());
    // Many small turns (a handle drag) come back without drifting.
    for (int a = 179; a >= 0; --a)
        editor.setTransformAngle(a);
    CHECK(editor.transformBox() == flat);
    editor.cancelFloating();
}

TEST_CASE("tools: text with several lines and spacing")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 32}});
    editor.setTool(Editor::Text);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    editor.setTextFont(1); // 5 x 7
    editor.setText("AB");
    const int oneLine = editor.transformBox().height();
    const int narrow = editor.transformBox().width();
    CHECK(oneLine == 7);

    editor.setText("AB\nCD"); // line spacing 1 by default
    CHECK(editor.transformBox().height() == 2 * 7 + 1);
    editor.setTextLineSpacing(4);
    CHECK(editor.transformBox().height() == 2 * 7 + 4);
    editor.setTextLetterSpacing(3); // one gap per line of two letters
    CHECK(editor.transformBox().width() == narrow + 3);
    editor.setText("AB\r\nCD"); // Windows line ends count once
    CHECK(editor.transformBox().height() == 2 * 7 + 4);
    editor.cancelFloating();
}

TEST_CASE("tools: the text caret follows the typing, and a click on the text just placed opens it again")
{
    Editor editor;
    editor.newSprite({{"width", 40}, {"height", 20}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Text);
    editor.setTextFont(1); // 5 x 7
    editor.beginStroke({4, 5}, false, {});
    editor.endStroke();
    CHECK(editor.textCaret() == QRect(4, 5, 1, 7)); // nothing typed: at the click

    editor.setText("ab");
    editor.setTextCursor(2);
    const QRect end = editor.textCaret();
    CHECK(end.x() > 4);
    CHECK(end.height() == 7);
    editor.setTextCursor(1);
    CHECK(editor.textCaret().x() < end.x());
    editor.setText("ab\ncd");
    editor.setTextCursor(4); // after the c, on the second line
    CHECK(editor.textCaret().y() > 5);

    editor.setText("ab");
    editor.commitFloating();
    CHECK_FALSE(editor.textActive());
    CHECK(editor.textCaret().isNull());
    const QImage placed = editor.composite().copy();
    const bool couldUndo = editor.canUndo();

    // A click on it: the pixels go and the text is typed again.
    editor.beginStroke({5, 6}, false, {});
    editor.endStroke();
    CHECK(editor.textActive());
    CHECK(editor.text() == "ab");
    CHECK(editor.floating());
    editor.setText("abc");
    editor.commitFloating();
    CHECK(editor.composite() != placed);
    CHECK(couldUndo);
    editor.undo(); // back to nothing: the reopened text was one step
    CHECK(editor.composite().pixelColor(5, 6).alpha() == 0);

    // After another edit it is just pixels.
    editor.setText("");
    editor.beginStroke({4, 5}, false, {});
    editor.endStroke();
    editor.setText("xy");
    editor.commitFloating();
    editor.setTool(Editor::Pencil);
    editor.beginStroke({30, 15}, false, {});
    editor.endStroke();
    editor.setTool(Editor::Text);
    editor.beginStroke({5, 6}, false, {});
    editor.endStroke();
    CHECK(editor.textActive());
    CHECK(editor.text().isEmpty()); // a new text, not the old one
}

TEST_CASE("tools: text can be centered or right-aligned")
{
    Editor editor;
    editor.newSprite({{"width", 60}, {"height", 30}});
    editor.setTool(Editor::Text);
    editor.setTextFont(1);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    editor.setText("wide line\nx");
    editor.setTextAlign(0);
    const int width = editor.transformBox().width();
    CHECK(editor.textAlign() == 0);
    editor.setTextAlign(1);
    CHECK(editor.textAlign() == 1);
    CHECK(editor.transformBox().width() == width); // the box is the widest line either way
    editor.setTextAlign(2);
    CHECK(editor.transformBox().width() == width);
    editor.setTextAlign(9);
    CHECK(editor.textAlign() == 2); // clamped
    editor.cancelFloating();
}

TEST_CASE("grid snapping: selections and rectangles cover whole cells, lines end on corners, moves go by cells")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.setGrid(0, 0, 16, 16);
    editor.setPrimaryColor(QColor(255, 0, 0));

    // The helpers.
    CHECK(editor.snapRect(QRect(5, 5, 3, 3)) == QRect(5, 5, 3, 3)); // off: as is
    editor.setSnapToGrid(true);
    CHECK(editor.snapRect(QRect(5, 5, 3, 3)) == QRect(0, 0, 16, 16));
    CHECK(editor.snapRect(QRect(10, 3, 10, 3)) == QRect(0, 0, 32, 16)); // across a line: both cells
    CHECK(editor.snapRect(QRect(16, 16, 16, 16)) == QRect(16, 16, 16, 16)); // already a cell
    CHECK(editor.snapEdge(23, false) == 16);
    CHECK(editor.snapEdge(25, false) == 32);
    CHECK(editor.snapMove(QPoint(0, 0), QPoint(7, 9)) == QPoint(0, 16));
    editor.setGrid(3, 5, 16, 16); // an offset grid
    CHECK(editor.snapRect(QRect(4, 6, 2, 2)) == QRect(3, 5, 16, 16));
    editor.setGrid(0, 0, 16, 16);

    // A selection drag covers the cells it touches.
    editor.setTool(Editor::Select);
    editor.beginStroke({5, 5}, false, {});
    editor.moveStroke({20, 8}, {});
    editor.endStroke();
    CHECK(editor.selection() == QRect(0, 0, 32, 16));

    // A rectangle (filled) paints whole cells.
    editor.deselect();
    editor.setTool(Editor::Rectangle);
    editor.setFillShapes(true);
    editor.beginStroke({20, 20}, false, {});
    editor.moveStroke({22, 22}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(16, 16) == QColor(255, 0, 0));
    CHECK(editor.composite().pixelColor(31, 31) == QColor(255, 0, 0));
    CHECK(editor.composite().pixelColor(15, 16).alpha() == 0);
    CHECK(editor.composite().pixelColor(32, 31).alpha() == 0);

    // With snapping off the same drag is exact.
    editor.undo();
    editor.setSnapToGrid(false);
    editor.beginStroke({20, 20}, false, {});
    editor.moveStroke({22, 22}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(21, 21) == QColor(255, 0, 0));
    CHECK(editor.composite().pixelColor(16, 16).alpha() == 0);
}

TEST_CASE("grid snapping: a selection moves a whole cell at a time")
{
    Editor editor;
    editor.newSprite({{"width", 64}, {"height", 64}});
    editor.setGrid(0, 0, 16, 16);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.setBrushSize(1);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();

    editor.setSnapToGrid(true);
    editor.setTool(Editor::Select);
    editor.beginStroke({4, 4}, false, {});
    editor.moveStroke({6, 6}, {});
    editor.endStroke();
    REQUIRE(editor.selection() == QRect(0, 0, 16, 16));

    // Drag inside the selection by 19 pixels: the cell goes 16, not 19.
    editor.beginStroke({4, 4}, false, {});
    editor.moveStroke({23, 4}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(18, 2) == QColor(255, 0, 0));
    CHECK(editor.composite().pixelColor(2, 2).alpha() == 0);
    CHECK(editor.composite().pixelColor(21, 2).alpha() == 0);
}

TEST_CASE("gradient: linear and radial, smooth or dithered between the two colors, inside the selection")
{
    const QColor red(255, 0, 0), blue(0, 0, 255);
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 8}});
    editor.setPrimaryColor(red);
    editor.setSecondaryColor(blue);
    editor.setTool(Editor::Gradient);

    // Linear, smooth: red at the start, blue at the end, blended between.
    editor.beginStroke({0, 4}, false, {});
    editor.moveStroke({31, 4}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(0, 0) == red);
    CHECK(editor.composite().pixelColor(31, 7) == blue);
    const QColor mid = editor.composite().pixelColor(16, 3);
    CHECK(mid.red() > 100);
    CHECK(mid.red() < 160);
    CHECK(mid.blue() > 100);
    CHECK(mid.red() + mid.blue() >= 250); // a blend, not a third color
    CHECK(editor.composite().pixelColor(16, 0) == editor.composite().pixelColor(16, 7)); // constant across
    editor.undo();
    CHECK(editor.composite().pixelColor(0, 0).alpha() == 0); // one undo step

    // Dithered: only the two colors, mixed in the middle.
    editor.setGradientDither(2);
    editor.beginStroke({0, 4}, false, {});
    editor.moveStroke({31, 4}, {});
    editor.endStroke();
    int reds = 0, blues = 0, others = 0;
    for (int x = 8; x < 24; ++x)
        for (int y = 0; y < 8; ++y) {
            const QColor c = editor.composite().pixelColor(x, y);
            reds += c == red;
            blues += c == blue;
            others += c != red && c != blue;
        }
    CHECK(others == 0);
    CHECK(reds > 10);
    CHECK(blues > 10);
    CHECK(editor.composite().pixelColor(0, 3) == red);
    CHECK(editor.composite().pixelColor(31, 3) == blue);
    editor.undo();
    editor.setGradientDither(0);

    // Radial: the same color all round at one distance.
    editor.setGradientShape(1);
    editor.beginStroke({16, 4}, false, {});
    editor.moveStroke({26, 4}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(16, 4) == red);
    CHECK(editor.composite().pixelColor(21, 4) == editor.composite().pixelColor(11, 4));
    CHECK(editor.composite().pixelColor(30, 4) == blue); // past the end stays blue
    editor.undo();

    // Inside the selection only.
    editor.setGradientShape(0);
    editor.setTool(Editor::Select);
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({9, 7}, {});
    editor.endStroke();
    editor.setTool(Editor::Gradient);
    editor.beginStroke({0, 4}, false, {});
    editor.moveStroke({31, 4}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(5, 4).alpha() == 255);
    CHECK(editor.composite().pixelColor(15, 4).alpha() == 0);
    editor.undo();

    // A fade: the secondary color transparent.
    editor.deselect();
    editor.setSecondaryColor(QColor(0, 0, 255, 0));
    editor.beginStroke({0, 4}, false, {});
    editor.moveStroke({31, 4}, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(0, 4).alpha() == 255);
    CHECK(editor.composite().pixelColor(31, 4).alpha() == 0);
    const int midAlpha = editor.composite().pixelColor(16, 4).alpha();
    CHECK(midAlpha > 60);
    CHECK(midAlpha < 200);
}

TEST_CASE("tiled view: fills and the wand go on across the edges, moves wrap, a seam corner goes with its copy")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 4}});
    editor.setBrushSize(1);
    editor.setPrimaryColor(QColor(255, 0, 0));
    const auto red = [&](int x, int y) { return editor.composite().pixelColor(x, y) == QColor(255, 0, 0); };
    editor.setTileMode(1); // across X

    // A wall at x = 3; the empty area to its left and right is one, round the back.
    editor.setTool(Editor::Pencil);
    for (int y = 0; y < 4; ++y) {
        editor.beginStroke({3, y}, false, {});
        editor.endStroke();
    }
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.setTool(Editor::Fill);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(6, 2) == QColor(0, 0, 255)); // reached through the left edge
    CHECK(editor.composite().pixelColor(4, 0) == QColor(0, 0, 255));
    CHECK(red(3, 1)); // the wall stays
    editor.undo();
    editor.setTileMode(0);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    CHECK(editor.composite().pixelColor(2, 1) == QColor(0, 0, 255));
    CHECK(editor.composite().pixelColor(6, 1).alpha() == 0); // not reached without wrapping
    editor.undo();
    editor.setTileMode(1);

    // The wand sees the same area.
    editor.setTool(Editor::MagicWand);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    CHECK(editor.selectionContains(QPoint(6, 2)));
    CHECK_FALSE(editor.selectionContains(QPoint(3, 2)));
    editor.deselect();
}

TEST_CASE("tiled view: pixels moved over an edge come in on the other side")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 4}});
    editor.setBrushSize(1);
    editor.setPrimaryColor(QColor(255, 0, 0));
    const auto red = [&](int x, int y) { return editor.composite().pixelColor(x, y) == QColor(255, 0, 0); };
    editor.setTileMode(1);
    editor.setTool(Editor::Pencil);
    editor.beginStroke({7, 0}, false, {});
    editor.endStroke();
    editor.setTool(Editor::Select);
    editor.beginStroke({6, 0}, false, {});
    editor.moveStroke({7, 1}, {});
    editor.endStroke();
    REQUIRE(editor.selection() == QRect(6, 0, 2, 2));
    editor.beginStroke({6, 0}, false, {}); // drag inside it by 2
    editor.moveStroke({8, 0}, {});
    editor.endStroke();
    editor.commitFloating(); // the moved pixels float until placed
    CHECK(red(1, 0)); // 7 + 2 = 9, wrapped to 1
    CHECK_FALSE(red(7, 0));
}

TEST_CASE("tiled view: a pixel-perfect corner removed past the edge takes its wrapped copy with it")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 4}});
    editor.setBrushSize(1);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.setTileMode(1);
    const auto red = [&](int x, int y) { return editor.composite().pixelColor(x, y) == QColor(255, 0, 0); };

    // An L over the seam: (7,1), (8,1), (8,2); the corner (8,1) is (0,1) on the canvas.
    editor.beginStroke({7, 1}, false, {});
    editor.moveStroke({8, 1}, {});
    editor.moveStroke({8, 2}, {});
    editor.endStroke();
    CHECK(red(7, 1));
    CHECK(red(0, 2));
    CHECK_FALSE(red(0, 1)); // removed, with no copy left
}

TEST_CASE("effects: Reduce Colors takes an RGBA sprite down to N of its own colors, keeping alpha")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 4}});
    editor.setTool(Editor::Pencil);
    editor.setBrushSize(1);
    // Sixteen different grays, and a half-see-through red.
    for (int x = 0; x < 16; ++x) {
        editor.setPrimaryColor(QColor(x * 16, x * 16, x * 16));
        editor.beginStroke({x, 0}, false, {});
        editor.endStroke();
    }
    editor.setPrimaryColor(QColor(255, 0, 0, 128));
    editor.beginStroke({0, 1}, false, {});
    editor.endStroke();

    const auto colorsUsed = [&] {
        QSet<QRgb> seen;
        const QImage image = editor.composite();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).alpha() == 255)
                    seen.insert(image.pixel(x, y));
        return seen.size();
    };
    CHECK(colorsUsed() == 16);
    const QImage before = editor.composite().copy();

    REQUIRE(editor.beginEffect());
    editor.previewEffect({{"kind", "quantize"}, {"colors", 4}, {"dither", "none"}});
    CHECK(colorsUsed() <= 4);
    CHECK(editor.composite().pixelColor(0, 1).alpha() == 128); // alpha stays
    editor.cancelEffect();
    CHECK(editor.composite() == before);

    REQUIRE(editor.beginEffect());
    editor.previewEffect({{"kind", "quantize"}, {"colors", 4}, {"dither", "ordered"}});
    CHECK(colorsUsed() <= 4);
    editor.applyEffect("Reduce Colors", "cel");
    CHECK(colorsUsed() <= 4);
    editor.undo();
    CHECK(editor.composite() == before);
}

TEST_CASE("tilemaps: a new layer on the tileset a conversion made shows its tiles")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setGrid(0, 0, 8, 8);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.setBrushSize(3);
    editor.beginStroke({4, 4}, false, {});
    editor.endStroke();
    editor.setBrushSize(1);
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({20, 12}, false, {});
    editor.endStroke();

    REQUIRE(editor.convertToTilemap({{"name", "Ground"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    const int tiles = editor.tileCount();
    CHECK(tiles >= 3); // the empty tile and the two made from the drawing
    REQUIRE(editor.tilesetList().size() == 1);

    QSignalSpy changed(&editor, &Editor::tilesetChanged);
    QVariantMap pick;
    pick["tileset"] = 0;
    REQUIRE(editor.addTilemapLayer(pick));
    CHECK(editor.tilemapLayer());
    CHECK(editor.tileCount() == tiles); // the same tileset
    CHECK(changed.count() >= 1);        // so a panel showing it asks again
    CHECK_FALSE(editor.tileImage(1).isNull());
    CHECK(editor.editTiles()); // its tiles are shown to place, not hidden behind the palette

    // A new tileset starts empty: painting fills it, so the pixel mode.
    REQUIRE(editor.addTilemapLayer({{"name", "Walls"}}));
    CHECK_FALSE(editor.editTiles());
}

TEST_CASE("tilemaps: managing tiles remaps the layer and undoes as one step")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 8}});
    editor.setGrid(0, 0, 8, 8);
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({10, 2}, false, {});
    editor.endStroke();
    REQUIRE(editor.convertToTilemap({{"name", "T"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    REQUIRE(editor.tileCount() == 3);
    CHECK(editor.tileUses(1) == 1);
    CHECK(editor.tileUses(2) == 1);
    CHECK(editor.unusedTileCount() == 0);

    const int copy = editor.duplicateTile(1);
    CHECK(copy == 3);
    CHECK(editor.tileCount() == 4);
    CHECK(editor.tileUses(3) == 0);
    CHECK(editor.unusedTileCount() == 1);
    CHECK(editor.duplicateTileCount() == 1);
    // All counts in one call: the empty tile's cells first, and the unused copy at 0.
    const QVariantList counts = editor.tileUseCounts();
    REQUIRE(counts.size() == 4);
    CHECK(counts[1].toInt() == 1);
    CHECK(counts[2].toInt() == 1);
    CHECK(counts[3].toInt() == 0);
    for (int i = 1; i < counts.size(); ++i)
        CHECK(counts[i].toInt() == editor.tileUses(i));

    CHECK(editor.mergeDuplicateTiles() == 1);
    CHECK(editor.tileCount() == 3);

    const QImage second = editor.tileImage(2);
    REQUIRE(editor.moveTile(2, 1));
    CHECK(editor.tileImage(1) == second);
    CHECK(editor.tileUses(1) == 1);

    REQUIRE(editor.deleteTile(1));
    CHECK(editor.tileCount() == 2);
    CHECK(editor.tileUses(1) == 1); // the other tile, shifted down
    editor.undo();
    CHECK(editor.tileCount() == 3);
    CHECK(editor.tileUses(1) == 1);
    CHECK(editor.tileUses(2) == 1);
    CHECK_FALSE(editor.deleteTile(0)); // the empty tile stays
}

TEST_CASE("tilemaps: importing a tileset image makes a tileset and a layer")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QImage sheet(32, 16, QImage::Format_RGBA8888);
    sheet.fill(Qt::transparent);
    sheet.setPixelColor(1, 1, QColor(255, 0, 0));   // tile 1
    sheet.setPixelColor(17, 2, QColor(0, 0, 255));  // tile 2; none in between
    const QString path = dir.filePath("ground.png");
    REQUIRE(sheet.save(path));

    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    REQUIRE(editor.loadTilesetSheet(QUrl::fromLocalFile(path)));
    CHECK(editor.tilesetSheetName() == "ground");
    CHECK(editor.tilesetSheetGrid(16, 16, 0, 0) == QSize(2, 1));
    CHECK(editor.tilesetSheetCells(8, 8, 0, 0).size() == 8);

    REQUIRE(editor.importTileset({{"tileWidth", 16}, {"tileHeight", 16}}));
    CHECK(editor.tilemapLayer());
    CHECK(editor.tileCount() == 3); // the empty tile and the two
    CHECK(editor.tileSize() == QSize(16, 16));
    CHECK(editor.editTiles());
    CHECK(editor.tileImage(1).pixelColor(1, 1) == QColor(255, 0, 0));
    CHECK(editor.tileImage(2).pixelColor(1, 2) == QColor(0, 0, 255));

    // Finer tiles, empty ones skipped; and one undo takes it all back.
    REQUIRE(editor.importTileset({{"tileWidth", 8}, {"tileHeight", 8}, {"skipEmpty", true}}));
    CHECK(editor.tileCount() == 3);
    editor.undo();
    CHECK(editor.tileCount() == 3);
    CHECK(editor.tilesetList().size() == 1);
    CHECK_FALSE(editor.importTileset({{"tileWidth", 64}, {"tileHeight", 64}}));
}

TEST_CASE("tilemaps: importing a sheet can lay the tiles out, merge equal ones, and add to a tileset")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    // Three 16 x 16 tiles side by side: red, blue, red again; each flat-colored.
    QImage sheet(48, 16, QImage::Format_RGBA8888);
    const QColor red(255, 0, 0), blue(0, 0, 255);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 48; ++x)
            sheet.setPixelColor(x, y, x / 16 == 1 ? blue : red);
    const QString path = dir.filePath("row.png");
    REQUIRE(sheet.save(path));

    Editor editor;
    editor.newSprite({{"width", 48}, {"height", 16}});
    REQUIRE(editor.loadTilesetSheet(QUrl::fromLocalFile(path)));

    // The picture's edges give its tile size.
    CHECK(editor.detectTilesetSize() == QSize(16, 16));

    // Laid out as the picture shows it: the canvas looks like the sheet.
    REQUIRE(editor.importTileset({{"tileWidth", 16}, {"tileHeight", 16}, {"layout", true}}));
    CHECK(editor.tileCount() == 4); // the empty tile and three
    const QImage shown = editor.composite();
    CHECK(shown.pixelColor(2, 2) == red);
    CHECK(shown.pixelColor(20, 2) == blue);
    CHECK(shown.pixelColor(40, 2) == red);
    editor.undo();

    // Merged: red once; the third cell is the first tile again, and it still looks the same.
    REQUIRE(editor.importTileset({{"tileWidth", 16}, {"tileHeight", 16}, {"layout", true}, {"merge", true}}));
    CHECK(editor.tileCount() == 3);
    CHECK(editor.composite().pixelColor(40, 2) == red);
    CHECK(editor.composite().pixelColor(20, 2) == blue);

    // Into the tileset the sprite has: nothing new to add, a layer that shows its tiles.
    REQUIRE(editor.importTileset({{"tileset", 0}, {"layout", true}, {"merge", true}}));
    CHECK(editor.tilesetList().size() == 1);
    CHECK(editor.tileCount() == 3);
    // Without merging they are appended after its tiles, in the same tileset.
    REQUIRE(editor.importTileset({{"tileset", 0}, {"tileWidth", 4}, {"tileHeight", 4}, {"layout", false}}));
    CHECK(editor.tilesetList().size() == 1);
    CHECK(editor.tileSize() == QSize(16, 16)); // the tileset's own size rules
    CHECK(editor.tileCount() == 6);
    CHECK(editor.currentTile() == 3); // the first appended one
    editor.undo();
    CHECK(editor.tileCount() == 3);
}

TEST_CASE("tilemaps: exporting a tileset writes the PNG, the .tsx, the .tmx and CSV")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 8}});
    editor.setGrid(0, 0, 8, 8);
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({10, 2}, false, {});
    editor.endStroke();
    REQUIRE(editor.convertToTilemap({{"name", "Ground"}, {"tileWidth", 8}, {"tileHeight", 8}}));

    const auto info = editor.tilesetExportInfo(2, 0, 0);
    CHECK(info["tiles"].toInt() == 2);
    CHECK(info["width"].toInt() == 16);
    CHECK(info["layers"].toInt() == 1);
    CHECK(editor.tilesetExportImage(2, 0, 0).pixelColor(2, 2) == QColor(255, 0, 0));

    const QString base = dir.filePath("level.png");
    REQUIRE(editor.exportTileset(QUrl::fromLocalFile(base), {{"columns", 2}, {"tsx", true}, {"tmx", true}, {"csv", true}}));
    QImage png(dir.filePath("level.png"));
    CHECK(png.size() == QSize(16, 8));
    CHECK(png.pixelColor(10, 2) == QColor(0, 0, 255));
    QString layerName = editor.layers()[editor.currentLayer()].toMap()["name"].toString();
    const QString csvName = "level_" + layerName.replace(' ', '_') + ".csv";
    for (const QString& name : {QString("level.tsx"), QString("level.tmx"), csvName})
        CHECK(QFileInfo::exists(dir.filePath(name)));
    QFile csv(dir.filePath(csvName));
    REQUIRE(csv.open(QIODevice::ReadOnly));
    CHECK(csv.readAll().trimmed() == "1,2");
}

TEST_CASE("tilemaps: exporting a tileset can write Tiled's JSON (.tsj and .tmj) too")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 8}});
    editor.setGrid(0, 0, 8, 8);
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({10, 2}, false, {});
    editor.endStroke();
    REQUIRE(editor.convertToTilemap({{"name", "Ground \"A\""}, {"tileWidth", 8}, {"tileHeight", 8}}));

    // Only the map is asked for: its tileset file comes with it. No XML files unless asked.
    REQUIRE(editor.exportTileset(QUrl::fromLocalFile(dir.filePath("level.png")),
                                 {{"columns", 2}, {"tsx", false}, {"tmj", true}}));
    CHECK(QFileInfo::exists(dir.filePath("level.tsj")));
    CHECK(QFileInfo::exists(dir.filePath("level.tmj")));
    CHECK_FALSE(QFileInfo::exists(dir.filePath("level.tsx")));
    CHECK_FALSE(QFileInfo::exists(dir.filePath("level.tmx")));

    const auto read = [&](const QString& name) {
        QFile file(dir.filePath(name));
        REQUIRE(file.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        REQUIRE(error.error == QJsonParseError::NoError);
        return doc.object();
    };
    const QJsonObject set = read("level.tsj");
    CHECK(set["type"].toString() == "tileset");
    CHECK(set["name"].toString() == "Ground \"A\""); // quotes survive
    CHECK(set["image"].toString() == "level.png");
    CHECK(set["tilewidth"].toInt() == 8);
    CHECK(set["tilecount"].toInt() == 2);
    CHECK(set["columns"].toInt() == 2);
    CHECK(set["imagewidth"].toInt() == 16);

    const QJsonObject map = read("level.tmj");
    CHECK(map["type"].toString() == "map");
    CHECK(map["width"].toInt() == 2);
    CHECK(map["height"].toInt() == 1);
    CHECK(map["tilesets"].toArray()[0].toObject()["source"].toString() == "level.tsj");
    CHECK(map["tilesets"].toArray()[0].toObject()["firstgid"].toInt() == 1);
    const QJsonObject layer = map["layers"].toArray()[0].toObject();
    CHECK(layer["type"].toString() == "tilelayer");
    CHECK(layer["visible"].toBool());
    REQUIRE(layer["data"].toArray().size() == 2);
    CHECK(layer["data"].toArray()[0].toInt() == 1);
    CHECK(layer["data"].toArray()[1].toInt() == 2);
}

TEST_CASE("tilemaps: a map holds the layers of several tilesets, each with its own files")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 8}});
    editor.setGrid(0, 0, 8, 8);
    editor.setTool(Editor::Pencil);
    const auto dot = [&](QColor color, QPoint at) {
        editor.setPrimaryColor(color);
        editor.beginStroke(at, false, {});
        editor.endStroke();
    };
    // Ground: two tiles (red, blue). Props: one tile (green). Small: another tile size.
    dot(QColor(255, 0, 0), {2, 2});
    dot(QColor(0, 0, 255), {10, 2});
    REQUIRE(editor.convertToTilemap({{"name", "Ground"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    const int ground = editor.currentLayer();
    editor.addLayer();
    dot(QColor(0, 255, 0), {2, 2});
    REQUIRE(editor.convertToTilemap({{"name", "Props"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    editor.addLayer();
    dot(QColor(255, 255, 0), {2, 2});
    REQUIRE(editor.convertToTilemap({{"name", "Small"}, {"tileWidth", 4}, {"tileHeight", 4}}));
    editor.setCurrentLayer(ground);

    const auto info = editor.tilesetExportInfo(2, 0, 0);
    CHECK(info["layers"].toInt() == 1);       // this tileset's own
    CHECK(info["mapLayers"].toInt() == 2);    // Ground and Props
    CHECK(info["mapTilesets"].toInt() == 2);
    CHECK(info["skippedLayers"].toInt() == 1); // Small has another tile size

    REQUIRE(editor.exportTileset(QUrl::fromLocalFile(dir.filePath("level.png")),
                                 {{"columns", 2}, {"tsx", true}, {"tmx", true}, {"tmj", true}}));
    // This tileset keeps the chosen name; the other one is named after itself.
    for (const QString& name : {"level.png", "level.tsx", "level.tsj", "level.tmx", "level.tmj", "Props.png", "Props.tsx", "Props.tsj"})
        CHECK_MESSAGE(QFileInfo::exists(dir.filePath(name)), name.toStdString());
    CHECK_FALSE(QFileInfo::exists(dir.filePath("Small.png")));

    QFile file(dir.filePath("level.tmj"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonObject map = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray sets = map["tilesets"].toArray();
    REQUIRE(sets.size() == 2);
    CHECK(sets[0].toObject()["firstgid"].toInt() == 1);
    CHECK(sets[0].toObject()["source"].toString() == "level.tsj");
    CHECK(sets[1].toObject()["firstgid"].toInt() == 3); // after Ground's two tiles
    CHECK(sets[1].toObject()["source"].toString() == "Props.tsj");
    const QJsonArray layers = map["layers"].toArray();
    REQUIRE(layers.size() == 2);
    // Ground's cells are 1 and 2; Props' one tile counts from 3.
    CHECK(layers[0].toObject()["data"].toArray()[0].toInt() == 1);
    CHECK(layers[0].toObject()["data"].toArray()[1].toInt() == 2);
    CHECK(layers[1].toObject()["data"].toArray()[0].toInt() == 3);
    CHECK(layers[1].toObject()["data"].toArray()[1].toInt() == 0);

    QFile xml(dir.filePath("level.tmx"));
    REQUIRE(xml.open(QIODevice::ReadOnly));
    const QByteArray text = xml.readAll();
    CHECK(text.contains("<tileset firstgid=\"3\" source=\"Props.tsx\"/>"));

    // A Godot TileSet: one source per tileset, the PNGs where the project keeps them.
    REQUIRE(editor.exportTileset(QUrl::fromLocalFile(dir.filePath("godot.png")),
                                 {{"columns", 2}, {"tsx", false}, {"tres", true}, {"godotFolder", "res://art/"}}));
    QFile tres(dir.filePath("godot.tres"));
    REQUIRE(tres.open(QIODevice::ReadOnly));
    const QByteArray godot = tres.readAll();
    CHECK(godot.startsWith("[gd_resource type=\"TileSet\""));
    CHECK(godot.contains("path=\"res://art/godot.png\""));
    CHECK(godot.contains("path=\"res://art/Props.png\""));
    CHECK(godot.contains("sources/1 = SubResource("));
    CHECK(QFileInfo::exists(dir.filePath("godot.png")));
    CHECK(QFileInfo::exists(dir.filePath("Props.png")));
    CHECK_FALSE(QFileInfo::exists(dir.filePath("godot.tsx"))); // not asked for
}

TEST_CASE("tilemaps: rectangle, line and fill place tiles, the eyedropper picks a stamp")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setGrid(0, 0, 8, 8);
    REQUIRE(editor.addTilemapLayer({{"name", "T"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    editor.addTile();
    editor.addTile();
    editor.addTile();
    REQUIRE(editor.tileCount() == 4);
    editor.setEditTiles(true);
    REQUIRE(editor.tilesActive());
    const auto drag = [&](QPoint from, QPoint to) {
        editor.beginStroke(from, false, {});
        editor.moveStroke(to);
        editor.endStroke();
    };

    // A solid rectangle of cells (0..2, 0..1) with tile 1.
    editor.setCurrentTile(1);
    editor.setTool(Editor::Rectangle);
    editor.setFillShapes(true);
    drag({1, 1}, {17, 9});
    CHECK(editor.tileUses(1) == 6);
    editor.setFillShapes(false);
    editor.undo();
    CHECK(editor.tileUses(1) == 0);
    drag({1, 1}, {17, 17}); // an outline of 3 x 3 cells: 8
    CHECK(editor.tileUses(1) == 8);
    editor.undo();
    editor.setFillShapes(true);
    drag({1, 1}, {17, 9});
    CHECK(editor.tileUses(1) == 6);

    // A line along the bottom row.
    editor.setCurrentTile(2);
    editor.setTool(Editor::Line);
    drag({1, 25}, {25, 25});
    CHECK(editor.tileUses(2) == 4);

    // Fill: the empty cells joined to the click.
    editor.setCurrentTile(3);
    editor.setTool(Editor::Fill);
    editor.beginStroke({30, 10}, false, {});
    editor.endStroke();
    CHECK(editor.tileUses(3) == 6);
    editor.undo();
    CHECK(editor.tileUses(3) == 0);

    // Dragging the eyedropper over two cells picks them; the pencil places the pair.
    editor.setTool(Editor::Eyedropper);
    editor.beginStroke({1, 1}, false, {});
    editor.moveStroke({9, 1});
    CHECK(editor.tilePickRect() == QRect(0, 0, 16, 8));
    editor.endStroke();
    CHECK(editor.tileStamp() == QSize(2, 1));
    CHECK(editor.tilePickRect().isEmpty());
    CHECK_FALSE(editor.tileCursorImage().isNull());
    editor.setTool(Editor::Pencil);
    editor.beginStroke({17, 25}, false, {}); // cell (2, 3)
    editor.endStroke();
    CHECK(editor.tileUses(1) == 8); // cells (2,3) and (3,3) were the line
    CHECK(editor.tileUses(2) == 2);
    editor.setCurrentTile(1); // picking a tile in the panel drops the stamp
    CHECK(editor.tileStamp() == QSize(0, 0));
}

TEST_CASE("tilemaps: ellipses, fills inside a selection, pixel perfect strokes, flipped and block stamps")
{
    Editor editor;
    editor.newSprite({{"width", 40}, {"height", 40}}); // 5 x 5 cells of 8
    editor.setGrid(0, 0, 8, 8);
    REQUIRE(editor.addTilemapLayer({{"name", "T"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    for (int i = 0; i < 5; ++i)
        editor.addTile();
    editor.setEditTiles(true);
    REQUIRE(editor.tilesActive());
    const auto drag = [&](QPoint from, QPoint to) {
        editor.beginStroke(from, false, {});
        editor.moveStroke(to);
        editor.endStroke();
    };

    // An ellipse of cells: its outline (the cells on the edge of the circle) or solid.
    int outline = 0, solid = 0;
    {
        std::set<std::pair<int, int>> cells;
        core::ellipseOutline(core::Rect{0, 0, 5, 5}, [&](core::Point p) { cells.insert({p.x, p.y}); });
        outline = int(cells.size());
        for (const auto& span : core::ellipseSpans(core::Rect{0, 0, 5, 5}))
            solid += span.second - span.first + 1;
    }
    REQUIRE(outline < solid);
    editor.setCurrentTile(1);
    editor.setTool(Editor::Ellipse);
    editor.setFillShapes(false);
    drag({1, 1}, {39, 39});
    CHECK(editor.tileUses(1) == outline);
    editor.undo();
    editor.setFillShapes(true);
    drag({1, 1}, {39, 39});
    CHECK(editor.tileUses(1) == solid);
    editor.undo();
    CHECK(editor.tileUses(1) == 0);

    // A fill with a selection stays inside the cells it touches: the 2 x 2 block at the top left.
    editor.setTool(Editor::Fill);
    editor.setCurrentTile(2);
    editor.setTool(Editor::Select);
    drag({0, 0}, {15, 15});
    REQUIRE(editor.selection() == QRect(0, 0, 16, 16));
    editor.setTool(Editor::Fill);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    CHECK(editor.tileUses(2) == 4);
    editor.undo();
    editor.deselect();
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    CHECK(editor.tileUses(2) == 25); // no selection: the whole map
    editor.undo();

    // Pixel perfect: a drag that steps right then down leaves a corner cell, which goes.
    editor.setTool(Editor::Pencil);
    editor.setCurrentTile(3);
    editor.setPixelPerfect(false);
    editor.beginStroke({1, 1}, false, {});
    editor.moveStroke({9, 1});   // cell (1, 0)
    editor.moveStroke({9, 9});   // cell (1, 1)
    editor.endStroke();
    CHECK(editor.tileUses(3) == 3);
    editor.undo();
    editor.setPixelPerfect(true);
    editor.beginStroke({1, 1}, false, {});
    editor.moveStroke({9, 1});
    editor.moveStroke({9, 9});
    editor.endStroke();
    CHECK(editor.tileUses(3) == 2); // (0,0) and (1,1): the L's corner went
    editor.undo();

    // A block of tiles from the panel is a stamp; the flip buttons turn it with its tiles.
    editor.setPixelPerfect(false);
    editor.setTileStamp(2, {1, 2, 3, 4});
    CHECK(editor.tileStamp() == QSize(2, 2));
    CHECK(editor.tileStampIds() == QVariantList{1, 2, 3, 4}); // the panel marks these
    editor.setTileFlips(4); // flip horizontally: columns swap, each tile gets the x flag
    editor.setTool(Editor::Pencil);
    editor.beginStroke({1, 1}, false, {});
    editor.endStroke();
    CHECK(editor.tileUses(1) == 1);
    CHECK(editor.tileUses(2) == 1);
    CHECK(editor.tileUses(3) == 1);
    CHECK(editor.tileUses(4) == 1);
    const auto cellValue = [&](int cx, int cy) {
        // The flipped tile's flag: read it back by picking the cell with the eyedropper.
        editor.setTool(Editor::Eyedropper);
        editor.setTileFlips(0);
        editor.beginStroke({cx * 8 + 1, cy * 8 + 1}, false, {});
        editor.endStroke();
        return std::pair<int, int>{editor.currentTile(), editor.tileFlips()};
    };
    CHECK(cellValue(0, 0) == std::pair<int, int>{2, 4}); // tile 2 is first now, flipped in x
    CHECK(cellValue(1, 0) == std::pair<int, int>{1, 4});
    CHECK(cellValue(0, 1) == std::pair<int, int>{4, 4});
    CHECK(cellValue(1, 1) == std::pair<int, int>{3, 4});
    editor.undo();

    // One tile picked this way is just the current tile.
    editor.setTileStamp(1, {3});
    CHECK(editor.tileStamp() == QSize(0, 0));
    CHECK(editor.currentTile() == 3);
    CHECK(editor.tileStampIds().isEmpty());
}

TEST_CASE("tilemaps: in tile mode a selection is whole cells and the arrows step by cells")
{
    Editor editor;
    editor.newSprite({{"width", 40}, {"height", 40}}); // 5 x 5 cells of 8
    editor.setGrid(0, 0, 8, 8);
    REQUIRE(editor.addTilemapLayer({{"name", "T"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    editor.addTile();
    const auto drag = [&](QPoint from, QPoint to) {
        editor.beginStroke(from, false, {});
        editor.moveStroke(to);
        editor.endStroke();
    };
    editor.setTool(Editor::Select);

    // Pixel mode: the rectangle as dragged.
    editor.setEditTiles(false);
    drag({3, 3}, {10, 10});
    CHECK(editor.selection() == QRect(3, 3, 8, 8));
    editor.deselect();

    // Tile mode: it grows to the cells it touches (cells 0..1 across and down).
    editor.setEditTiles(true);
    REQUIRE(editor.tilesActive());
    drag({3, 3}, {10, 10});
    CHECK(editor.selection() == QRect(0, 0, 16, 16));
    // A small drag inside one cell selects that cell.
    editor.deselect();
    drag({17, 25}, {18, 26});
    CHECK(editor.selection() == QRect(16, 24, 8, 8));

    // The arrows move it a cell at a time; Shift (ten) is the caller's step.
    editor.deselect();
    drag({1, 1}, {9, 9});
    CHECK(editor.selection() == QRect(0, 0, 16, 16));
    CHECK(editor.nudge(1, 0, false));
    CHECK(editor.selection() == QRect(8, 0, 16, 16));
    CHECK(editor.nudge(0, 1, true)); // with Alt: the same in tile mode
    CHECK(editor.selection() == QRect(8, 8, 16, 16));
    CHECK(editor.nudge(-1, -1, false));
    CHECK(editor.selection() == QRect(0, 0, 16, 16));
    // No selection: nothing to move (the arrows then change frame / layer).
    editor.deselect();
    CHECK_FALSE(editor.nudge(1, 0, false));
    // Back in pixel mode, the arrows are one pixel again.
    editor.setEditTiles(false);
    editor.setTool(Editor::Select);
    drag({1, 1}, {9, 9});
    CHECK(editor.nudge(1, 0, true));
    CHECK(editor.selection() == QRect(2, 1, 9, 9));
}

TEST_CASE("tilemaps: selected cells move, copy, stop at the map's edge and follow the arrows")
{
    Editor editor;
    editor.newSprite({{"width", 40}, {"height", 40}}); // 5 x 5 cells of 8
    editor.setGrid(0, 0, 8, 8);
    REQUIRE(editor.addTilemapLayer({{"name", "T"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    for (int i = 0; i < 4; ++i)
        editor.addTile();
    editor.setEditTiles(true);
    REQUIRE(editor.tilesActive());
    const auto click = [&](QPoint p, Qt::KeyboardModifiers mods = {}) {
        editor.beginStroke(p, false, mods);
        editor.endStroke();
    };
    const auto drag = [&](QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
        editor.beginStroke(from, false, mods);
        editor.moveStroke(to);
        editor.endStroke();
    };
    const auto place = [&](int tile, int cx, int cy) {
        editor.setTool(Editor::Pencil);
        editor.setCurrentTile(tile);
        click({cx * 8 + 1, cy * 8 + 1});
    };
    // Which tile a cell shows (picked with the eyedropper, so it also tells empty cells: 0).
    const auto tileAt = [&](int cx, int cy) {
        const Editor::Tool before = editor.tool();
        const int current = editor.currentTile();
        editor.setTool(Editor::Eyedropper);
        click({cx * 8 + 1, cy * 8 + 1});
        const int tile = editor.currentTile();
        editor.setTool(before);
        editor.setCurrentTile(current);
        return tile;
    };

    // Tiles 1, 2, 3 in the top left 2 x 2 block (the fourth cell empty), tile 4 where it will land.
    place(1, 0, 0);
    place(2, 1, 0);
    place(3, 0, 1);
    place(4, 3, 1);
    editor.setTool(Editor::Select);
    drag({1, 1}, {9, 9});
    REQUIRE(editor.selection() == QRect(0, 0, 16, 16));

    // The Move tool drags them two cells right; the block's empty cell leaves tile 4 alone.
    editor.setTool(Editor::Move);
    drag({4, 4}, {20, 5}); // 16 px right (and a pixel down: not a whole cell)
    CHECK(tileAt(0, 0) == 0);
    CHECK(tileAt(1, 0) == 0);
    CHECK(tileAt(0, 1) == 0);
    CHECK(tileAt(2, 0) == 1);
    CHECK(tileAt(3, 0) == 2);
    CHECK(tileAt(2, 1) == 3);
    CHECK(tileAt(3, 1) == 4);
    CHECK(editor.selection() == QRect(16, 0, 16, 16)); // the selection went along
    editor.undo(); // one step: the cells and the selection
    CHECK(tileAt(0, 0) == 1);
    CHECK(tileAt(1, 0) == 2);
    CHECK(tileAt(2, 0) == 0);
    CHECK(editor.selection() == QRect(0, 0, 16, 16));

    // Alt copies: the originals stay.
    drag({4, 4}, {20, 4}, Qt::AltModifier);
    CHECK(tileAt(0, 0) == 1);
    CHECK(tileAt(2, 0) == 1);
    CHECK(tileAt(3, 0) == 2);
    CHECK(editor.tileUses(1) == 2);
    editor.undo();
    CHECK(editor.tileUses(1) == 1);
    CHECK(editor.selection() == QRect(0, 0, 16, 16));

    // Dragging far stops at the map's edge: the block ends in the last two columns.
    drag({4, 4}, {200, 4});
    CHECK(tileAt(3, 0) == 1);
    CHECK(tileAt(4, 0) == 2);
    CHECK(editor.selection() == QRect(24, 0, 16, 16));
    editor.undo();

    // The Select tool drags them too (inside the selection); a click there deselects.
    editor.setTool(Editor::Select);
    drag({4, 4}, {4, 20});
    CHECK(tileAt(0, 2) == 1);
    CHECK(tileAt(0, 0) == 0);
    editor.undo();
    click({4, 4});
    CHECK(editor.selection().isEmpty());
    editor.undo(); // the selection comes back; no tile changed
    REQUIRE(editor.selection() == QRect(0, 0, 16, 16));
    CHECK(tileAt(0, 0) == 1);

    // The arrows move the cells a cell at a time (Alt: only the selection's outline).
    CHECK(editor.nudge(1, 0, false));
    CHECK(tileAt(1, 0) == 1);
    CHECK(tileAt(2, 0) == 2);
    CHECK(tileAt(0, 0) == 0);
    CHECK(editor.selection() == QRect(8, 0, 16, 16));
    editor.undo();
    CHECK(tileAt(0, 0) == 1);
    CHECK(editor.selection() == QRect(0, 0, 16, 16));
    CHECK(editor.nudge(1, 0, true)); // outline only
    CHECK(tileAt(0, 0) == 1);
    CHECK(editor.selection() == QRect(8, 0, 16, 16));
}

TEST_CASE("gradient: more shapes, and a run through the palette's range")
{
    const QColor red(255, 0, 0), green(0, 255, 0), blue(0, 0, 255);
    Editor editor;
    editor.newSprite({{"width", 33}, {"height", 33}});
    editor.setTool(Editor::Gradient);
    editor.setPrimaryColor(red);
    editor.setSecondaryColor(blue);
    const auto drag = [&](QPoint from, QPoint to) {
        editor.beginStroke(from, false, {});
        editor.moveStroke(to, {});
        CHECK(editor.gradientLine().size() == 2); // the line being dragged, for the overlay
        editor.endStroke();
        CHECK(editor.gradientEditable()); // the line stays, open to change
        editor.finishGradient();
        CHECK(editor.gradientLine().isEmpty());
    };

    // Reflected: the same on both sides of the start; diamond: the same four ways round it.
    editor.setGradientShape(2);
    drag({16, 16}, {32, 16});
    CHECK(editor.composite().pixelColor(16, 16) == red);
    CHECK(editor.composite().pixelColor(8, 3) == editor.composite().pixelColor(24, 29));
    editor.undo();
    editor.setGradientShape(3);
    drag({16, 16}, {32, 16});
    CHECK(editor.composite().pixelColor(16, 16) == red);
    CHECK(editor.composite().pixelColor(24, 16) == editor.composite().pixelColor(16, 24));
    CHECK(editor.composite().pixelColor(24, 16) == editor.composite().pixelColor(8, 16));
    editor.undo();
    // Angle: red where it starts (to the right of the start), going round and back towards blue.
    editor.setGradientShape(4);
    drag({16, 16}, {32, 16});
    CHECK(editor.composite().pixelColor(30, 16) == red);
    CHECK(editor.composite().pixelColor(16, 30) != editor.composite().pixelColor(2, 16));
    editor.undo();

    // Through the palette: three entries selected as the range, in this order.
    editor.setPaletteColors(QVariantList{red, green, blue, QColor(255, 255, 0)});
    editor.setShadeRange(QPoint(0, 2));
    editor.setGradientShape(0);
    editor.setGradientRamp(true);
    drag({0, 16}, {32, 16});
    CHECK(editor.composite().pixelColor(0, 16) == red);
    const QColor middle = editor.composite().pixelColor(16, 16); // the middle of three: green, to a step
    CHECK(middle.green() > 245);
    CHECK(middle.red() < 8);
    CHECK(middle.blue() < 8);
    CHECK(editor.composite().pixelColor(32, 16) == blue);
    const QColor quarter = editor.composite().pixelColor(8, 16); // between red and green
    CHECK(quarter.red() > 100);
    CHECK(quarter.green() > 100);
    CHECK(quarter.blue() == 0);
    editor.undo();

    // Dithered, only the range's colors appear, the yellow outside it never does.
    editor.setGradientDither(2);
    drag({0, 16}, {32, 16});
    int others = 0;
    for (int y = 0; y < 33; ++y)
        for (int x = 0; x < 33; ++x) {
            const QColor c = editor.composite().pixelColor(x, y);
            others += c != red && c != green && c != blue;
        }
    CHECK(others == 0);
    editor.undo();

    // Without a range, the whole palette (four colors); without enough colors, primary to secondary.
    editor.setShadeRange(QPoint(-1, -1));
    editor.setGradientDither(0);
    drag({0, 16}, {32, 16});
    CHECK(editor.composite().pixelColor(32, 16) == QColor(255, 255, 0));
}

TEST_CASE("gradient: it stays editable after the drag, repeats, and takes stops of its own")
{
    const QColor red(255, 0, 0), green(0, 255, 0), blue(0, 0, 255);
    Editor editor;
    editor.newSprite({{"width", 17}, {"height", 4}});
    editor.setTool(Editor::Gradient);
    editor.setPrimaryColor(red);
    editor.setSecondaryColor(blue);
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({8, 0}, {});
    editor.endStroke();
    REQUIRE(editor.gradientEditable());
    const QColor first = editor.composite().pixelColor(4, 0);
    CHECK(editor.composite().pixelColor(16, 0) == blue); // flat beyond the end

    // The end moves: still one undo step.
    editor.editGradient(0, 0, 16, 0);
    CHECK(editor.gradientEditable());
    CHECK(editor.composite().pixelColor(4, 0) != first);
    CHECK(editor.composite().pixelColor(16, 0) == blue);
    editor.undo();
    CHECK_FALSE(editor.gradientEditable());
    CHECK(editor.composite().pixelColor(4, 0).alpha() == 0); // nothing left of it
    editor.redo();

    // Options and colors redraw it.
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({8, 0}, {});
    editor.endStroke();
    editor.setGradientRepeat(1);
    CHECK(editor.composite().pixelColor(8, 0) == red); // starts over at the end
    editor.setGradientRepeat(2);
    CHECK(editor.composite().pixelColor(8, 0) == blue);
    CHECK(editor.composite().pixelColor(16, 0) == red); // and runs back
    editor.setSecondaryColor(green);
    CHECK(editor.composite().pixelColor(8, 0) == green);
    editor.setGradientRepeat(0);

    // Stops of your own, anywhere.
    editor.setGradientCustom(true);
    QVariantList stops;
    stops.append(QVariantMap{{"position", 0.0}, {"color", red}});
    stops.append(QVariantMap{{"position", 0.5}, {"color", green}});
    stops.append(QVariantMap{{"position", 1.0}, {"color", blue}});
    editor.setGradientCustomStops(stops);
    CHECK(editor.composite().pixelColor(4, 0).green() > 240); // the middle stop
    CHECK(editor.composite().pixelColor(8, 0) == blue);
    CHECK(Editor().gradientStopsText().isEmpty());
    Editor other;
    other.setGradientStopsText(editor.gradientStopsText());
    CHECK(other.gradientCustomStops().size() == 3);

    // Esc takes it back; another edit, or another tool, closes it.
    editor.cancelGradient();
    CHECK_FALSE(editor.gradientEditable());
    CHECK(editor.composite().pixelColor(4, 0) != green);
    editor.beginStroke({0, 0}, false, {});
    editor.moveStroke({8, 0}, {});
    editor.endStroke();
    CHECK(editor.gradientEditable());
    editor.setTool(Editor::Pencil);
    CHECK_FALSE(editor.gradientEditable());
    CHECK(editor.gradientLine().isEmpty());
}

TEST_CASE("tilemaps: the tile panel follows the selected layer")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    editor.setGrid(0, 0, 8, 8);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setTool(Editor::Pencil);
    editor.beginStroke({4, 4}, false, {});
    editor.endStroke();
    REQUIRE(editor.convertToTilemap({{"name", "Ground"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    const int paintLayer = editor.currentLayer();
    REQUIRE(editor.addTilemapLayer({{"tileset", 0}}));
    const int second = editor.currentLayer();
    CHECK(editor.currentTilesetIndex() == 0);
    CHECK(editor.tilesetName() == "Ground");
    CHECK(editor.editTiles());

    // A normal layer in between: the panel's state follows.
    editor.addLayer();
    QSignalSpy changed(&editor, &Editor::tilesetChanged);
    CHECK_FALSE(editor.tilemapLayer());
    editor.setCurrentLayer(second);
    CHECK(changed.count() >= 1);
    CHECK(editor.tilemapLayer());
    CHECK(editor.tilesetName() == "Ground");
    CHECK(editor.tileCount() > 1);
    editor.setCurrentLayer(paintLayer);
    CHECK(editor.currentTilesetIndex() == 0);
}

TEST_CASE("tilemaps: a tile edited in its own tab goes back to the tileset")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.setGrid(0, 0, 8, 8);
    editor.setTool(Editor::Pencil);
    editor.setPrimaryColor(QColor(255, 0, 0));
    editor.setBrushSize(1);
    editor.beginStroke({2, 2}, false, {});
    editor.endStroke();
    REQUIRE(editor.convertToTilemap({{"name", "T"}, {"tileWidth", 8}, {"tileHeight", 8}}));
    const int tileMode = editor.tileMode();
    REQUIRE(editor.tileCount() == 2);
    CHECK(editor.tileImage(1).pixelColor(2, 2) == QColor(255, 0, 0));

    // QML reads the editor when the tiled view changes: a document must be in place.
    int reads = 0;
    QObject::connect(&editor, &Editor::tileModeChanged, [&] {
        reads += editor.canvasWidth() > 0 && editor.layers().size() > 0;
    });
    REQUIRE(editor.editTileInTab(1));
    CHECK(editor.documents().size() == 2);
    CHECK(editor.currentDocument() == 1);
    CHECK(editor.tileMode() == 3); // repeating on both axes
    CHECK(editor.canvasWidth() == 8);
    CHECK(editor.documents()[1].toMap()["name"].toString().startsWith("Tile 1"));

    editor.setPrimaryColor(QColor(0, 0, 255));
    editor.beginStroke({5, 5}, false, {});
    editor.endStroke();
    CHECK_FALSE(editor.documents()[1].toMap()["modified"].toBool()); // never asks to be saved
    editor.setCurrentDocument(0);
    CHECK(editor.tileMode() == tileMode); // the other tab's view is as it was
    CHECK(editor.tileImage(1).pixelColor(5, 5) == QColor(0, 0, 255));
    CHECK(editor.tileImage(1).pixelColor(2, 2) == QColor(255, 0, 0));
    CHECK(editor.documents()[0].toMap()["modified"].toBool());

    editor.undo(); // one step in the source
    CHECK(editor.tileImage(1).pixelColor(5, 5).alpha() == 0);
    editor.redo();

    editor.setCurrentDocument(1);
    CHECK(editor.tileMode() == 3);
    editor.beginStroke({6, 6}, false, {});
    editor.endStroke();
    editor.setCurrentDocument(0);
    CHECK(editor.tileImage(1).pixelColor(6, 6) == QColor(0, 0, 255));

    editor.setCurrentDocument(1);
    editor.closeDocument(1);
    CHECK(editor.documents().size() == 1);
    CHECK(editor.tileMode() == tileMode);
    CHECK(reads >= 4); // every change happened with a document there
}

TEST_CASE("symmetry: radial copies turn a custom brush with them when asked")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    QImage bar(3, 1, QImage::Format_RGBA8888);
    bar.fill(QColor(255, 0, 0));
    REQUIRE(editor.setCustomBrush(bar));
    editor.setTool(Editor::Pencil);
    editor.setSymmetryRadial(4);

    // The bar at (11, 8) is carried to (7, 11); kept as it is, it stays horizontal there.
    drag(editor, {{11, 8}});
    CHECK(painted(editor, 11, 8));
    CHECK(painted(editor, 6, 11));
    CHECK(painted(editor, 8, 11));
    CHECK_FALSE(painted(editor, 7, 10));
    editor.undo();

    editor.setSymmetryRadialTurn(true); // turned a quarter with the copy: vertical there
    drag(editor, {{11, 8}});
    CHECK(painted(editor, 10, 8));
    CHECK(painted(editor, 7, 10));
    CHECK(painted(editor, 7, 12));
    CHECK_FALSE(painted(editor, 6, 11));
    CHECK(paintedCount(editor) == 12);
}

TEST_CASE("symmetry: the wand and selections follow the axes and the radial copies")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.setPrimaryColor(kRed);
    editor.setTool(Editor::Pencil);
    drag(editor, {{1, 1}});
    drag(editor, {{6, 1}});
    editor.setSymmetryHorizontal(true);
    editor.setTool(Editor::MagicWand);
    drag(editor, {{1, 1}});
    CHECK(editor.selectionContains({1, 1}));
    CHECK(editor.selectionContains({6, 1})); // the mirror's dot
    CHECK_FALSE(editor.selectionContains({3, 1}));
    editor.deselect();
    editor.setSymmetryHorizontal(false);

    // A radial selection takes the turned copies too.
    editor.setSymmetryRadial(4);
    editor.setTool(Editor::Select);
    drag(editor, {{5, 4}, {5, 5}});
    CHECK(editor.selectionContains({5, 4}));
    CHECK(editor.selectionContains({3, 5})); // a quarter turn about (4, 4)
    CHECK(editor.selectionContains({2, 3}));
    CHECK(editor.selectionContains({4, 2}));
}

TEST_CASE("symmetry: a diagonal axis keeps the crossing point on whole pixels' middles")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 9}}); // centered at (4, 4.5): x and y differ by half a pixel
    editor.setSymmetryRising(true);
    const QPointF c = editor.symmetryCenter();
    CHECK((int(c.x() * 2) - int(c.y() * 2)) % 2 == 0);
    // Moving one coordinate by a half pixel gives way in the other direction, not by drifting the other.
    editor.setSymmetryCenter({c.x() + 0.5, c.y()});
    const QPointF d = editor.symmetryCenter();
    CHECK(d.y() == c.y());
    CHECK((int(d.x() * 2) - int(d.y() * 2)) % 2 == 0);
}

TEST_CASE("symmetry: pixel perfect keeps the pixels another part of the stroke paints")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.setPrimaryColor(kRed);
    editor.setTool(Editor::Pencil);
    editor.setSymmetryHorizontal(true); // axis between x = 3 and x = 4
    REQUIRE(editor.pixelPerfect());
    // (3,3) is a corner and goes, but (4,3) is its mirror image and the path's own start.
    drag(editor, {{4, 3}, {3, 3}, {3, 2}});
    CHECK(painted(editor, 4, 3));
    CHECK(painted(editor, 3, 3));
    CHECK(painted(editor, 3, 2));
    CHECK(painted(editor, 4, 2));
}

TEST_CASE("symmetry: moving a mirrored selection moves the pieces as mirror images")
{
    Editor editor;
    editor.newSprite({{"width", 16}, {"height", 16}});
    editor.setPrimaryColor(kRed);
    editor.setTool(Editor::Pencil);
    drag(editor, {{1, 1}});
    drag(editor, {{14, 1}});
    editor.setSymmetryHorizontal(true);
    editor.setTool(Editor::Select);
    drag(editor, {{1, 1}, {2, 2}}); // takes the mirrored square too
    REQUIRE(editor.selectionContains({14, 1}));

    editor.setTool(Editor::Move);
    drag(editor, {{1, 1}, {3, 1}});
    CHECK(painted(editor, 3, 1));
    CHECK(painted(editor, 12, 1)); // the mirror image moved the other way
    CHECK_FALSE(painted(editor, 1, 1));
    CHECK_FALSE(painted(editor, 14, 1));
    CHECK(editor.selectionContains({12, 1}));
    editor.undo();
    CHECK(painted(editor, 1, 1));
    CHECK(painted(editor, 14, 1));
}

TEST_CASE("tools: the size being dragged out, for the readout by the pointer")
{
    Fixture f;
    QSignalSpy spy(&f.editor, &Editor::dragInfoChanged);
    CHECK(f.editor.dragInfo().isEmpty());

    f.editor.setTool(Editor::Rectangle);
    f.editor.beginStroke({2, 2}, false);
    f.editor.moveStroke({5, 6});
    CHECK(f.editor.dragInfo() == QString::fromUtf8("4 × 5"));
    f.editor.setStrokeModifiers(Qt::ShiftModifier); // a square
    CHECK(f.editor.dragInfo() == QString::fromUtf8("5 × 5"));
    f.editor.endStroke();
    CHECK(f.editor.dragInfo().isEmpty());
    CHECK(spy.count() >= 3);

    f.editor.setTool(Editor::Line);
    f.editor.beginStroke({1, 1}, false);
    f.editor.moveStroke({9, 3});
    CHECK(f.editor.dragInfo() == QString::fromUtf8("9 × 3"));
    f.editor.endStroke();

    // A selection: what it selects, cut to the canvas.
    f.editor.setTool(Editor::Select);
    f.editor.beginStroke({10, 12}, false);
    f.editor.moveStroke({30, 30});
    CHECK(f.editor.dragInfo() == QString::fromUtf8("6 × 4"));
    f.editor.endStroke();
    CHECK(f.editor.dragInfo().isEmpty());

    // The pencil has no size to show.
    f.editor.setTool(Editor::Pencil);
    f.editor.beginStroke({1, 1}, false);
    f.editor.moveStroke({4, 4});
    CHECK(f.editor.dragInfo().isEmpty());
    f.editor.endStroke();
}
