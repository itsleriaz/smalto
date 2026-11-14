#include "Recovery.h"

#include "formats/Aseprite.h"
#include "formats/FileIO.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

namespace {

const QString kLockName = QStringLiteral("session.lock");

std::filesystem::path toFsPath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

} // namespace

RecoveryStore::RecoveryStore(QString root)
    : m_root(std::move(root))
{
    m_pool.setMaxThreadCount(1); // writes stay in order
}

RecoveryStore::~RecoveryStore()
{
    endSession();
}

// The session folder and its lock, on first use: an app that never had
// anything to recover leaves nothing behind.
void RecoveryStore::startSession()
{
    if (!m_session.isEmpty())
        return;
    m_session = QDir(m_root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(m_session);
    m_lock = std::make_unique<QLockFile>(QDir(m_session).filePath(kLockName));
    m_lock->setStaleLockTime(0); // held as long as the app runs, however long
    m_lock->tryLock(0);
}

void RecoveryStore::save(const QString& id, const core::Document& doc, const QString& path, const QString& name, int frame,
                         int layer)
{
    startSession();
    const QString base = QDir(m_session).filePath(id);
    std::shared_ptr<core::Document> copy = core::cloneDocument(doc);
    const QJsonObject note{{"path", path},
                           {"name", name},
                           {"saved", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                           {"frame", frame},
                           {"layer", layer}};
    m_pool.start([copy, base, note] {
        const auto bytes = formats::writeAseprite(*copy);
        std::string error;
        if (!formats::writeFileAtomically(toFsPath(base + ".aseprite"), bytes, &error))
            return;
        const QByteArray json = QJsonDocument(note).toJson();
        formats::writeFileAtomically(toFsPath(base + ".json"), std::vector<uint8_t>(json.begin(), json.end()), &error);
    });
}

void RecoveryStore::remove(const QString& id)
{
    if (m_session.isEmpty())
        return;
    const QString base = QDir(m_session).filePath(id);
    m_pool.start([base] {
        QFile::remove(base + ".aseprite");
        QFile::remove(base + ".json");
    });
}

void RecoveryStore::wait() { m_pool.waitForDone(); }

std::vector<RecoveryStore::Entry> RecoveryStore::crashed()
{
    std::vector<Entry> entries;
    const QDir root(m_root);
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString folder = root.filePath(name);
        if (folder == m_session
            || std::find(m_claimedFolders.begin(), m_claimedFolders.end(), folder) != m_claimedFolders.end())
            continue;
        // A live app holds its lock; a crashed one left it stale (or none).
        auto lock = std::make_unique<QLockFile>(QDir(folder).filePath(kLockName));
        lock->setStaleLockTime(0); // only a dead owner makes it stale
        if (!lock->tryLock(0))
            continue;
        const QDir dir(folder);
        for (const QString& note : dir.entryList({"*.json"}, QDir::Files)) {
            QFile file(dir.filePath(note));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
            const QString copy = dir.filePath(QFileInfo(note).completeBaseName() + ".aseprite");
            if (!QFile::exists(copy))
                continue;
            entries.push_back({copy, o.value("path").toString(), o.value("name").toString(),
                               QDateTime::fromString(o.value("saved").toString(), Qt::ISODate), o.value("frame").toInt(),
                               o.value("layer").toInt()});
        }
        m_claimed.push_back(std::move(lock));
        m_claimedFolders.push_back(folder);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.saved < b.saved; });
    return entries;
}

void RecoveryStore::discardCrashed()
{
    m_claimed.clear(); // unlock first: the lock file is inside the folder
    for (const QString& folder : m_claimedFolders)
        QDir(folder).removeRecursively();
    m_claimedFolders.clear();
}

void RecoveryStore::endSession(bool keep)
{
    if (m_ended)
        return;
    m_ended = true;
    m_pool.waitForDone();
    if (m_session.isEmpty())
        return;
    m_lock.reset(); // unlocks (removes the lock file)
    if (!keep)
        QDir(m_session).removeRecursively();
}
