#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

// Keyboard shortcuts that can be rebound in Preferences. Each action has an
// id (used by the Shortcut items in Main.qml), a group and name for the
// list, and default keys; changes are kept in QSettings ("shortcuts/<id>").
// Keys are QKeySequence portable text ("Ctrl+S": Command on macOS). Keys
// whose meaning depends on what's going on (Esc, Enter, plain Delete,
// Backspace and Space, arrows with Shift / Alt) stay fixed and can't be
// assigned.
class Shortcuts : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // id -> its keys (portable text), for Shortcut.sequences.
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY changed)
    // id -> its first key as shown on this platform ("⌘S"), or "": for menus.
    Q_PROPERTY(QVariantMap labels READ labels NOTIFY changed)
    // id -> " (B)" with the first key as shown on this platform, or "" when
    // the action has no key: for tooltips ("Pencil" + hints.pencil).
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY changed)
    // {id, group, name, keys (shown text), custom} in list order.
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(bool customized READ customized NOTIFY changed)

public:
    struct Action {
        QString id;
        QString group;
        QString name;
        QStringList defaults;
    };
    static const std::vector<Action>& table();

    explicit Shortcuts(QObject* parent = nullptr);
    // Uses an INI file instead of the app's settings (tests).
    explicit Shortcuts(const QString& settingsFile, QObject* parent = nullptr);

    QVariantMap keys() const;
    QVariantMap labels() const;
    QVariantMap hints() const;
    QVariantList actions() const;
    bool customized() const;

    Q_INVOKABLE QStringList keysFor(const QString& id) const;
    // Adds `key` to the action, taking it from any other action that had it.
    // Returns the name of that other action ("" if none). Reserved or
    // invalid keys are refused (returns "" and changes nothing).
    Q_INVOKABLE QString assign(const QString& id, const QString& key);
    Q_INVOKABLE void unassign(const QString& id, const QString& key);
    Q_INVOKABLE void reset(const QString& id);
    Q_INVOKABLE void resetAll();
    // The action using `key` ("" if none), and its name.
    Q_INVOKABLE QString owner(const QString& key) const;
    Q_INVOKABLE QString nameOf(const QString& id) const;
    Q_INVOKABLE static bool isReserved(const QString& key);

    // A key press as portable text: "" for a lone modifier. Shift is dropped
    // from symbols it was needed to type ("+", not "Shift++").
    Q_INVOKABLE static QString fromKeyEvent(int key, int modifiers);
    // As shown to the user on this platform ("⌘S" on macOS).
    Q_INVOKABLE static QString display(const QString& key);

signals:
    void changed();

private:
    const Action* find(const QString& id) const;
    void store(const QString& id, const QStringList& keys);
    static QString normalized(const QString& key);

    QString m_settingsFile;
    QVariantMap m_keys; // current keys of every action
};
