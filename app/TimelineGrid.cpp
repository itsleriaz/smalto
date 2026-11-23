#include "TimelineGrid.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

TimelineGrid::TimelineGrid(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &TimelineGrid::changed, this, &TimelineGrid::repaintAll);
}

void TimelineGrid::setModel(TimelineModel* model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        const auto repaint = [this] { repaintAll(); };
        connect(m_model, &QAbstractItemModel::modelReset, this, repaint);
        connect(m_model, &QAbstractItemModel::dataChanged, this, repaint);
        connect(m_model, &TimelineModel::editorChanged, this, &TimelineGrid::watchEditor);
    }
    watchEditor();
    emit changed();
}

// The current frame, layer, range and onion skin are drawn too.
void TimelineGrid::watchEditor()
{
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = m_model ? m_model->editor() : nullptr;
    if (!m_editor)
        return;
    const auto repaint = [this] { repaintAll(); };
    // A new current frame (every step of playback) only changes the columns around the old and the new
    // one: the highlight, and the onion skin's marks when it's on.
    connect(m_editor, &Editor::currentFrameChanged, this, [this] {
        const int from = m_shownFrame, to = m_editor->currentFrame();
        if (from < 0 || m_cellWidth <= 0 || m_editor->onionLoop()) {
            repaintAll();
            return;
        }
        if (m_fullPending) // a whole repaint is already asked for: a smaller one must not shrink it
            return;
        const int margin = m_editor->onionSkin() ? std::max(m_editor->onionBefore(), m_editor->onionAfter()) + 1 : 1;
        const qreal left = (std::min(from, to) - margin) * m_cellWidth - m_contentX;
        const qreal right = (std::max(from, to) + margin + 1) * m_cellWidth - m_contentX;
        update(QRect(int(std::floor(left)), 0, int(std::ceil(right - left)) + 1, int(std::ceil(height()))));
    });
    for (auto signal : {&Editor::currentLayerChanged, &Editor::rangeChanged,
                        &Editor::onionChanged, &Editor::layersChanged, &Editor::liveChanged})
        connect(m_editor, signal, this, repaint);
    repaintAll();
}

QColor TimelineGrid::rangeFill() const
{
    if (m_rangeActive)
        return m_accentSoft;
    QColor gray = m_textDim;
    gray.setAlphaF(0.16f);
    return gray;
}

QColor TimelineGrid::rangeOutline() const
{
    return m_rangeActive ? m_accent : m_textDim;
}

void TimelineGrid::repaintAll()
{
    m_fullPending = true;
    update();
}

void TimelineGrid::paint(QPainter* painter)
{
    m_fullPending = false;
    if (!m_model || !m_editor || m_cellWidth <= 0 || m_rowHeight <= 0)
        return;
    m_shownFrame = m_editor->currentFrame();
    const int columns = m_model->columnCount();
    if (columns == 0)
        return;
    const int first = std::max(0, int(std::floor(m_contentX / m_cellWidth)));
    const int last = std::min(columns - 1, int(std::floor((m_contentX + width()) / m_cellWidth)));
    if (m_mode == Cels)
        paintCels(*painter, first, last);
    else
        paintFrameNumbers(*painter, first, last);
}

// As the old per-cell delegates: background (selected / current row or
// frame / group), a column line, links to the neighbours, a dot for a cel or
// a ring for an empty frame, and an outline around the current cel.
void TimelineGrid::paintCels(QPainter& p, int firstColumn, int lastColumn)
{
    const int rows = m_model->rowCount();
    const int firstRow = std::max(0, int(std::floor(m_contentY / m_rowHeight)));
    const int lastRow = std::min(rows - 1, int(std::floor((m_contentY + height()) / m_rowHeight)));
    const int currentFrame = m_editor->currentFrame();
    const bool anyRange = m_editor->rangeType() != Editor::NoRange;
    const bool celRange = m_editor->rangeType() == Editor::CelRange;
    const QRect range = m_editor->range();
    const QColor fill = rangeFill();
    const QColor outline = rangeOutline();
    const auto inRange = [&](int row, int column) {
        if (row < 0 || row >= m_model->rowCount() || column < range.x() || column >= range.x() + range.width())
            return false;
        const int layer = m_model->layerForRow(row);
        return layer >= range.y() && layer < range.y() + range.height();
    };
    const QColor groupShade(255, 255, 255, int(0.025 * 255));
    const QColor columnLine(255, 255, 255, int(0.04 * 255));

    for (int row = firstRow; row <= lastRow; ++row) {
        const int layerIndex = m_model->layerForRow(row);
        const bool currentRow = row == m_currentRow;
        const bool shown = m_model->data(m_model->index(row, 0), TimelineModel::ShownRole).toBool();
        const qreal y = row * m_rowHeight - m_contentY;
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const int cel = m_model->data(m_model->index(row, column), TimelineModel::CelRole).toInt();
            const bool group = cel & TimelineModel::NoCels;
            const bool filled = cel & TimelineModel::HasCel;
            const bool isCurrentFrame = column == currentFrame;
            const bool selected = anyRange && column >= range.x() && column < range.x() + range.width()
                && layerIndex >= range.y() && layerIndex < range.y() + range.height();
            const QRectF r(column * m_cellWidth - m_contentX, y, m_cellWidth, m_rowHeight);

            if (selected)
                p.fillRect(r, fill);
            else if (isCurrentFrame || currentRow)
                p.fillRect(r, m_hover);
            else if (group)
                p.fillRect(r, groupShade);
            p.fillRect(QRectF(r.right() - 1, r.top(), 1, r.height()), columnLine);

            const qreal mid = r.center().y();
            if (cel & TimelineModel::LinkedToPrevious)
                p.fillRect(QRectF(r.left(), mid - 1, r.width() / 2, 2), m_textDim);
            if (cel & TimelineModel::LinkedToNext)
                p.fillRect(QRectF(r.center().x(), mid - 1, r.width() / 2, 2), m_textDim);

            if (!group) {
                if (filled) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(isCurrentFrame && currentRow ? m_accent : shown ? m_text : m_textFaint);
                    p.drawEllipse(r.center(), 5, 5);
                } else {
                    p.setPen(QPen(m_textFaint, 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(r.center(), 3, 3);
                }
            }
            if (isCurrentFrame && currentRow) {
                p.setPen(QPen(m_accent, 1));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), 4, 4);
            }
            // A block of cels (not whole frames): outlined around its edge, inside
            // the cells so the neighbours' fill can't cover it. The edge is also
            // where a drag moves the block (Timeline.qml).
            if (selected && celRange) {
                const qreal w = 2;
                if (!inRange(row, column - 1))
                    p.fillRect(QRectF(r.left(), r.top(), w, r.height()), outline);
                if (!inRange(row, column + 1))
                    p.fillRect(QRectF(r.right() - w, r.top(), w, r.height()), outline);
                if (!inRange(row - 1, column))
                    p.fillRect(QRectF(r.left(), r.top(), r.width(), w), outline);
                if (!inRange(row + 1, column))
                    p.fillRect(QRectF(r.left(), r.bottom() - w, r.width(), w), outline);
            }
        }
        paintLiveRange(p, row, y);
        paintSmearKeys(p, row, y);
    }
}

// A Smear group's keys: a diamond on each keyed frame, joined by a line from the first to the last (the
// frames it moves on).
void TimelineGrid::paintSmearKeys(QPainter& p, int row, qreal y)
{
    const QList<int> keys = m_model->smearKeys(row);
    if (keys.isEmpty())
        return;
    const qreal mid = y + m_rowHeight / 2;
    const auto centerX = [&](int frame) { return (frame + 0.5) * m_cellWidth - m_contentX; };
    QColor line = m_accent;
    line.setAlphaF(0.45f);
    p.setPen(QPen(line, 2));
    p.drawLine(QPointF(centerX(keys.front()), mid), QPointF(centerX(keys.back()), mid));
    const int current = m_editor->currentFrame();
    for (const int frame : keys) {
        const qreal cx = centerX(frame), r = 5.5;
        QPainterPath diamond;
        diamond.moveTo(cx, mid - r);
        diamond.lineTo(cx + r, mid);
        diamond.lineTo(cx, mid + r);
        diamond.lineTo(cx - r, mid);
        diamond.closeSubpath();
        p.setPen(QPen(frame == current ? m_text : m_accent, 1.5));
        p.setBrush(m_accent);
        p.drawPath(diamond);
    }
}

// A live layer's effect: a bar over the frames it emits on, a paler tail where the last particles
// are still alive. The ends are where it is dragged (see Timeline.qml).
void TimelineGrid::paintLiveRange(QPainter& p, int row, qreal y)
{
    const QPoint range = m_model->liveRange(row);
    if (range.x() < 0)
        return;
    const int frames = m_model->columnCount();
    const int tail = m_model->liveTail(row);
    const qreal h = m_rowHeight - 10;
    const qreal top = y + 5;
    QColor soft = m_accent;
    soft.setAlphaF(0.30f);
    QColor faint = m_accent;
    faint.setAlphaF(0.12f);
    const bool preRoll = range.x() < 0; // it began before the first frame: the bar starts at the edge
    const qreal x0 = std::max(0, range.x()) * m_cellWidth - m_contentX;
    const qreal x1 = (range.y() + 1) * m_cellWidth - m_contentX;
    if (tail > 0) {
        const int end = std::min(frames, range.y() + 1 + tail);
        if (end > range.y() + 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(faint);
            p.drawRoundedRect(QRectF(x1 - 4, top, (end - range.y() - 1) * m_cellWidth + 4, h), 4, 4);
        }
    }
    p.setPen(QPen(m_accent, 1.5));
    p.setBrush(soft);
    p.drawRoundedRect(QRectF(x0 + 1, top, x1 - x0 - 2, h), 4, 4);
    if (m_model->liveStream(row)) { // grips on the ends of a stream, however short
        p.setPen(QPen(m_accent, 2, Qt::SolidLine, Qt::RoundCap));
        if (!preRoll)
            p.drawLine(QPointF(x0 + 4, top + 4), QPointF(x0 + 4, top + h - 4));
        if (!m_model->liveOpenEnded(row))
            p.drawLine(QPointF(x1 - 4, top + 4), QPointF(x1 - 4, top + h - 4));
    }
}

// Frame numbers: the current frame as an accent pill, frames in the range
// softly highlighted, onion skin markers fading out under the number.
void TimelineGrid::paintFrameNumbers(QPainter& p, int firstColumn, int lastColumn)
{
    const int currentFrame = m_editor->currentFrame();
    // Only whole frames light up here; a block of cels is outlined in the grid instead, so
    // the two don't look alike.
    const bool frameRange = m_editor->rangeType() == Editor::FrameRange;
    const QRect range = m_editor->range();
    const bool onion = m_editor->onionSkin();
    const int before = m_editor->onionBefore();
    const int after = m_editor->onionAfter();

    QFont font = p.font();
    font.setPixelSize(m_fontPixelSize);
    font.setFeature(QFont::Tag("tnum"), 1);
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);

    // The selected frames as one bar, under the current frame's pill.
    if (frameRange) {
        p.setPen(Qt::NoPen);
        p.setBrush(rangeFill());
        p.drawRoundedRect(QRectF(range.x() * m_cellWidth - m_contentX + 1, 1, range.width() * m_cellWidth - 2, height() - 2),
                          4, 4);
    }
    for (int column = firstColumn; column <= lastColumn; ++column) {
        const bool current = column == currentFrame;
        const bool selected = frameRange && column >= range.x() && column < range.x() + range.width();
        const QRectF r(column * m_cellWidth - m_contentX, 0, m_cellWidth, height());

        const int d = column - currentFrame;
        if (onion && ((d < 0 && -d <= before) || (d > 0 && d <= after))) {
            QColor c = d < 0 ? QColor(0xff, 0x6b, 0x6b) : QColor(0x5c, 0x9d, 0xff);
            c.setAlphaF(float(1.0 - (std::abs(d) - 1) / qreal(std::max(1, d < 0 ? before : after))));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(r.left() + 3, r.bottom() - 2, r.width() - 6, 2), 1, 1);
        }
        if (current) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_accent);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        }
        p.setFont(current ? bold : font);
        p.setPen(current ? QColor(Qt::white) : selected ? m_text : m_textDim);
        p.drawText(r, Qt::AlignCenter, QString::number(column + 1));
    }
}
