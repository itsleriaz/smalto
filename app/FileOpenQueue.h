#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

// Files the OS (Finder "Open With", Dock drops) or the command line asked us
// to open. Collects them from launch on, before QML exists; QML takes them
// with take() at startup and whenever filesQueued fires.
class FileOpenQueue : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    // main() owns the one instance; QML gets that same object.
    // No default constructor, so QML uses create() instead of making its own.
    explicit FileOpenQueue(QObject* parent);
    ~FileOpenQueue() override;
    static FileOpenQueue* create(QQmlEngine*, QJSEngine*);

    void push(const QString& path);
    Q_INVOKABLE QStringList take();

signals:
    void filesQueued();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QStringList m_paths;
};
