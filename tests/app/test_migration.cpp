#include <doctest/doctest.h>

#include "Migration.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

TEST_CASE("migration: Gridline's settings are copied once into empty new settings")
{
    QTemporaryDir dir;
    QSettings oldSettings(dir.filePath("old.ini"), QSettings::IniFormat);
    oldSettings.setValue("preferences/panelStyle", 1);
    oldSettings.setValue("window/panelLayout", "{\"left\":[\"layers\"]}");
    QSettings newSettings(dir.filePath("new.ini"), QSettings::IniFormat);

    CHECK(migration::copySettings(oldSettings, newSettings));
    CHECK(newSettings.value("preferences/panelStyle").toInt() == 1);
    CHECK(newSettings.value("window/panelLayout").toString() == "{\"left\":[\"layers\"]}");
    CHECK(oldSettings.value("preferences/panelStyle").toInt() == 1); // left as it was

    // Settings of its own already: nothing is overwritten.
    newSettings.setValue("preferences/panelStyle", 0);
    oldSettings.setValue("preferences/panelStyle", 1);
    CHECK_FALSE(migration::copySettings(oldSettings, newSettings));
    CHECK(newSettings.value("preferences/panelStyle").toInt() == 0);

    // Nothing to copy.
    QSettings none(dir.filePath("none.ini"), QSettings::IniFormat);
    QSettings fresh(dir.filePath("fresh.ini"), QSettings::IniFormat);
    CHECK_FALSE(migration::copySettings(none, fresh));
}

TEST_CASE("migration: the data folder moves when the new one doesn't exist yet")
{
    QTemporaryDir dir;
    const QString from = dir.filePath("Gridline/Gridline");
    const QString to = dir.filePath("Smalto/Smalto");
    REQUIRE(QDir().mkpath(from + "/brushes"));
    QFile file(from + "/brushes/a.png");
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    CHECK(migration::moveFolder(from, to));
    CHECK(QFile::exists(to + "/brushes/a.png"));
    CHECK_FALSE(QDir(from).exists());

    // Already there (or nothing to move): left alone.
    REQUIRE(QDir().mkpath(from));
    CHECK_FALSE(migration::moveFolder(from, to));
    CHECK_FALSE(migration::moveFolder(dir.filePath("missing"), dir.filePath("other")));
}
