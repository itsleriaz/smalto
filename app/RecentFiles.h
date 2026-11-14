#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Recently opened or saved files, newest first, kept in QSettings. Files that
// no longer exist are dropped when the list is read.
class RecentFiles : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // {path, name, folder} maps for menus; folder uses ~ for the home folder.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)

public:
    static constexpr int kMaxFiles = 10;

    explicit RecentFiles(QObject* parent = nullptr);
    // Uses an INI file instead of the app's settings (tests).
    explicit RecentFiles(const QString& settingsFile, QObject* parent = nullptr);

    QStringList paths() const;
    QVariantList items() const;

    Q_INVOKABLE void add(const QString& path);
    Q_INVOKABLE void remove(const QString& path);
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    void store(const QStringList& paths);

    QString m_settingsFile;
};
