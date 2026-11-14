#include "PaletteLibrary.h"

#include "formats/PaletteIO.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

// The preset at `path` as a list entry, or an empty map if it doesn't parse.
QVariantMap readPreset(const QString& path, bool builtin)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    const auto loaded = formats::readGpl(std::string_view(bytes.constData(), size_t(bytes.size())));
    if (!loaded)
        return {};
    QVariantList colors;
    for (const auto& e : loaded->palette.entries)
        colors.append(QColor(e.color.r, e.color.g, e.color.b, e.color.a));
    const QString name = loaded->name.empty() ? QFileInfo(path).completeBaseName() : QString::fromStdString(loaded->name);
    return {{"name", name}, {"path", path}, {"builtin", builtin}, {"colors", colors}};
}

} // namespace

PaletteLibrary::PaletteLibrary(QObject* parent)
    : PaletteLibrary(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/palettes", parent)
{
}

PaletteLibrary::PaletteLibrary(const QString& userFolder, QObject* parent)
    : QObject(parent)
    , m_userFolder(userFolder)
{
    reload();
}

void PaletteLibrary::reload()
{
    m_presets.clear();
    const auto add = [this](const QString& folder, bool builtin) {
        const QDir dir(folder);
        for (const QString& name : dir.entryList({"*.gpl"}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            const QVariantMap preset = readPreset(dir.filePath(name), builtin);
            if (!preset.isEmpty())
                m_presets.append(preset);
        }
    };
    add(":/palettes", true);
    add(m_userFolder, false);
    emit presetsChanged();
}

QString PaletteLibrary::presetPath(const QString& name) const
{
    // Keep the name readable but safe as a file name everywhere.
    QString file = name.trimmed();
    static const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for (QChar& c : file)
        if (forbidden.contains(c) || c.unicode() < 32)
            c = '_';
    if (file.isEmpty() || file.startsWith('.'))
        file.prepend("palette");
    QDir().mkpath(m_userFolder);
    return QDir(m_userFolder).filePath(file + ".gpl");
}

bool PaletteLibrary::hasPreset(const QString& name) const
{
    for (const QVariant& p : m_presets)
        if (!p.toMap().value("builtin").toBool() && p.toMap().value("name").toString().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return QFileInfo::exists(presetPath(name));
}

bool PaletteLibrary::savePreset(const QString& name, const QVariantList& colors)
{
    core::Palette palette;
    for (const QVariant& v : colors) {
        const QColor c = v.value<QColor>();
        if (c.isValid())
            palette.entries.push_back({{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), uint8_t(c.alpha())}, {}});
    }
    if (name.trimmed().isEmpty() || palette.entries.empty())
        return false;
    const std::string text = formats::writeGpl(palette, name.trimmed().toStdString());
    QSaveFile file(presetPath(name)); // written whole or not at all
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(text.data(), qint64(text.size())) == qint64(text.size())
                 && file.commit();
    reload();
    return ok;
}

bool PaletteLibrary::renamePreset(const QString& path, const QString& newName)
{
    const QString name = newName.trimmed();
    if (name.isEmpty() || !QFileInfo(path).absoluteFilePath().startsWith(QFileInfo(m_userFolder).absoluteFilePath() + "/"))
        return false;
    const QString target = presetPath(name);
    const bool sameFile = QFileInfo(target).absoluteFilePath() == QFileInfo(path).absoluteFilePath();
    if (!sameFile && (hasPreset(name) || QFileInfo::exists(target)))
        return false;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = in.readAll();
    in.close();
    auto loaded = formats::readGpl(std::string_view(bytes.constData(), size_t(bytes.size())));
    if (!loaded)
        return false;
    const std::string text = formats::writeGpl(loaded->palette, name.toStdString());
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(text.data(), qint64(text.size())) != qint64(text.size())
        || !out.commit())
        return false;
    if (!sameFile)
        QFile::remove(path);
    reload();
    return true;
}

bool PaletteLibrary::removePreset(const QString& path)
{
    if (!QFileInfo(path).absolutePath().startsWith(QFileInfo(m_userFolder).absoluteFilePath()))
        return false;
    const bool removed = QFile::remove(path);
    reload();
    return removed;
}
