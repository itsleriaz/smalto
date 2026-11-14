#include "Manual.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

QString Manual::folder()
{
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + QStringLiteral("/../Resources/manual"),       // macOS bundle
        app + QStringLiteral("/manual"),                    // Windows zip
        app + QStringLiteral("/../share/smalto/manual"),    // AppImage
#ifdef SMALTO_SOURCE_DIR
        QStringLiteral(SMALTO_SOURCE_DIR "/manual/book"),   // a build from the source tree
#endif
    };
    for (const QString& dir : candidates) {
        if (QFileInfo::exists(dir + QStringLiteral("/index.html")))
            return QDir::cleanPath(dir);
    }
    return {};
}

bool Manual::open(const QString& page)
{
    const QString dir = folder();
    if (dir.isEmpty())
        return false;
    QUrl url = QUrl::fromLocalFile(dir + QLatin1Char('/') + (page.isEmpty() ? QStringLiteral("index.html") : page));
    // fromLocalFile treats "#" as part of the name: split the anchor off.
    const qsizetype hash = page.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        url = QUrl::fromLocalFile(dir + QLatin1Char('/') + page.left(hash));
        url.setFragment(page.mid(hash + 1));
    }
    return QDesktopServices::openUrl(url);
}
