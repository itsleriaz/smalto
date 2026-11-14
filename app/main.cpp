#include "ClickAway.h"
#include "FileOpenQueue.h"
#include "Migration.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickWindow>
#ifdef Q_OS_MACOS
#include "MacTitleBar.h"
#endif

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Smalto"));
    QGuiApplication::setOrganizationName(QStringLiteral("Smalto"));
    QGuiApplication::setApplicationVersion(QStringLiteral(SMALTO_VERSION));
#ifdef Q_OS_LINUX
    // Names resources/linux/smalto.desktop, which gives the window its icon and name.
    QGuiApplication::setDesktopFileName(QStringLiteral("smalto"));
#endif

    // Before the engine, so files opened at launch (Finder, command line)
    // wait for QML. Declared first, destroyed last.
    FileOpenQueue openQueue(nullptr);
    app.installEventFilter(&openQueue);
    // --screenshot out.png [--size WxH] [--theme NAME] [--mode dark|light]
    // [--set group/key=value ...]
    // [file]: render the window once into a PNG and quit, with settings and
    // recovery copies in a throwaway folder (see README).
    const QStringList args = QGuiApplication::arguments();
    QString shotPath, shotTheme, shotMode;
    QStringList shotSettings; // --set group/key=value, seeded before QML reads them
    QSize shotSize(1440, 900);
    for (qsizetype i = 1; i < args.size(); ++i) {
        const auto value = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
        if (args[i] == QLatin1String("--screenshot")) {
            shotPath = value();
        } else if (args[i] == QLatin1String("--size")) {
            const QStringList wh = value().split(QLatin1Char('x'));
            if (wh.size() == 2)
                shotSize = QSize(wh[0].toInt(), wh[1].toInt());
        } else if (args[i] == QLatin1String("--theme")) {
            shotTheme = value();
        } else if (args[i] == QLatin1String("--mode")) {
            shotMode = value();
        } else if (args[i] == QLatin1String("--set")) {
            shotSettings.append(value());
        } else if (!args[i].startsWith(QLatin1Char('-'))) {
            openQueue.push(args[i]);
        }
    }

    // Once, on the first run after the rename: Gridline's settings and data.
    // Not for screenshots, which use throwaway settings.
    if (shotPath.isEmpty())
        migration::fromGridline();

    QTemporaryDir shotDir;
    if (!shotPath.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, shotDir.path());
        qputenv("SMALTO_RECOVERY_DIR", QDir(shotDir.path()).filePath(QStringLiteral("recovery")).toUtf8());
        QSettings settings;
        settings.setValue(QStringLiteral("window/width"), shotSize.width());
        settings.setValue(QStringLiteral("window/height"), shotSize.height());
        static const QStringList themes = {"basic", "everforest", "tokyonight", "catppuccin", "gruvbox"};
        const qsizetype themeIndex = themes.indexOf(QString(shotTheme).remove(QLatin1Char(' ')).toLower());
        if (themeIndex >= 0)
            settings.setValue(QStringLiteral("appearance/palette"), int(themeIndex));
        if (shotMode == QLatin1String("dark") || shotMode == QLatin1String("light"))
            settings.setValue(QStringLiteral("appearance/theme"), shotMode == QLatin1String("dark") ? 1 : 2);
        for (const QString& pair : std::as_const(shotSettings)) {
            const qsizetype eq = pair.indexOf(QLatin1Char('='));
            if (eq > 0) {
                const QString value = pair.mid(eq + 1);
                settings.setValue(pair.left(eq), value == QLatin1String("true") ? QVariant(true)
                                                  : value == QLatin1String("false") ? QVariant(false) : QVariant(value));
            }
        }
        settings.sync();
    }

    ClickAway clickAway;
    app.installEventFilter(&clickAway);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("Smalto", "Main");
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QLatin1String("cocoa")) // not offscreen: no NSView there
        for (QObject* root : engine.rootObjects())
            if (auto* window = qobject_cast<QQuickWindow*>(root))
                hideTitleText(window);
#endif

    if (!shotPath.isEmpty()) {
        QQuickWindow* window = nullptr;
        for (QObject* root : engine.rootObjects())
            if (!window)
                window = qobject_cast<QQuickWindow*>(root);
        if (!window)
            return EXIT_FAILURE;
        int status = EXIT_SUCCESS;
        // Give the first frames (canvas upload, files opening) time to land.
        QTimer::singleShot(1500, &app, [&] {
            const QImage image = window->grabWindow();
            if (image.isNull() || !image.save(shotPath))
                status = EXIT_FAILURE;
            QCoreApplication::exit(status);
        });
        const int code = QGuiApplication::exec();
        return code != 0 ? code : status;
    }

    return QGuiApplication::exec();
}
