#include "RecentFiles.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QVariantMap>

#include <memory>

namespace {

const QString kKey = QStringLiteral("recentFiles");

std::unique_ptr<QSettings> openSettings(const QString& file)
{
    return file.isEmpty() ? std::make_unique<QSettings>() : std::make_unique<QSettings>(file, QSettings::IniFormat);
}

} // namespace

RecentFiles::RecentFiles(QObject* parent)
    : QObject(parent)
{
}

RecentFiles::RecentFiles(const QString& settingsFile, QObject* parent)
    : QObject(parent)
    , m_settingsFile(settingsFile)
{
}

QStringList RecentFiles::paths() const
{
    QStringList out;
    for (const QString& path : openSettings(m_settingsFile)->value(kKey).toStringList())
        if (QFileInfo::exists(path) && !out.contains(path))
            out.append(path);
    return out.mid(0, kMaxFiles);
}

QVariantList RecentFiles::items() const
{
    const QString home = QDir::homePath();
    QVariantList out;
    for (const QString& path : paths()) {
        const QFileInfo info(path);
        QString folder = QDir::toNativeSeparators(info.absolutePath());
        if (folder.startsWith(QDir::toNativeSeparators(home)))
            folder = QLatin1Char('~') + folder.mid(home.size());
        out.append(QVariantMap{{"path", path}, {"name", info.fileName()}, {"folder", folder}});
    }
    return out;
}

void RecentFiles::add(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QStringList list = paths();
    list.removeAll(absolute);
    list.prepend(absolute);
    store(list.mid(0, kMaxFiles));
}

void RecentFiles::remove(const QString& path)
{
    QStringList list = paths();
    if (list.removeAll(QFileInfo(path).absoluteFilePath()) > 0)
        store(list);
}

void RecentFiles::clear() { store({}); }

void RecentFiles::store(const QStringList& paths)
{
    openSettings(m_settingsFile)->setValue(kKey, paths);
    emit changed();
}
