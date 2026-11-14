#include "BrushLibrary.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantMap>

BrushLibrary::BrushLibrary(QObject* parent)
    : BrushLibrary(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/brushes", parent)
{
}

BrushLibrary::BrushLibrary(const QString& folder, QObject* parent)
    : QObject(parent)
    , m_folder(folder)
{
    reload();
}

void BrushLibrary::reload()
{
    m_brushes.clear();
    m_images.clear();
    const QDir dir(m_folder);
    // Names start with the time they were saved, so name order is age order.
    for (const QString& name : dir.entryList({"*.png"}, QDir::Files, QDir::Name)) {
        const QString path = dir.filePath(name);
        const QImage image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            continue;
        m_brushes.append(QVariantMap{{"path", path}, {"width", image.width()}, {"height", image.height()}});
        m_images.append(image);
    }
    emit brushesChanged();
}

bool BrushLibrary::contains(const QImage& image) const
{
    return m_images.contains(image.convertToFormat(QImage::Format_RGBA8888));
}

QImage BrushLibrary::image(const QString& path) const
{
    for (qsizetype i = 0; i < m_brushes.size(); ++i)
        if (m_brushes[i].toMap().value("path").toString() == path)
            return m_images[i];
    return {};
}

bool BrushLibrary::add(const QImage& image)
{
    if (image.isNull() || contains(image) || !QDir().mkpath(m_folder))
        return false;
    QString base = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
    QString path = QDir(m_folder).filePath(base + ".png");
    for (int n = 2; QFile::exists(path); ++n)
        path = QDir(m_folder).filePath(base + "-" + QString::number(n) + ".png");
    if (!image.convertToFormat(QImage::Format_RGBA8888).save(path, "PNG"))
        return false;
    reload();
    return true;
}

bool BrushLibrary::remove(const QString& path)
{
    if (QFileInfo(path).absolutePath() != QDir(m_folder).absolutePath() || !QFile::remove(path))
        return false;
    reload();
    return true;
}
