#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Palette presets: the built-in ones (":/palettes/*.gpl", in file name order)
// and the user's own, saved as .gpl in the app data folder. Load one with
// Editor.loadPalette(path); save one with Editor.savePalette(presetPath(name), name).
class PaletteLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // {name, path, builtin, colors: [color]} maps, built-ins first, then the
    // user's presets by name.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)

public:
    explicit PaletteLibrary(QObject* parent = nullptr);
    // Keeps user presets in `userFolder` instead of the app data folder (tests).
    explicit PaletteLibrary(const QString& userFolder, QObject* parent = nullptr);

    QVariantList presets() const { return m_presets; }

    // Where a user preset named `name` is saved (the folder is created).
    Q_INVOKABLE QString presetPath(const QString& name) const;
    Q_INVOKABLE bool hasPreset(const QString& name) const;
    // Saves `colors` as a user preset named `name` (replacing one of that
    // name) and reloads; the sprite's palette isn't touched.
    Q_INVOKABLE bool savePreset(const QString& name, const QVariantList& colors);
    // Renames a user preset (its file and its name inside). False for a
    // built-in, an empty name, or a name another preset already has.
    Q_INVOKABLE bool renamePreset(const QString& path, const QString& newName);
    // Removes a user preset (built-ins can't be removed).
    Q_INVOKABLE bool removePreset(const QString& path);
    Q_INVOKABLE void reload();

signals:
    void presetsChanged();

private:
    QString m_userFolder;
    QVariantList m_presets;
};
