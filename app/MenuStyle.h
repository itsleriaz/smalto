#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// Styling the platform's own menus can't get from QML. On macOS the menu bar
// is native, so destructive entries are colored red here; elsewhere it does
// nothing (the in-window menus color their own).
class MenuStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit MenuStyle(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void markDestructive(const QStringList& titles);
};
