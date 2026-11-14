#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The user manual (manual/, built with mdBook into plain HTML). The packages
// carry a copy; Help > Smalto Manual opens it in the default browser.
class Manual : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Manual(QObject* parent = nullptr) : QObject(parent) {}

    // The folder holding index.html, or an empty string when this build has no manual.
    Q_INVOKABLE static QString folder();
    // Opens the manual, at `page` (e.g. "reference/shortcuts.html", optionally
    // with "#anchor"; empty for the front page). False when there is none.
    Q_INVOKABLE bool open(const QString& page);
};
