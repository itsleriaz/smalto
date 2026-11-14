#include "Migration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace migration {

bool copySettings(QSettings& from, QSettings& to)
{
    if (!to.allKeys().isEmpty())
        return false;
    const QStringList keys = from.allKeys();
    if (keys.isEmpty())
        return false;
    for (const QString& key : keys)
        to.setValue(key, from.value(key));
    to.sync();
    return true;
}

bool moveFolder(const QString& from, const QString& to)
{
    if (from.isEmpty() || to.isEmpty() || !QFileInfo(from).isDir() || QFileInfo::exists(to))
        return false;
    QDir().mkpath(QFileInfo(to).absolutePath());
    return QDir().rename(from, to);
}

void fromGridline()
{
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    if (org == QLatin1String("Gridline") && app == QLatin1String("Gridline"))
        return;

    // Where Gridline kept its data: the same rule, under the old names.
    QCoreApplication::setOrganizationName(QStringLiteral("Gridline"));
    QCoreApplication::setApplicationName(QStringLiteral("Gridline"));
    const QString oldData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    const QString newData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QSettings oldSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("Gridline"), QStringLiteral("Gridline"));
    QSettings newSettings(QSettings::NativeFormat, QSettings::UserScope, org, app);
    copySettings(oldSettings, newSettings);
    moveFolder(oldData, newData);
}

} // namespace migration
