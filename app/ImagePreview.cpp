#include "ImagePreview.h"

#include <QPainter>
#include <QQuickWindow>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

ImagePreview::ImagePreview(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    connect(this, &ImagePreview::changed, this, [this] { update(); });
    connect(this, &QQuickItem::widthChanged, this, &ImagePreview::updateZoom);
    connect(this, &QQuickItem::heightChanged, this, &ImagePreview::updateZoom);
}

void ImagePreview::setImage(const QImage& image)
{
    // A new size (trim, canvas size, another sprite): a pan kept from the old
    // one could leave the picture out of view, so start centered again.
    if (image.size() != m_image.size() && !m_pan.isNull()) {
        m_pan = {};
        emit panChanged();
    }
    m_image = image;
    updateZoom();
    emit imageChanged();
    update();
}

void ImagePreview::setCells(const QVariantList& cells)
{
    m_cellList = cells;
    m_cells.clear();
    for (const QVariant& v : cells)
        m_cells.append(v.toRectF());
    update();
}

void ImagePreview::setFixedZoom(qreal zoom)
{
    zoom = std::max(0.0, zoom);
    if (zoom == m_fixedZoom)
        return;
    m_fixedZoom = zoom;
    updateZoom();
    emit zoomChanged();
}

void ImagePreview::setPan(QPointF pan)
{
    if (pan == m_pan)
        return;
    m_pan = pan;
    emit panChanged();
    update();
}

void ImagePreview::zoomAt(qreal factor, QPointF pos)
{
    const qreal from = m_zoom; // Fit's zoom too: zooming starts from what's shown
    const qreal to = std::clamp(from * factor, kMinZoom, kMaxZoom);
    if (to == from && m_fixedZoom > 0)
        return;
    // The image's center sits at the item's center plus the pan (none at Fit).
    const QPointF center(width() / 2, height() / 2);
    const QPointF pan = m_fixedZoom > 0 ? m_pan : QPointF();
    const QPointF offset = pos - center - pan; // pointer from the image center
    setFixedZoom(to);
    setPan(pos - center - offset * (to / from));
}

void ImagePreview::wheelEvent(QWheelEvent* event)
{
    if (!m_wheelZooms || m_image.isNull()) {
        event->ignore();
        return;
    }
    if (!event->pixelDelta().isNull()) {
        // Trackpad (or a smooth-scrolling mouse): many small steps per swipe,
        // so each zooms a little, as on the canvas.
        zoomAt(std::pow(1.01, event->pixelDelta().y()), event->position());
    } else {
        // A notched wheel: a step per notch (120). The start and end of a
        // trackpad gesture carry no delta at all, and do nothing.
        m_wheelAccum += event->angleDelta().y();
        while (std::abs(m_wheelAccum) >= 120) {
            const int dir = m_wheelAccum > 0 ? 1 : -1;
            zoomAt(dir > 0 ? 1.25 : 1 / 1.25, event->position());
            m_wheelAccum -= dir * 120;
        }
    }
    event->accept();
}

void ImagePreview::updateZoom()
{
    qreal zoom = 1;
    if (m_fixedZoom > 0) {
        zoom = m_fixedZoom;
    } else if (!m_image.isNull() && width() > 0 && height() > 0) {
        zoom = std::min(width() / m_image.width(), height() / m_image.height());
        if (zoom >= 1)
            zoom = std::floor(zoom); // whole steps keep pixels square
    }
    if (zoom != m_zoom) {
        m_zoom = zoom;
        emit zoomChanged();
    }
    update();
}

void ImagePreview::paint(QPainter* painter)
{
    if (m_image.isNull())
        return;
    const QSizeF size(m_image.width() * m_zoom, m_image.height() * m_zoom);
    // Whole device pixels, so scaled-up pixels all come out the same size.
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
    auto snap = [dpr](qreal v) { return std::round(v * dpr) / dpr; };
    const QPointF pan = m_fixedZoom > 0 ? m_pan : QPointF();
    const QRectF target(snap((width() - size.width()) / 2 + pan.x()), snap((height() - size.height()) / 2 + pan.y()),
                        size.width(), size.height());

    // Only the cells in view: zoomed in, the image can be far bigger.
    const QRectF visible = target.intersected(QRectF(0, 0, width(), height()));
    if (visible.isEmpty())
        return;
    painter->save();
    painter->setClipRect(visible);
    const int cell = std::max(1, m_checkerSize);
    const int x0 = int((visible.left() - target.left()) / cell), x1 = int((visible.right() - target.left()) / cell);
    const int y0 = int((visible.top() - target.top()) / cell), y1 = int((visible.bottom() - target.top()) / cell);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            painter->fillRect(QRectF(target.x() + x * cell, target.y() + y * cell, cell, cell),
                              (x + y) % 2 ? m_checkerDark : m_checkerLight);
    painter->restore();

    painter->setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1);
    painter->drawImage(target, m_image);

    if (!m_cells.isEmpty() && m_cells.first().width() * m_zoom >= 5) {
        painter->save();
        painter->setClipRect(visible);
        QPen pen(m_cellColor, 1, Qt::DashLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        for (const QRectF& r : m_cells) {
            const QRectF cell(target.x() + r.x() * m_zoom, target.y() + r.y() * m_zoom, r.width() * m_zoom,
                              r.height() * m_zoom);
            if (cell.intersects(visible))
                painter->drawRect(cell);
        }
        painter->restore();
    }
}
