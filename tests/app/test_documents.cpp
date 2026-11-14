#include <doctest/doctest.h>

#include "Editor.h"

#include <QSignalSpy>

namespace {

const QString kCorpus = QStringLiteral(SMALTO_CORPUS_DIR);

QStringList names(const Editor& editor)
{
    QStringList list;
    for (const QVariant& d : editor.documents())
        list << d.toMap()["name"].toString();
    return list;
}

void paint(Editor& editor, QPoint p)
{
    editor.setTool(Editor::Pencil);
    editor.beginStroke(p, false, {});
    editor.endStroke();
}

} // namespace

TEST_CASE("documents: the untouched startup sprite is replaced, others get tabs")
{
    Editor editor;
    REQUIRE(editor.documents().size() == 1);
    editor.newSprite({{"width", 8}, {"height", 8}});
    CHECK(names(editor) == QStringList{"Untitled"});
    editor.newSprite({{"width", 4}, {"height", 4}});
    CHECK(names(editor) == QStringList{"Untitled", "Untitled 2"});
    CHECK(editor.currentDocument() == 1);
    CHECK(editor.canvasWidth() == 4);

    // A touched startup sprite stays.
    Editor touched;
    paint(touched, {1, 1});
    touched.newSprite({{"width", 8}, {"height", 8}});
    CHECK(touched.documents().size() == 2);
}

TEST_CASE("documents: each has its own pixels, history and selection")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.setPrimaryColor(QColor(255, 0, 0));
    paint(editor, {2, 2});
    editor.selectAll();
    editor.newSprite({{"width", 6}, {"height", 6}});
    CHECK_FALSE(editor.canUndo());
    CHECK_FALSE(editor.hasSelection());
    CHECK(editor.composite().pixelColor(2, 2).alpha() == 0);

    QSignalSpy sized(&editor, &Editor::canvasSizeChanged);
    editor.setCurrentDocument(0);
    CHECK(sized.count() == 1);
    CHECK(editor.canvasWidth() == 8);
    CHECK(editor.composite().pixelColor(2, 2) == QColor(255, 0, 0));
    CHECK(editor.selection() == QRect(0, 0, 8, 8));
    CHECK(editor.modified());
    CHECK_FALSE(editor.isDocumentModified(1));
    CHECK(editor.documents()[0].toMap()["modified"] == true);
    editor.undo(); // Select All
    editor.undo(); // the pixel
    CHECK(editor.composite().pixelColor(2, 2).alpha() == 0);
    CHECK_FALSE(editor.modified());
}

TEST_CASE("documents: closing picks a neighbour; the last one leaves a new sprite")
{
    Editor editor;
    editor.newSprite({{"width", 1}, {"height", 1}});
    editor.newSprite({{"width", 2}, {"height", 2}});
    editor.newSprite({{"width", 3}, {"height", 3}});
    editor.setCurrentDocument(1);
    editor.closeDocument(0); // before the current one
    CHECK(editor.currentDocument() == 0);
    CHECK(editor.canvasWidth() == 2);
    editor.closeDocument(0); // the current one: its neighbour takes over
    CHECK(editor.documents().size() == 1);
    CHECK(editor.canvasWidth() == 3);
    editor.closeDocument(0);
    REQUIRE(editor.documents().size() == 1);
    CHECK(editor.canvasWidth() == 64);
    CHECK_FALSE(editor.modified());
    editor.newSprite({{"width", 5}, {"height", 5}}); // it's a placeholder again
    CHECK(editor.documents().size() == 1);
}

TEST_CASE("documents: opening an open file switches to it")
{
    Editor editor;
    REQUIRE(editor.openPath(kCorpus + "/indexed.aseprite"));
    editor.newSprite({{"width", 4}, {"height", 4}});
    REQUIRE(editor.documents().size() == 2);
    REQUIRE(editor.openPath(kCorpus + "/indexed.aseprite"));
    CHECK(editor.documents().size() == 2);
    CHECK(editor.currentDocument() == 0);
    CHECK(editor.fileName() == "indexed.aseprite");
    CHECK(editor.documentForPath(kCorpus + "/indexed.aseprite") == 0);
    CHECK(editor.documentForPath(kCorpus + "/missing.aseprite") == -1);
}

TEST_CASE("documents: a paste lands before switching; cels paste only across the same mode")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.setPrimaryColor(QColor(0, 255, 0));
    paint(editor, {1, 1});
    editor.copy();
    editor.newSprite({{"width", 8}, {"height", 8}});
    editor.paste();
    REQUIRE(editor.floating());
    editor.setCurrentDocument(0);
    CHECK_FALSE(editor.floating());
    editor.setCurrentDocument(1);
    CHECK(editor.composite().pixelColor(1, 1) == QColor(0, 255, 0)); // landed where it was pasted

    editor.copyTimeline();
    CHECK(editor.canPasteTimeline());
    editor.newSprite({{"width", 8}, {"height", 8}, {"colorMode", "indexed"}});
    CHECK_FALSE(editor.canPasteTimeline());
    const int frames = editor.frameCount();
    editor.pasteTimeline();
    CHECK(editor.frameCount() == frames);
    editor.setCurrentDocument(0);
    CHECK(editor.canPasteTimeline());
}

TEST_CASE("documents: tabs move, the current one stays current")
{
    Editor editor;
    editor.newSprite({{"width", 8}, {"height", 8}});  // replaces the startup sprite
    editor.newSprite({{"width", 9}, {"height", 9}});
    editor.newSprite({{"width", 10}, {"height", 10}});
    REQUIRE(editor.documents().size() == 3);
    const auto names = [&] {
        QStringList out;
        for (const QVariant& d : editor.documents())
            out << d.toMap().value("name").toString();
        return out;
    };
    const QStringList before = names();
    editor.setCurrentDocument(0);
    CHECK(editor.canvasWidth() == 8);

    editor.moveDocument(0, 2); // the current one, to the end
    CHECK(names() == QStringList{before[1], before[2], before[0]});
    CHECK(editor.currentDocument() == 2);
    CHECK(editor.canvasWidth() == 8);

    editor.moveDocument(1, 0); // another one past it
    CHECK(names() == QStringList{before[2], before[1], before[0]});
    CHECK(editor.currentDocument() == 2);
    editor.moveDocument(2, 0); // and back to the front
    CHECK(names() == QStringList{before[0], before[2], before[1]});
    CHECK(editor.currentDocument() == 0);
    editor.setCurrentDocument(1);
    CHECK(editor.canvasWidth() == 10); // the parked ones moved with their names
    editor.moveDocument(0, 5); // out of range: nothing
    CHECK(names()[1] == before[2]);
    CHECK(Editor::fileExists(QStringLiteral("/no/such/file.aseprite")) == false);
}
