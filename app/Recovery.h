#pragma once

#include "core/Document.h"

#include <QDateTime>
#include <QLockFile>
#include <QString>
#include <QThreadPool>

#include <memory>
#include <vector>

// Crash recovery files. Each running app writes into its own session folder
// under `root`, held by a lock file while it runs, and removes the folder
// when it quits normally. A session folder whose lock is gone or stale was
// left by an app that crashed: its documents can be restored.
//
// Writes run on a single background thread, in order: `save` takes a deep
// copy of the document, so editing goes on meanwhile; `remove` queues behind
// any pending write of the same document.
class RecoveryStore {
public:
    explicit RecoveryStore(QString root);
    ~RecoveryStore();

    // Writes `id`.aseprite (and a note with the original path, tab name and where the work was: the
    // current frame and layer row).
    void save(const QString& id, const core::Document& doc, const QString& path, const QString& name, int frame = 0,
              int layer = 0);
    void remove(const QString& id);
    // Blocks until queued writes are done.
    void wait();

    struct Entry {
        QString file; // the recovery copy
        QString path; // the document's own file, empty if never saved
        QString name; // its tab name
        QDateTime saved;
        int frame = 0; // where the work was
        int layer = 0; // the current layer's row
    };
    // Documents left by crashed sessions, oldest first. Claims those sessions
    // (holds their locks) so no other app restores them too.
    std::vector<Entry> crashed();
    // Deletes the claimed sessions' folders.
    void discardCrashed();

    // Quitting normally: this session's folder goes (unless `keep`, for tests
    // that simulate a crash: the files stay and the lock is released).
    void endSession(bool keep = false);

private:
    void startSession();

    QString m_root;
    QString m_session; // this app's folder, created on the first write
    std::unique_ptr<QLockFile> m_lock;
    std::vector<std::unique_ptr<QLockFile>> m_claimed; // crashed sessions' locks
    std::vector<QString> m_claimedFolders;
    QThreadPool m_pool;
    bool m_ended = false;
};
