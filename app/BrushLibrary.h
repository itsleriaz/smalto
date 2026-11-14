#pragma once

#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// The user's saved custom brushes: RGBA PNGs in the app data folder
// ("brushes/"), oldest first, so a brush keeps its place. Make one current
// with Editor.setCustomBrush(image).
class BrushLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // {path, width, height} maps; the pixels come from image(path) (a QImage
    // inside a map reaches QML as undefined).
    Q_PROPERTY(QVariantList brushes READ brushes NOTIFY brushesChanged)

public:
    explicit BrushLibrary(QObject* parent = nullptr);
    // Keeps the brushes in `folder` instead of the app data folder (tests).
    explicit BrushLibrary(const QString& folder, QObject* parent = nullptr);

    QVariantList brushes() const { return m_brushes; }

    // Saves `image` as a new brush (the folder is created); false if it's
    // empty or the same pixels are saved already.
    Q_INVOKABLE bool add(const QImage& image);
    Q_INVOKABLE bool contains(const QImage& image) const;
    // The brush saved at `path` (null if it isn't one of them).
    Q_INVOKABLE QImage image(const QString& path) const;
    Q_INVOKABLE bool remove(const QString& path);
    Q_INVOKABLE void reload();

signals:
    void brushesChanged();

private:
    QString m_folder;
    QVariantList m_brushes;
    QList<QImage> m_images; // same order as m_brushes
};
