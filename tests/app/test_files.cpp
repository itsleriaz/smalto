#include <doctest/doctest.h>

#include "FileOpenQueue.h"
#include "RecentFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileOpenEvent>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

QString touch(const QTemporaryDir& dir, const QString& name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

TEST_CASE("recent files: newest first, no duplicates, capped, persisted")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString settings = dir.filePath("settings.ini");
    RecentFiles recent(settings);
    QSignalSpy changed(&recent, &RecentFiles::changed);

    const QString a = touch(dir, "a.aseprite");
    const QString b = touch(dir, "b.aseprite");
    recent.add(a);
    recent.add(b);
    recent.add(a); // moves back to the top
    CHECK(recent.paths() == QStringList{a, b});
    CHECK(changed.count() == 3);

    const QVariantMap item = recent.items().first().toMap();
    CHECK(item.value("name") == "a.aseprite");
    CHECK(item.value("path") == a);

    for (int i = 0; i < 15; ++i)
        recent.add(touch(dir, QString("f%1.aseprite").arg(i)));
    CHECK(recent.paths().size() == RecentFiles::kMaxFiles);
    CHECK(recent.paths().first().endsWith("f14.aseprite"));

    // Another instance reads the same settings.
    CHECK(RecentFiles(settings).paths() == recent.paths());

    recent.remove(recent.paths().first());
    CHECK(recent.paths().first().endsWith("f13.aseprite"));
    recent.clear();
    CHECK(recent.paths().isEmpty());
}

TEST_CASE("recent files: missing files are dropped")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    RecentFiles recent(dir.filePath("settings.ini"));
    const QString kept = touch(dir, "kept.aseprite");
    const QString gone = touch(dir, "gone.aseprite");
    recent.add(kept);
    recent.add(gone);
    QFile::remove(gone);
    CHECK(recent.paths() == QStringList{kept});
}

TEST_CASE("file open queue: collects OS open events and paths")
{
    FileOpenQueue queue(nullptr);
    QSignalSpy queued(&queue, &FileOpenQueue::filesQueued);
    QCoreApplication::instance()->installEventFilter(&queue);

    QFileOpenEvent event(QStringLiteral("/tmp/from-finder.aseprite"));
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
    queue.push(QStringLiteral("relative.ase"));
    CHECK(queued.count() == 2);

    const QStringList files = queue.take();
    REQUIRE(files.size() == 2);
    CHECK(files[0] == QFileInfo("/tmp/from-finder.aseprite").absoluteFilePath()); // C:/tmp/... on Windows
    CHECK(QFileInfo(files[1]).isAbsolute());
    CHECK(queue.take().isEmpty());
    QCoreApplication::instance()->removeEventFilter(&queue);
}
