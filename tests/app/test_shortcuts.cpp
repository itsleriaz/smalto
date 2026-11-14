#include <doctest/doctest.h>

#include "Shortcuts.h"

#include <QKeySequence>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <set>

TEST_CASE("shortcuts: defaults, ids and no key used twice")
{
    QTemporaryDir dir;
    Shortcuts shortcuts(dir.filePath("settings.ini"));
    CHECK(shortcuts.keysFor("pencil") == QStringList{"B"});
    CHECK(shortcuts.keysFor("zoomIn") == QStringList{"+", "="});
    CHECK(shortcuts.keysFor("preferences") == QStringList{"Ctrl+,"});
    CHECK(!shortcuts.keysFor("undo").isEmpty()); // the platform's standard key
    CHECK(!shortcuts.customized());

    std::set<QString> ids, keys;
    for (const auto& a : Shortcuts::table()) {
        CHECK_MESSAGE(ids.insert(a.id).second, a.id.toStdString());
        for (const QString& key : a.defaults) {
            CHECK_MESSAGE(keys.insert(key).second, key.toStdString());
            CHECK_MESSAGE(!Shortcuts::isReserved(key), key.toStdString());
        }
    }
    CHECK(shortcuts.hints().value("pencil").toString() == " (B)");
    CHECK(shortcuts.labels().value("pencil").toString() == "B");
}

TEST_CASE("shortcuts: assigning moves a key, and changes are kept")
{
    QTemporaryDir dir;
    const QString file = dir.filePath("settings.ini");
    {
        Shortcuts shortcuts(file);
        QSignalSpy changed(&shortcuts, &Shortcuts::changed);
        // B moves from the pencil to the eraser.
        CHECK(shortcuts.assign("eraser", "B") == "Pencil");
        CHECK(shortcuts.keysFor("eraser") == QStringList{"E", "B"});
        CHECK(shortcuts.keysFor("pencil").isEmpty());
        CHECK(shortcuts.hints().value("pencil").toString().isEmpty());
        CHECK(shortcuts.owner("B") == "eraser");
        CHECK(changed.count() == 1);
        // Normalized: "ctrl+shift+k" is "Ctrl+Shift+K".
        CHECK(shortcuts.assign("save", "ctrl+shift+k").isEmpty());
        CHECK(shortcuts.keysFor("save") == QStringList{"Ctrl+S", "Ctrl+Shift+K"});
        shortcuts.unassign("save", "Ctrl+S");
        CHECK(shortcuts.keysFor("save") == QStringList{"Ctrl+Shift+K"});
        CHECK(shortcuts.customized());
    }
    // A new instance (next run) reads them back, including "no key".
    Shortcuts again(file);
    CHECK(again.keysFor("eraser") == QStringList{"E", "B"});
    CHECK(again.keysFor("pencil").isEmpty());
    CHECK(again.keysFor("save") == QStringList{"Ctrl+Shift+K"});

    // Resetting the pencil takes B back from the eraser.
    again.reset("pencil");
    CHECK(again.keysFor("pencil") == QStringList{"B"});
    CHECK(again.keysFor("eraser") == QStringList{"E"});
    again.resetAll();
    CHECK(!again.customized());
    CHECK(Shortcuts(file).keysFor("save") == QStringList{"Ctrl+S"});
}

TEST_CASE("shortcuts: fixed keys can't be assigned")
{
    QTemporaryDir dir;
    Shortcuts shortcuts(dir.filePath("settings.ini"));
    for (const char* key : {"Esc", "Return", "Enter", "Del", "Backspace", "Space", "Left", "Shift+Up", "Alt+Shift+Right"}) {
        CHECK_MESSAGE(Shortcuts::isReserved(key), key);
        CHECK(shortcuts.assign("pencil", key).isEmpty());
    }
    CHECK(shortcuts.keysFor("pencil") == QStringList{"B"});
    CHECK(!Shortcuts::isReserved("Ctrl+Left"));
    CHECK(!Shortcuts::isReserved("Ctrl+Space"));
    CHECK(!Shortcuts::isReserved("Alt+Backspace")); // a standard Undo key
    CHECK(shortcuts.assign("pencil", "nonsense+key+here").isEmpty());
    CHECK(shortcuts.keysFor("pencil") == QStringList{"B"});
}

TEST_CASE("shortcuts: key presses become portable text")
{
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_Shift, Qt::ShiftModifier).isEmpty());
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_Control, Qt::ControlModifier).isEmpty());
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_B, 0) == "B");
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier) == "Ctrl+Shift+S");
    // Shift typed the symbol: "+", not "Shift++".
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_Plus, Qt::ShiftModifier) == "+");
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_1, Qt::ShiftModifier) == "Shift+1");
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_Backtab, Qt::ShiftModifier) == "Shift+Tab");
    CHECK(Shortcuts::fromKeyEvent(Qt::Key_F3, Qt::KeypadModifier) == "F3");
    CHECK(Shortcuts::display("Ctrl+S") == QKeySequence("Ctrl+S").toString(QKeySequence::NativeText));
}
