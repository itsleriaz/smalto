#pragma once

#include "Editor.h"

#include <QAbstractTableModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include <vector>

// The timeline grid: one row per layer row of the editor (topmost layer
// first, as the timeline shows them), one column per frame. Each cell says
// whether there is a cel and how it is linked; each row also carries the
// layer's own data for the layer column.
class TimelineModel : public QAbstractTableModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Editor* editor READ editor WRITE setEditor NOTIFY editorChanged)

public:
    enum CelFlag {
        HasCel = 1,
        Linked = 2,           // shares its pixels with another frame
        LinkedToPrevious = 4, // ... with the frame just before
        LinkedToNext = 8,     // ... with the frame just after
        NoCels = 16,          // group layer
    };
    Q_ENUM(CelFlag)

    enum Role {
        CelRole = Qt::UserRole + 1,
        LayerIndexRole, // index into Editor::layers()
        NameRole,
        KindRole,
        DepthRole,
        LayerVisibleRole,
        ShownRole, // visible including enclosing groups
        LockedRole,
        AlphaLockRole,
        CollapsedRole,
        OpacityRole, // percent
        BlendRole,   // blend mode name
    };

    explicit TimelineModel(QObject* parent = nullptr);

    Editor* editor() const { return m_editor; }
    void setEditor(Editor* editor);

    // Row of the timeline for an index into Editor::layers(), and back.
    Q_INVOKABLE int rowForLayer(int layerIndex) const;
    Q_INVOKABLE int layerForRow(int row) const;
    // The frames a live layer's effect starts and ends on (a burst: both the same), or (-1, -1) for
    // any other row; and how many frames its last particles live on after the end (none when looping).
    Q_INVOKABLE QPoint liveRange(int row) const;
    Q_INVOKABLE int liveTail(int row) const;
    // Whether the live layer on this row emits over a range (a stream, even a one frame long one) rather
    // than all at once (a burst): only a stream has ends to drag.
    Q_INVOKABLE bool liveStream(int row) const;
    // A stream that runs to the last frame: its end is not a grip.
    Q_INVOKABLE bool liveOpenEnded(int row) const;
    // The frames a Smear group on this row has keys on (none for any other row).
    Q_INVOKABLE QList<int> smearKeys(int row) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void editorChanged();

private:
    void rebuild();
    const core::Layer* layerAt(int row) const;

    QPointer<Editor> m_editor;
    std::vector<std::vector<int>> m_flags; // [row][frame]
    int m_rows = 0;
    int m_columns = 0;
};
