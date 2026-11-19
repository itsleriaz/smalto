#include <doctest/doctest.h>

#include "Editor.h"

#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

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

// --- Import Sprite Sheet and folders of animations -----------------------------------------------------

namespace {

// A PNG sheet of 3 x 2 tiles of 4 x 4, each filled with its own gray (20, 50, ... 170).
QString writeSheet(const QTemporaryDir& dir)
{
    QImage image(12, 8, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col) {
            const int shade = 20 + (row * 3 + col) * 30;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    image.setPixelColor(col * 4 + x, row * 4 + y, QColor(shade, shade, shade));
        }
    const QString path = dir.filePath("sheet.png");
    REQUIRE(image.save(path));
    return path;
}

int grayOf(const Editor& editor, int frame, int x, int y)
{
    // The composite shows the current frame only.
    REQUIRE(frame >= 0);
    return editor.composite().pixelColor(x, y).red();
}

} // namespace

TEST_CASE("sprite sheet: a new sprite of the tiles, the sheet untouched")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    REQUIRE(editor.openPath(writeSheet(dir)));
    const int tabs = int(editor.documents().size());

    const QVariantMap options{{"width", 4}, {"height", 4}, {"columns", 3}, {"rows", 2}};
    CHECK(editor.spriteSheetFit(options, false) == QSize(3, 2));
    REQUIRE(editor.importSpriteSheet(options));
    CHECK(int(editor.documents().size()) == tabs + 1);
    CHECK(editor.canvasWidth() == 4);
    CHECK(editor.canvasHeight() == 4);
    CHECK(editor.frameCount() == 6);
    CHECK(grayOf(editor, 0, 0, 0) == 20);
    editor.setCurrentFrame(4);
    CHECK(grayOf(editor, 4, 1, 1) == 140); // row 1, column 1

    // The sheet is still in its tab, as it was.
    editor.setCurrentDocument(tabs - 1);
    CHECK(editor.canvasWidth() == 12);
    CHECK(editor.frameCount() == 1);
}

TEST_CASE("sprite sheet: by columns, with a start offset, skipping empty tiles")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Editor editor;
    REQUIRE(editor.openPath(writeSheet(dir)));

    // Down the columns first: the second frame is the one below the first.
    REQUIRE(editor.importSpriteSheet(QVariantMap{{"width", 4}, {"height", 4}, {"columns", 3}, {"rows", 2},
                                                 {"byColumns", true}}));
    editor.setCurrentFrame(1);
    CHECK(grayOf(editor, 1, 0, 0) == 110);
    editor.setCurrentDocument(0);

    // Starting at x = 4 with a partial tile counted: columns 1 and 2 are whole, a third would be cut off.
    const QVariantMap shifted{{"x", 4}, {"width", 4}, {"height", 4}};
    CHECK(editor.spriteSheetFit(shifted, false).width() == 2);
    CHECK(editor.spriteSheetFit(shifted, true).width() == 2);
    const QVariantMap wide{{"x", 6}, {"width", 4}, {"height", 4}};
    CHECK(editor.spriteSheetFit(wide, false).width() == 1);
    CHECK(editor.spriteSheetFit(wide, true).width() == 2);

    // One 1 x 1 tile from the corner of the last tile; and settings that make no tiles are refused.
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    CHECK(editor.importSpriteSheet(QVariantMap{{"x", 11}, {"y", 7}, {"width", 1}, {"height", 1}, {"columns", 1},
                                               {"rows", 1}, {"skipEmpty", true}}));
    CHECK(grayOf(editor, 0, 0, 0) == 170);
    editor.setCurrentDocument(0);
    CHECK_FALSE(editor.importSpriteSheet(QVariantMap{{"width", 0}, {"height", 4}, {"columns", 1}, {"rows", 1}}));
    CHECK(errors.count() == 1);
}

TEST_CASE("image folders: one sprite with a tag for each animation")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto save = [&](const QString& name, int shade) {
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(QColor(shade, shade, shade));
        REQUIRE(image.save(dir.filePath(name)));
    };
    save("walk_1.png", 10);
    save("walk_2.png", 20);
    save("walk_3.png", 30);
    save("idle.png", 40);

    Editor editor;
    const QVariantList clips = editor.scanImageFolder(dir.path());
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].toMap().value("name").toString() == "idle");
    CHECK(clips[1].toMap().value("name").toString() == "walk");
    CHECK(clips[1].toMap().value("count").toInt() == 3);

    REQUIRE(editor.openImageClips(clips, dir.path()));
    CHECK(editor.frameCount() == 4);
    const QVariantList tags = editor.tags();
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].toMap().value("name").toString() == "idle");
    CHECK(tags[1].toMap().value("name").toString() == "walk");
    CHECK(tags[1].toMap().value("from").toInt() == 1);
    CHECK(tags[1].toMap().value("to").toInt() == 3);

    CHECK(editor.scanImageFolder(dir.filePath("nothing here")).isEmpty());
}
