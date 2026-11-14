#pragma once

#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Shows an image fitted into the item over a checkerboard: scaled up by
// whole steps with hard pixel edges, or down smoothly when it doesn't fit.
// Used for the export preview. With `fixedZoom` > 0 it's shown at that zoom
// instead, centered and moved by `pan` (screen pixels; the preview panel).
class ImagePreview : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
    // Zoom the image is shown at (1 = actual pixels), for a label.
    Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal fixedZoom READ fixedZoom WRITE setFixedZoom NOTIFY zoomChanged)
    Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY panChanged)
    // Outlines drawn over the image: rects (QRectF values, in image pixels).
    // Left out when the cells would be only a few screen pixels wide.
    Q_PROPERTY(QVariantList cells READ cells WRITE setCells NOTIFY changed)
    Q_PROPERTY(QColor cellColor MEMBER m_cellColor NOTIFY changed)
    Q_PROPERTY(QColor checkerLight MEMBER m_checkerLight NOTIFY changed)
    Q_PROPERTY(QColor checkerDark MEMBER m_checkerDark NOTIFY changed)
    Q_PROPERTY(int checkerSize MEMBER m_checkerSize NOTIFY changed)

public:
    explicit ImagePreview(QQuickItem* parent = nullptr);

    QImage image() const { return m_image; }
    void setImage(const QImage& image);
    qreal zoom() const { return m_zoom; }
    qreal fixedZoom() const { return m_fixedZoom; }
    void setFixedZoom(qreal zoom);
    QPointF pan() const { return m_pan; }
    void setPan(QPointF pan);

    QVariantList cells() const { return m_cellList; }
    void setCells(const QVariantList& cells);

    void paint(QPainter* painter) override;

signals:
    void imageChanged();
    void zoomChanged();
    void panChanged();
    void changed();

private:
    void updateZoom();

    QImage m_image;
    qreal m_zoom = 1;
    qreal m_fixedZoom = 0;
    QPointF m_pan;
    QVariantList m_cellList;
    QList<QRectF> m_cells;
    QColor m_cellColor{0xff, 0xff, 0xff, 0x90};
    QColor m_checkerLight{0x3a, 0x3a, 0x40};
    QColor m_checkerDark{0x2c, 0x2c, 0x32};
    int m_checkerSize = 8;
};
