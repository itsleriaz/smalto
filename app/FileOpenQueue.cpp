#include "FileOpenQueue.h"

#include <QFileInfo>
#include <QFileOpenEvent>
#include <QJSEngine>

namespace {
FileOpenQueue* s_instance = nullptr;
}

FileOpenQueue::FileOpenQueue(QObject* parent)
    : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
}

FileOpenQueue::~FileOpenQueue() { s_instance = nullptr; }

FileOpenQueue* FileOpenQueue::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void FileOpenQueue::push(const QString& path)
{
    m_paths.append(QFileInfo(path).absoluteFilePath());
    emit filesQueued();
}

QStringList FileOpenQueue::take() { return std::exchange(m_paths, {}); }

bool FileOpenQueue::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FileOpen) {
        const auto* open = static_cast<QFileOpenEvent*>(event);
        if (!open->file().isEmpty()) {
            push(open->file());
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
