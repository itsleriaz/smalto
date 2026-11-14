#include <doctest/doctest.h>

#include "Editor.h"
#include "TimelineModel.h"

#include <QSignalSpy>
#include <QTest>

namespace {

const QString kCorpus = QStringLiteral(SMALTO_CORPUS_DIR);

QVariant cell(const TimelineModel& model, int row, int column, TimelineModel::Role role)
{
    return model.data(model.index(row, column), role);
}

int flags(const TimelineModel& model, int row, int column)
{
    return cell(model, row, column, TimelineModel::CelRole).toInt();
}

} // namespace

TEST_CASE("timeline: rows are layers top first, columns are frames")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/rgba_features.aseprite"));
    TimelineModel model;
    model.setEditor(&editor);
    REQUIRE(model.rowCount() == 6);
    REQUIRE(model.columnCount() == 4);

    CHECK(cell(model, 0, 0, TimelineModel::NameRole) == QString::fromUtf8("locked ünïcode"));
    CHECK(cell(model, 0, 0, TimelineModel::LockedRole) == true);
    CHECK(cell(model, 5, 0, TimelineModel::NameRole) == "bg");
    CHECK(model.layerForRow(5) == 0);
    CHECK(model.rowForLayer(0) == 5);

    CHECK(cell(model, 1, 0, TimelineModel::NameRole) == "group"); // above its children
    const int group = model.rowForLayer(4);
    CHECK(cell(model, group, 0, TimelineModel::KindRole) == "group");
    CHECK(flags(model, group, 0) == TimelineModel::NoCels);
    const int hidden = model.rowForLayer(3);
    CHECK(cell(model, hidden, 0, TimelineModel::DepthRole) == 1);
    CHECK(cell(model, hidden, 0, TimelineModel::ShownRole) == false);

    // bg: frames 3 and 4 share a cel; the locked layer only has frame 1.
    CHECK(flags(model, 5, 0) == TimelineModel::HasCel);
    CHECK(flags(model, 5, 2) == (TimelineModel::HasCel | TimelineModel::Linked | TimelineModel::LinkedToNext));
    CHECK(flags(model, 5, 3) == (TimelineModel::HasCel | TimelineModel::Linked | TimelineModel::LinkedToPrevious));
    CHECK(flags(model, 0, 0) == TimelineModel::HasCel);
    CHECK(flags(model, 0, 1) == 0);
}

TEST_CASE("timeline: updates in place, resets only when the grid changes shape")
{
    Editor editor; // 64x64, one layer, one frame
    TimelineModel model;
    model.setEditor(&editor);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    CHECK(flags(model, 0, 0) == 0);

    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(flags(model, 0, 0) == TimelineModel::HasCel);
    editor.setLayerVisible(0, false);
    CHECK(cell(model, 0, 0, TimelineModel::LayerVisibleRole) == false);
    CHECK(resets.count() == 0);
    CHECK(changes.count() > 0);

    editor.addFrame();
    CHECK(model.columnCount() == 2);
    editor.addLayer();
    CHECK(model.rowCount() == 2);
    CHECK(resets.count() == 2);
}

TEST_CASE("timeline: undoing the first stroke on an empty frame leaves it empty")
{
    Editor editor;
    TimelineModel model;
    model.setEditor(&editor);
    editor.beginStroke({1, 1}, false);
    editor.moveStroke({4, 4});
    editor.endStroke();
    CHECK(flags(model, 0, 0) == TimelineModel::HasCel);
    editor.undo();
    CHECK(flags(model, 0, 0) == 0);
    editor.redo();
    CHECK(flags(model, 0, 0) == TimelineModel::HasCel);
    CHECK(editor.composite().pixelColor(4, 4).alpha() > 0);

    // A stroke that changes nothing (outside the selection) leaves no cel.
    editor.addFrame();
    editor.setTool(Editor::Select);
    editor.beginStroke({10, 10}, false);
    editor.moveStroke({12, 12});
    editor.endStroke();
    editor.setTool(Editor::Pencil);
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(flags(model, 0, 1) == 0);
    CHECK(editor.canUndo());
}

TEST_CASE("timeline: lock toggles block drawing and count as edits")
{
    Editor editor;
    editor.setLayerLocked(0, true);
    CHECK(editor.modified());
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(editor.composite().pixelColor(1, 1).alpha() == 0);
    editor.setLayerLocked(0, false);
    editor.beginStroke({1, 1}, false);
    editor.endStroke();
    CHECK(editor.composite().pixelColor(1, 1).alpha() > 0);
}

namespace {

// A 4x4 document with `frames` frames; frame f has one pixel at (f, 0) on
// the bottom layer, so frames can be told apart by their cels.
struct Frames {
    Editor editor;
    explicit Frames(int frames)
    {
        editor.newDocument(8, 4);
        editor.setPrimaryColor(QColor(255, 0, 0));
        for (int f = 0; f < frames; ++f) {
            if (f > 0)
                editor.addFrame();
            editor.beginStroke({f, 0}, false);
            editor.endStroke();
        }
    }
    const core::Layer& layer(int index) const { return *editor.layerRows()[size_t(index)]; }
    // Which marker pixel frame f shows (-1: none).
    int marker(int f)
    {
        editor.setCurrentFrame(f);
        for (int x = 0; x < 8; ++x)
            if (editor.composite().pixelColor(x, 0).alpha() > 0)
                return x;
        return -1;
    }
    std::vector<int> markers()
    {
        std::vector<int> out;
        for (int f = 0; f < editor.frameCount(); ++f)
            out.push_back(marker(f));
        return out;
    }
};

} // namespace

TEST_CASE("timeline ranges: frame operations act on the selected frames")
{
    Frames t(4);
    t.editor.selectFrames(2, 1);
    CHECK(t.editor.rangeType() == Editor::FrameRange);
    CHECK(t.editor.range() == QRect(1, 0, 2, 1));

    t.editor.duplicateFrame();
    CHECK(t.markers() == std::vector{0, 1, 2, 1, 2, 3});
    CHECK(t.editor.range() == QRect(3, 0, 2, 1)); // the copies
    t.editor.undo();
    CHECK(t.markers() == std::vector{0, 1, 2, 3});

    t.editor.selectFrames(0, 2);
    t.editor.reverseFrames();
    CHECK(t.markers() == std::vector{2, 1, 0, 3});
    t.editor.undo();

    t.editor.selectFrames(1, 2);
    CHECK(t.editor.moveRange(0, 1)); // [1 2] one later
    CHECK(t.markers() == std::vector{0, 3, 1, 2});
    CHECK(t.editor.range() == QRect(2, 0, 2, 1));
    CHECK_FALSE(t.editor.moveRange(0, 5)); // already at the end
    CHECK(t.editor.range().x() == 2);
    t.editor.undo();
    CHECK(t.markers() == std::vector{0, 1, 2, 3});

    t.editor.selectFrames(1, 2);
    t.editor.removeFrame();
    CHECK(t.markers() == std::vector{0, 3});
    CHECK(t.editor.rangeType() == Editor::NoRange);
    t.editor.selectFrames(0, 1);
    t.editor.removeFrame(); // would leave nothing: refused
    CHECK(t.editor.frameCount() == 2);
    t.editor.undo();
    CHECK(t.markers() == std::vector{0, 1, 2, 3});
}

TEST_CASE("timeline ranges: duplicated frames keep links among themselves")
{
    Frames t(2);
    t.editor.selectFrames(0, 1);
    t.editor.linkCels();
    CHECK(t.layer(0).cel(0) == t.layer(0).cel(1));
    t.editor.duplicateFrame();
    REQUIRE(t.editor.frameCount() == 4);
    CHECK(t.layer(0).cel(2) == t.layer(0).cel(3)); // linked copies
    CHECK(t.layer(0).cel(2) != t.layer(0).cel(0)); // but not to the originals
}

TEST_CASE("timeline ranges: cels move and copy across layers and frames")
{
    Frames t(3);
    t.editor.addLayer(); // layer 1, empty
    const core::CelRef first = t.layer(0).cel(0);

    t.editor.selectCels(0, 0, 0, 1);
    CHECK(t.editor.inRange(0, 1));
    CHECK_FALSE(t.editor.inRange(1, 1));
    REQUIRE(t.editor.moveRange(1, 1)); // up one layer, one frame later
    CHECK(t.layer(0).cel(0) == nullptr);
    CHECK(t.layer(0).cel(1) == nullptr);
    CHECK(t.layer(1).cel(1) == first);
    CHECK(t.layer(1).cel(2) != nullptr);
    CHECK(t.layer(0).cel(2) != nullptr); // untouched
    CHECK(t.editor.range() == QRect(1, 1, 2, 1));
    t.editor.undo();
    CHECK(t.layer(0).cel(0) == first);
    CHECK(t.layer(1).cel(1) == nullptr);

    // Alt: copies, with new pixels.
    t.editor.selectCels(0, 0, 0, 0);
    REQUIRE(t.editor.moveRange(1, 0, true));
    CHECK(t.layer(0).cel(0) == first);
    REQUIRE(t.layer(1).cel(0));
    CHECK(t.layer(1).cel(0) != first);
    CHECK(*t.layer(1).cel(0)->image == *first->image);

    // Off the grid or onto a group: refused, nothing changes.
    t.editor.selectCels(0, 0, 0, 2);
    CHECK_FALSE(t.editor.moveRange(0, 1));
    CHECK_FALSE(t.editor.moveRange(2, 0));
}

TEST_CASE("timeline ranges: clear, link and unlink cels")
{
    Frames t(3);
    t.editor.selectCels(0, 0, 0, 2);
    t.editor.linkCels();
    CHECK(t.layer(0).cel(1) == t.layer(0).cel(0));
    CHECK(t.layer(0).cel(2) == t.layer(0).cel(0));
    CHECK(t.markers() == std::vector{0, 0, 0});
    t.editor.selectCels(0, 1, 0, 1);
    t.editor.unlinkCels();
    CHECK(t.layer(0).cel(1) != t.layer(0).cel(0));
    CHECK(t.layer(0).cel(2) == t.layer(0).cel(0));
    t.editor.clearCels();
    CHECK(t.layer(0).cel(1) == nullptr);
    t.editor.undo();
    t.editor.undo();
    t.editor.undo();
    CHECK(t.markers() == std::vector{0, 1, 2});
}

TEST_CASE("timeline: Delete acts on what is selected: cels clear, frames and layers go")
{
    Frames t(3);
    // Cels of the grid: cleared, the frames stay.
    t.editor.selectCels(0, 0, 0, 1);
    t.editor.deleteSelection();
    CHECK(t.layer(0).cel(0) == nullptr);
    CHECK(t.layer(0).cel(1) == nullptr);
    CHECK(t.editor.frameCount() == 3);
    t.editor.undo();
    // Frames: deleted.
    t.editor.selectFrames(0, 1);
    t.editor.deleteSelection();
    CHECK(t.editor.frameCount() == 1);
    t.editor.undo();
    CHECK(t.editor.frameCount() == 3);
    // A block of layers from the layer column: deleted. The same cels taken in the grid: cleared.
    t.editor.addLayer();
    t.editor.addLayer();
    const int layers = int(t.editor.layers().size());
    t.editor.selectCels(0, 0, 1, 0);
    t.editor.deleteSelection();
    CHECK(int(t.editor.layers().size()) == layers);
    t.editor.undo();
    t.editor.selectLayerBlock(0, 1);
    t.editor.deleteSelection();
    CHECK(int(t.editor.layers().size()) == layers - 2);
    t.editor.undo();
    CHECK(int(t.editor.layers().size()) == layers);
}

TEST_CASE("timeline: Clear Layer empties the current layer on every frame")
{
    Frames t(3);
    t.editor.clearLayer();
    CHECK(t.markers() == std::vector{-1, -1, -1});
    t.editor.undo();
    CHECK(t.markers() == std::vector{0, 1, 2});
}

TEST_CASE("timeline: tags are added, edited and removed with undo")
{
    Frames t(4);
    t.editor.selectFrames(1, 2);
    t.editor.addTag();
    REQUIRE(t.editor.tags().size() == 1);
    QVariantMap tag = t.editor.tags().first().toMap();
    CHECK(tag.value("name") == "Tag 1");
    CHECK(tag.value("from") == 1);
    CHECK(tag.value("to") == 2);

    t.editor.setTag(0, {{"name", "walk"}, {"direction", 2}, {"repeat", 3}, {"color", QColor(1, 2, 3)}});
    tag = t.editor.tags().first().toMap();
    CHECK(tag.value("name") == "walk");
    CHECK(tag.value("direction") == 2);
    CHECK(tag.value("repeat") == 3);
    CHECK(tag.value("color").value<QColor>() == QColor(1, 2, 3));

    t.editor.undo();
    CHECK(t.editor.tags().first().toMap().value("name") == "Tag 1");
    t.editor.removeTag(0);
    CHECK(t.editor.tags().isEmpty());
    t.editor.undo();
    t.editor.undo();
    CHECK(t.editor.tags().isEmpty());

    // Deleting a frame inside a tag shrinks it.
    t.editor.selectFrames(1, 2);
    t.editor.addTag();
    t.editor.clearRange();
    t.editor.setCurrentFrame(1);
    t.editor.removeFrame();
    CHECK(t.editor.tags().first().toMap().value("to") == 1);
}

TEST_CASE("onion skin: shows neighbours, follows edits, hides while playing")
{
    Frames t(3); // frame f has a marker pixel at (f, 0)
    t.editor.setCurrentFrame(1);
    auto onionAlpha = [&](int x) { return t.editor.onion().pixelColor(x, 0).alpha(); };
    CHECK(onionAlpha(0) == 0); // off by default
    CHECK_FALSE(t.editor.takeOnionDirty().isEmpty()); // initial upload

    t.editor.setOnionSkin(true);
    CHECK(onionAlpha(0) > 0); // frame 0, before
    CHECK(onionAlpha(2) > 0); // frame 2, after
    CHECK(onionAlpha(1) == 0); // not the current frame
    CHECK(t.editor.takeOnionDirty() == QRect(0, 0, 8, 4));

    t.editor.setOnionAfter(0);
    CHECK(onionAlpha(2) == 0);
    t.editor.setOnionOpacity(100);
    CHECK(onionAlpha(0) == 255);
    CHECK(t.editor.onionOpacity() == 100);

    // Moving to another frame moves the onion skin with it.
    t.editor.setOnionAfter(1);
    t.editor.setCurrentFrame(2);
    CHECK(onionAlpha(1) > 0);
    CHECK(onionAlpha(2) == 0);

    // Editing a cel linked into the previous frame shows up in the onion.
    t.editor.selectFrames(1, 2);
    t.editor.linkCels();
    t.editor.clearRange();
    t.editor.setCurrentFrame(2);
    t.editor.beginStroke({5, 2}, false);
    t.editor.endStroke();
    CHECK(t.editor.onion().pixelColor(5, 2).alpha() > 0);

    t.editor.setPlaying(true);
    CHECK(onionAlpha(1) == 0);
    t.editor.setPlaying(false);
    CHECK(onionAlpha(1) > 0);
    t.editor.setOnionSkin(false);
    CHECK(onionAlpha(1) == 0);
}

TEST_CASE("playback: the preview panel plays the tag's direction and repeats without moving the editor")
{
    Frames t(5);
    t.editor.selectFrames(1, 3);
    t.editor.addTag();
    t.editor.setTag(0, {{"direction", 2}, {"repeat", 2}});
    t.editor.clearRange();
    t.editor.setCurrentFrame(1);

    std::vector<int> seen{t.editor.startPreviewPlayback(1)};
    for (int f = t.editor.nextPreviewFrame(); f >= 0; f = t.editor.nextPreviewFrame())
        seen.push_back(f);
    CHECK(seen == std::vector{1, 2, 3, 2, 1});
    CHECK(t.editor.nextPreviewFrame() == -1);
    CHECK(t.editor.currentFrame() == 1);

    // Outside any tag: every frame, forever.
    CHECK(t.editor.startPreviewPlayback(4) == 4);
    CHECK(t.editor.nextPreviewFrame() == 0);
    CHECK(t.editor.nextPreviewFrame() == 1);
    t.editor.stopPreviewPlayback();
    CHECK(t.editor.nextPreviewFrame() == -1);

    // What the panel compares to restart in another tag.
    CHECK(t.editor.tagIndexAt(2) == 0);
    CHECK(t.editor.tagIndexAt(4) == -1);
}

TEST_CASE("playback: plays the tag under the playhead, then stops after its repeats")
{
    Frames t(5);
    for (int f = 0; f < 5; ++f) {
        t.editor.setCurrentFrame(f);
        t.editor.setFrameDuration(5);
    }
    // Tag over frames 1..3, ping-pong, two passes from 1: 2 3 (there), 2 1 (back), stop.
    t.editor.selectFrames(1, 3);
    t.editor.addTag();
    t.editor.setTag(0, {{"direction", 2}, {"repeat", 2}});
    t.editor.clearRange();
    t.editor.setCurrentFrame(1);

    std::vector<int> seen;
    QObject::connect(&t.editor, &Editor::currentFrameChanged, [&] { seen.push_back(t.editor.currentFrame()); });
    QSignalSpy playing(&t.editor, &Editor::playingChanged);
    t.editor.setPlaying(true);
    CHECK(t.editor.playingTag() == 0);
    REQUIRE(playing.wait(2000)); // stops by itself
    CHECK_FALSE(t.editor.playing());
    CHECK(t.editor.playingTag() == -1);
    CHECK(seen == std::vector{2, 3, 2, 1});

    // Outside any tag: all frames, looping, until stopped.
    t.editor.setCurrentFrame(4);
    seen.clear();
    t.editor.setPlaying(true);
    CHECK(t.editor.playingTag() == -1);
    QTRY_VERIFY_WITH_TIMEOUT(seen.size() >= 6, 2000);
    t.editor.setPlaying(false);
    CHECK(std::vector(seen.begin(), seen.begin() + 6) == std::vector{0, 1, 2, 3, 4, 0});
}

TEST_CASE("timeline: duration for the selected frames")
{
    Frames t(4);
    t.editor.selectFrames(1, 2);
    t.editor.setFramesDuration(250);
    t.editor.clearRange();
    std::vector<int> ms;
    for (int f = 0; f < 4; ++f) {
        t.editor.setCurrentFrame(f);
        ms.push_back(t.editor.frameDuration());
    }
    CHECK(ms == std::vector{100, 250, 250, 100});
    CHECK(t.editor.modified());
    t.editor.setCurrentFrame(3);
    t.editor.setFramesDuration(0); // clamped
    CHECK(t.editor.frameDuration() == 1);
}

TEST_CASE("timeline: a new document of the same shape never reads the old layers")
{
    // Same rows and columns, so rebuild() emits dataChanged during
    // setDocument; the layer list must already point into the new document.
    Editor editor;
    TimelineModel model;
    model.setEditor(&editor);
    REQUIRE(model.rowCount() == 1);
    QStringList seen;
    QObject::connect(&model, &TimelineModel::dataChanged, [&] {
        seen << cell(model, 0, 0, TimelineModel::NameRole).toString();
    });
    editor.newSprite({{"width", 32}, {"height", 16}});
    REQUIRE_FALSE(seen.isEmpty());
    for (const QString& name : seen)
        CHECK(name == "Layer 1");
}

TEST_CASE("timeline: a live layer's range is read from its recipe and dragged as one undo step")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    for (int i = 0; i < 29; ++i)
        editor.addFrame();
    TimelineModel model;
    model.setEditor(&editor);
    editor.addParticleLayer();
    const int row = model.rowForLayer(editor.currentLayer());
    const int plainRow = model.rowForLayer(0); // the layer the sprite started with, below it
    REQUIRE(row != plainRow);

    CHECK(model.liveRange(plainRow) == QPoint(-1, -1));
    CHECK(model.liveTail(plainRow) == 0);
    // The default stream: frames 0 to 15, particles living 12 frames (25% either way) after it.
    CHECK(model.liveRange(row) == QPoint(0, 15));
    CHECK(model.liveTail(row) == 14);
    editor.setParticleField("loop", 1, false);
    CHECK(model.liveTail(row) == 0);
    editor.setParticleField("loop", 0, false);

    // Moved and resized by dragging: many steps, one undo.
    editor.setParticleRange(2, 17, true);
    editor.setParticleRange(5, 20, true);
    CHECK(model.liveRange(row) == QPoint(5, 20));
    editor.setParticleRange(5, 10, true);
    CHECK(model.liveRange(row) == QPoint(5, 10));
    editor.setParticleRange(8, 3, true); // the end can't come before the start
    CHECK(model.liveRange(row) == QPoint(8, 8));
    CHECK(model.liveStream(row)); // one frame long, still a stream: it keeps its ends to drag
    CHECK_FALSE(model.liveStream(plainRow));
    editor.undo();
    CHECK(model.liveRange(row) == QPoint(0, 15));

    // A burst is a single frame.
    editor.setParticleField("emission", 0, false);
    editor.setParticleRange(4, 9, false);
    CHECK(model.liveRange(row) == QPoint(4, 4));
    CHECK_FALSE(model.liveStream(row));
}

TEST_CASE("timeline: a stream that began before the first frame or runs to the end shows that on its bar")
{
    Editor editor;
    editor.newSprite({{"width", 32}, {"height", 32}});
    for (int i = 0; i < 39; ++i)
        editor.addFrame();
    TimelineModel model;
    model.setEditor(&editor);
    editor.addParticleLayer();
    const int row = model.rowForLayer(editor.currentLayer());
    CHECK_FALSE(model.liveOpenEnded(row));
    editor.setParticleField("startFrame", -12, false);
    CHECK(model.liveRange(row).x() == -12); // the bar is clipped at the first frame when it is painted
    editor.setParticleField("untilEnd", 1, false);
    CHECK(model.liveOpenEnded(row));
    CHECK(model.liveRange(row) == QPoint(-12, 39)); // to the last frame, whatever its number
    editor.addFramesUpTo(60);
    CHECK(model.liveRange(row) == QPoint(-12, 59));
    // A burst has no end to run to.
    editor.setParticleField("emission", 0, false);
    CHECK_FALSE(model.liveOpenEnded(row));
}
