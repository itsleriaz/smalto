#include <doctest/doctest.h>

#include "Editor.h"

#include <QDir>
#include <QDirIterator>
#include <QTest>
#include <QTemporaryDir>

namespace {

void paint(Editor& editor, QPoint p, QColor color = QColor(255, 0, 0))
{
    editor.setPrimaryColor(color);
    editor.setTool(Editor::Pencil);
    editor.beginStroke(p, false, {});
    editor.endStroke();
}

// Recovery copies anywhere under `root`.
QStringList copies(const QString& root)
{
    QStringList list;
    QDirIterator it(root, {"*.aseprite"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        list << it.next();
    return list;
}

} // namespace

TEST_CASE("recovery: unsaved documents are copied; saving removes the copy")
{
    QTemporaryDir root, files;
    Editor editor;
    editor.setRecoveryRoot(root.path());
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.autosaveNow();
    editor.waitForAutosave();
    CHECK(copies(root.path()).isEmpty()); // nothing unsaved yet

    paint(editor, {1, 1});
    editor.autosaveNow();
    editor.waitForAutosave();
    CHECK(copies(root.path()).size() == 1);

    REQUIRE(editor.saveFileAs(QUrl::fromLocalFile(files.filePath("a.aseprite"))));
    editor.waitForAutosave();
    CHECK(copies(root.path()).isEmpty());

    // A running app's session is never offered to another one.
    paint(editor, {2, 2});
    editor.autosaveNow();
    editor.waitForAutosave();
    Editor other;
    other.setRecoveryRoot(root.path());
    CHECK(other.recoverableDocuments().isEmpty());
    CHECK(copies(root.path()).size() == 1);
}

TEST_CASE("recovery: after a crash the documents come back as unsaved tabs")
{
    QTemporaryDir root, files;
    const QString saved = files.filePath("saved.aseprite");
    {
        Editor crashed;
        crashed.setRecoveryRoot(root.path());
        crashed.newSprite({{"width", 8}, {"height", 8}});
        REQUIRE(crashed.saveFileAs(QUrl::fromLocalFile(saved)));
        paint(crashed, {3, 3}, QColor(0, 0, 255));
        crashed.newSprite({{"width", 4}, {"height", 4}}); // never saved
        paint(crashed, {1, 1});
        crashed.autosaveNow();
        crashed.waitForAutosave();
        crashed.setKeepRecoveryOnExit(true); // as if it crashed
    }
    REQUIRE(copies(root.path()).size() == 2);

    Editor editor;
    editor.setRecoveryRoot(root.path());
    const QVariantList found = editor.recoverableDocuments();
    REQUIRE(found.size() == 2);
    QStringList names;
    for (const QVariant& d : found)
        names << d.toMap()["name"].toString();
    CHECK(names.contains("saved.aseprite"));
    CHECK(names.contains("Untitled")); // the first one was saved under its own name

    CHECK(editor.restoreRecovered() == 2);
    CHECK(editor.documents().size() == 2); // the startup sprite was replaced
    const int savedTab = editor.documentForPath(saved);
    REQUIRE(savedTab >= 0);
    editor.setCurrentDocument(savedTab);
    CHECK(editor.modified());
    CHECK(editor.composite().pixelColor(3, 3) == QColor(0, 0, 255));
    CHECK(editor.isDocumentModified(1 - savedTab));
    CHECK(editor.documents()[1 - savedTab].toMap()["name"] == "Untitled");

    // The crashed session is gone; the restored ones are this session's now.
    editor.waitForAutosave();
    CHECK(copies(root.path()).size() == 2);
    Editor later;
    later.setRecoveryRoot(root.path());
    CHECK(later.recoverableDocuments().isEmpty());
}

TEST_CASE("recovery: discarding deletes the copies; closing a tab removes its copy")
{
    QTemporaryDir root;
    {
        Editor crashed;
        crashed.setRecoveryRoot(root.path());
        paint(crashed, {1, 1});
        crashed.autosaveNow();
        crashed.waitForAutosave();
        crashed.setKeepRecoveryOnExit(true);
    }
    Editor editor;
    editor.setRecoveryRoot(root.path());
    REQUIRE(editor.recoverableDocuments().size() == 1);
    editor.discardRecovered();
    CHECK(copies(root.path()).isEmpty());
    CHECK(editor.documents().size() == 1);

    paint(editor, {1, 1});
    editor.newSprite({{"width", 4}, {"height", 4}});
    paint(editor, {1, 1});
    editor.autosaveNow();
    editor.waitForAutosave();
    REQUIRE(copies(root.path()).size() == 2);
    editor.closeDocument(1);
    editor.waitForAutosave();
    CHECK(copies(root.path()).size() == 1);
}

TEST_CASE("recovery: quitting normally leaves nothing")
{
    QTemporaryDir root;
    {
        Editor editor;
        editor.setRecoveryRoot(root.path());
        paint(editor, {1, 1});
        editor.autosaveNow();
        editor.waitForAutosave();
        REQUIRE(copies(root.path()).size() == 1);
    }
    CHECK(copies(root.path()).isEmpty());
    CHECK(QDir(root.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

TEST_CASE("recovery: the copy waits autosaveDelay seconds; 0 turns it off")
{
    QTemporaryDir root;
    Editor editor;
    editor.setRecoveryRoot(root.path());
    CHECK(editor.autosaveDelay() == 15);
    editor.newSprite({{"width", 8}, {"height", 8}});

    editor.setAutosaveDelay(0);
    paint(editor, {1, 1});
    QTest::qWait(1300);
    editor.waitForAutosave();
    CHECK(copies(root.path()).isEmpty());

    // Turned on again: the change made while off is copied too.
    editor.setAutosaveDelay(1);
    CHECK(QTest::qWaitFor([&] { editor.waitForAutosave(); return copies(root.path()).size() == 1; }, 3000));
}

TEST_CASE("recovery: a restored tab is on the frame and layer where the work was")
{
    QTemporaryDir root;
    {
        Editor crashed;
        crashed.setRecoveryRoot(root.path());
        crashed.newSprite({{"width", 8}, {"height", 8}, {"frames", 4}});
        crashed.addLayer();
        crashed.addLayer(); // three layers
        crashed.setCurrentFrame(2);
        crashed.setCurrentLayer(1);
        paint(crashed, {1, 1});
        // The tab is another one when the autosave comes, so the saved note is the other tab's place.
        crashed.newSprite({{"width", 4}, {"height", 4}, {"frames", 2}});
        crashed.addLayer();
        crashed.setCurrentFrame(1);
        paint(crashed, {2, 2});
        crashed.autosaveNow();
        crashed.waitForAutosave();
        crashed.setKeepRecoveryOnExit(true); // as if it crashed
    }
    Editor editor;
    editor.setRecoveryRoot(root.path());
    REQUIRE(editor.recoverableDocuments().size() == 2);
    REQUIRE(editor.restoreRecovered() == 2);
    // The tabs come back in no fixed order (they were saved in the same second): find each by its size.
    REQUIRE(editor.documents().size() == 2);
    int wide = -1, narrow = -1;
    for (int i = 0; i < 2; ++i) {
        editor.setCurrentDocument(i);
        (editor.canvasWidth() == 8 ? wide : narrow) = i;
    }
    REQUIRE(wide >= 0);
    REQUIRE(narrow >= 0);
    // The 8 x 8 one on frame 2 and its middle layer; the 4 x 4 one on frame 1 and its new layer.
    editor.setCurrentDocument(wide);
    CHECK(editor.currentFrame() == 2);
    CHECK(editor.currentLayer() == 1);
    editor.setCurrentDocument(narrow);
    CHECK(editor.currentFrame() == 1);
    CHECK(editor.currentLayer() == 1);
}
