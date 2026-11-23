#pragma once

#include "Editor.h"
#include "TimelineModel.h"

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Paints the visible part of the timeline's cel grid or its frame-number
// row, straight from the TimelineModel, instead of a QML item per cell, so
// switching documents or scrolling costs the same for 1 frame or 9999. It
// sits behind a TableView that only provides scrolling and input, and
// follows that view's contentX / contentY.
class TimelineGrid : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(TimelineModel* model READ model WRITE setModel NOTIFY changed)
    Q_PROPERTY(Mode mode MEMBER m_mode NOTIFY changed)
    Q_PROPERTY(qreal contentX MEMBER m_contentX NOTIFY changed)
    Q_PROPERTY(qreal contentY MEMBER m_contentY NOTIFY changed)
    Q_PROPERTY(qreal cellWidth MEMBER m_cellWidth NOTIFY changed)
    Q_PROPERTY(qreal rowHeight MEMBER m_rowHeight NOTIFY changed)
    Q_PROPERTY(int currentRow MEMBER m_currentRow NOTIFY changed)
    // Whether the timeline was clicked last (what Delete, Copy... act on): its
    // selection in the accent color, else in gray.
    Q_PROPERTY(bool rangeActive MEMBER m_rangeActive NOTIFY changed)
    // Theme colors and font size, set from Theme in QML.
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY changed)
    Q_PROPERTY(QColor accentSoft MEMBER m_accentSoft NOTIFY changed)
    Q_PROPERTY(QColor hover MEMBER m_hover NOTIFY changed)
    Q_PROPERTY(QColor text MEMBER m_text NOTIFY changed)
    Q_PROPERTY(QColor textDim MEMBER m_textDim NOTIFY changed)
    Q_PROPERTY(QColor textFaint MEMBER m_textFaint NOTIFY changed)
    Q_PROPERTY(int fontPixelSize MEMBER m_fontPixelSize NOTIFY changed)

public:
    enum Mode { Cels, FrameNumbers };
    Q_ENUM(Mode)

    explicit TimelineGrid(QQuickItem* parent = nullptr);

    TimelineModel* model() const { return m_model; }
    void setModel(TimelineModel* model);

    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    void paintCels(QPainter& p, int firstColumn, int lastColumn);
    void paintFrameNumbers(QPainter& p, int firstColumn, int lastColumn);
    void paintLiveRange(QPainter& p, int row, qreal y);
    void paintSmearKeys(QPainter& p, int row, qreal y);
    QColor rangeFill() const;    // the selection's fill, accent or gray (rangeActive)
    QColor rangeOutline() const; // a cel block's outline
    void watchEditor();
    void repaintAll();
    bool m_fullPending = true; // a whole repaint is asked for and not yet done

    QPointer<TimelineModel> m_model;
    QPointer<Editor> m_editor;
    QMetaObject::Connection m_editorWatch;
    Mode m_mode = Cels;
    qreal m_contentX = 0;
    qreal m_contentY = 0;
    qreal m_cellWidth = 24;
    qreal m_rowHeight = 24;
    int m_currentRow = -1;
    bool m_rangeActive = true;
    int m_shownFrame = -1; // the current frame as last painted
    QColor m_accent, m_accentSoft, m_hover, m_text, m_textDim, m_textFaint;
    int m_fontPixelSize = 10;
};
