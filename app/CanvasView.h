#pragma once

#include "Editor.h"

#include <QColor>
#include <QPointer>
#include <QHash>
#include <QQuickRhiItem>
#include <QTimer>

// GPU canvas: shows the editor's composite with pan/zoom, checkerboard and
// pixel grid, and turns pointer input into strokes.
class CanvasView : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Editor* editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    // Canvas top-left in item coordinates; with zoom, maps canvas pixels to the screen.
    Q_PROPERTY(QPointF offset READ offset NOTIFY viewChanged)
    // The part of the item that isn't under docked panels, in item coordinates
    // (empty: all of it). Fitting, centering and the zoom buttons use it; the
    // canvas is still drawn across the whole item.
    Q_PROPERTY(QRectF viewArea READ viewArea WRITE setViewArea NOTIFY viewAreaChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)
    // The document's own grid (Editor::grid), drawn over the canvas.
    Q_PROPERTY(bool showCustomGrid READ showCustomGrid WRITE setShowCustomGrid NOTIFY showCustomGridChanged)
    // The slices overlay (SliceOverlay.qml) shows.
    Q_PROPERTY(bool showSlices READ showSlices WRITE setShowSlices NOTIFY showSlicesChanged)
    // Checkerboard behind transparent pixels: a color pair (0 dark, 1 light, 2 gray)
    // and the cell size in canvas pixels (0: about 12 screen pixels at any zoom).
    Q_PROPERTY(int checkerStyle READ checkerStyle WRITE setCheckerStyle NOTIFY checkerChanged)
    Q_PROPERTY(int checkerSize READ checkerSize WRITE setCheckerSize NOTIFY checkerChanged)
    // The two checkerboard colors of checkerStyle (for previews that match).
    Q_PROPERTY(QColor checkerA READ checkerA NOTIFY checkerChanged)
    Q_PROPERTY(QColor checkerB READ checkerB NOTIFY checkerChanged)
    // Use the theme's pair instead of checkerStyle's (the theme sets it, see Main.qml).
    Q_PROPERTY(bool checkerFollowsTheme READ checkerFollowsTheme WRITE setCheckerFollowsTheme NOTIFY checkerChanged)
    // Custom pair for checkerStyle 3, and a checkerboard that stays put on the
    // screen (fixed-size cells, not moving with the picture) instead of on the canvas.
    Q_PROPERTY(QColor customCheckerA READ customCheckerA WRITE setCustomCheckerA NOTIFY checkerChanged)
    Q_PROPERTY(QColor customCheckerB READ customCheckerB WRITE setCustomCheckerB NOTIFY checkerChanged)
    Q_PROPERTY(bool checkerFixed READ checkerFixed WRITE setCheckerFixed NOTIFY checkerChanged)
    // The pixel grid's color (its alpha is the strength), and the document grid's
    // (transparent: black or white, by what's beneath).
    // Tiled view: the copies around the sprite fade a little, so the original stays clear.
    Q_PROPERTY(bool dimTiledCopies READ dimTiledCopies WRITE setDimTiledCopies NOTIFY gridColorsChanged)
    // Grid handles on the canvas (GridHandles.qml): drag the grid's corner to move
    // it and the next one to size its cells. Not remembered.
    Q_PROPERTY(bool editGrid READ editGrid WRITE setEditGrid NOTIFY editGridChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorsChanged)
    Q_PROPERTY(QColor documentGridColor READ documentGridColor WRITE setDocumentGridColor NOTIFY gridColorsChanged)
    Q_PROPERTY(QColor themeCheckerA READ themeCheckerA WRITE setThemeCheckerA NOTIFY checkerChanged)
    Q_PROPERTY(QColor themeCheckerB READ themeCheckerB WRITE setThemeCheckerB NOTIFY checkerChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    // The 1 px line around the canvas (its alpha blends it over the background).
    // Scrolling zooms (Cmd / Ctrl + scroll pans) instead of panning (Cmd /
    // Ctrl + scroll zooms). A notched mouse wheel zooms either way.
    Q_PROPERTY(bool scrollZooms READ scrollZooms WRITE setScrollZooms NOTIFY scrollZoomsChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QPoint hoverPixel READ hoverPixel NOTIFY hoverChanged)
    Q_PROPERTY(bool hovering READ hovering NOTIFY hoverChanged)
    // The pointer in item coordinates (while hovering).
    Q_PROPERTY(QPointF hoverPosition READ hoverPosition NOTIFY hoverChanged)
    // Cmd / Ctrl held over the canvas: a click would pick the layer named
    // by layerPickName ("" when there's none under the pointer).
    Q_PROPERTY(bool layerPicking READ layerPicking NOTIFY layerPickChanged)
    Q_PROPERTY(QString layerPickName READ layerPickName NOTIFY layerPickChanged)
    // Ctrl+Alt (Ctrl+Option on a Mac) and a sideways drag: the brush size (pencil, eraser, blur, jumble).
    Q_PROPERTY(bool resizingBrush READ resizingBrush NOTIFY resizingBrushChanged)
    Q_PROPERTY(bool turningBrush READ turningBrush NOTIFY resizingBrushChanged)

public:
    explicit CanvasView(QQuickItem* parent = nullptr);

    Editor* editor() const { return m_editor; }
    void setEditor(Editor* editor);

    qreal zoom() const { return m_zoom; }
    QPointF offset() const { return m_offset; }
    QRectF viewArea() const { return m_viewArea; }
    void setViewArea(const QRectF& area);
    // The free rectangle: `viewArea`, or the whole item.
    QRectF freeArea() const { return m_viewArea.isEmpty() ? QRectF(0, 0, width(), height()) : m_viewArea; }
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool show);
    bool showCustomGrid() const { return m_showCustomGrid; }
    void setShowCustomGrid(bool show);
    bool showSlices() const { return m_showSlices; }
    void setShowSlices(bool show);
    int checkerStyle() const { return m_checkerStyle; }
    void setCheckerStyle(int style);
    QColor customCheckerA() const { return m_customCheckerA; }
    QColor customCheckerB() const { return m_customCheckerB; }
    void setCustomCheckerA(const QColor& c);
    void setCustomCheckerB(const QColor& c);
    bool checkerFixed() const { return m_checkerFixed; }
    void setCheckerFixed(bool fixed);
    bool editGrid() const { return m_editGrid; }
    void setEditGrid(bool on)
    {
        if (m_editGrid == on)
            return;
        m_editGrid = on;
        emit editGridChanged();
    }
    bool dimTiledCopies() const { return m_dimTiledCopies; }
    void setDimTiledCopies(bool dim);
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor& c);
    QColor documentGridColor() const { return m_documentGridColor; }
    void setDocumentGridColor(const QColor& c);
    int checkerSize() const { return m_checkerSize; }
    QColor checkerA() const;
    QColor checkerB() const;
    void setCheckerSize(int size);
    bool checkerFollowsTheme() const { return m_checkerThemed; }
    void setCheckerFollowsTheme(bool follows);
    // Preferences > Canvas back to their defaults (checkerboard, grid colors,
    // tiled copies, scrolling).
    Q_INVOKABLE void resetPreferences();
    QColor themeCheckerA() const { return m_themeCheckerA; }
    QColor themeCheckerB() const { return m_themeCheckerB; }
    void setThemeCheckerA(const QColor& c);
    void setThemeCheckerB(const QColor& c);
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor& c);
    QColor borderColor() const { return m_border; }
    bool scrollZooms() const { return m_scrollZooms; }
    void setScrollZooms(bool zooms);
    void setBorderColor(const QColor& c);
    // The sprite pixel under the pointer (in the tiled view, on whichever
    // copy it's over).
    QPoint hoverPixel() const { return m_editor ? m_editor->wrapPixel(m_hover) : m_hover; }
    bool hovering() const { return m_hovering; }
    QPointF hoverPosition() const { return m_hoverPos; }
    bool layerPicking() const { return m_layerPicking; }
    QString layerPickName() const { return m_layerPickName; }
    bool resizingBrush() const { return m_sizing; }
    bool turningBrush() const { return m_sizing && m_turning; }
    // True when the brush preview should be drawn under the cursor.
    bool showsBrushPreview() const;
    // Animation step of the selection's marching ants.
    int antsPhase() const;

    Q_INVOKABLE void zoomIn();
    // Sets the zoom (1 = 100%) at once, keeping the view's center in place.
    Q_INVOKABLE void zoomTo(qreal zoom);
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void fitToView();
    // Continuous zoom (pinch), keeping `center` fixed on screen.
    Q_INVOKABLE void zoomBy(qreal factor, QPointF center);

signals:
    void editorChanged();
    void viewChanged();
    void viewAreaChanged();
    void showGridChanged();
    void showCustomGridChanged();
    void checkerChanged();
    void gridColorsChanged();
    void editGridChanged();
    void showSlicesChanged();
    void backgroundColorChanged();
    void scrollZoomsChanged();
    void hoverChanged();
    void layerPickChanged();
    void resizingBrushChanged();
    // Any mouse press on the canvas (e.g. to route Ctrl+C to pixels).
    void pressed();
    // While an effect is previewed the canvas doesn't draw: a left click
    // reports the pixel instead (the effect dialog picks colors with it).
    void effectPicked(QPoint pixel);

protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    // Each document's zoom and pan, kept while switching tabs.
    struct View {
        qreal zoom = 1;
        QPointF offset;
        QSize canvas; // the canvas size it was for
    };
    QHash<QString, View> m_views;
    QString m_viewDocument; // whose view is showing
    QSize m_viewCanvas;
    void documentOrSizeChanged();
    QPoint toPixel(QPointF pos) const;
    void setHover(QPointF pos);
    void stepZoom(int direction, QPointF anchor);
    void animateZoomTo(qreal zoom, QPointF anchor);
    void applyZoom(qreal zoom, QPointF anchor);
    void animationTick();
    void updateCursor();
    // The modifiers held (from key and hover events), for the cursor and
    // the layer pick label.
    void setModifiers(Qt::KeyboardModifiers modifiers);
    void updateLayerPick();

    QPointer<Editor> m_editor;
    QPointF m_offset;  // canvas top-left in item coordinates
    qreal m_zoom = 8;  // screen pixels per canvas pixel
    qreal m_targetZoom = 8;
    QPointF m_zoomAnchor;
    QTimer m_zoomTimer;

    bool m_showGrid = true;
    bool m_showCustomGrid = false;
    // Preferences > Canvas defaults (resetPreferences goes back to them).
    static constexpr int kCheckerStyle = 0;
    static constexpr int kCheckerSize = 0;
    static inline const QColor kCustomCheckerA{0x48, 0x48, 0x4e};
    static inline const QColor kCustomCheckerB{0x3a, 0x3a, 0x40};
    static constexpr bool kCheckerFixed = false;
    static constexpr bool kDimTiledCopies = true;
    static inline const QColor kGridColor{0, 0, 0, 71};
    static inline const QColor kDocumentGridColor{0, 0, 0, 0}; // transparent: automatic
    static constexpr bool kCheckerThemed = false;
    static constexpr bool kScrollZooms = false;

    int m_checkerStyle = kCheckerStyle;
    int m_checkerSize = kCheckerSize;
    QColor m_customCheckerA = kCustomCheckerA;
    QColor m_customCheckerB = kCustomCheckerB;
    bool m_checkerFixed = kCheckerFixed;
    bool m_dimTiledCopies = kDimTiledCopies;
    bool m_editGrid = false;
    QColor m_gridColor = kGridColor;
    QColor m_documentGridColor = kDocumentGridColor;
    bool m_checkerThemed = kCheckerThemed;
    QColor m_themeCheckerA{0x48, 0x48, 0x4e};
    QColor m_themeCheckerB{0x3a, 0x3a, 0x40};
    bool m_showSlices = true;
    QColor m_background{0x1a, 0x1a, 0x1e};
    QColor m_border{255, 255, 255, 30};
    bool m_scrollZooms = kScrollZooms;
    QPoint m_hover;
    QPointF m_hoverPos;
    bool m_hovering = false;
    Qt::KeyboardModifiers m_modifiers;
    bool m_layerPicking = false;
    QString m_layerPickName;
    // The brush size drag: where it began and the size then.
    bool brushResizeGesture(Qt::KeyboardModifiers modifiers) const;
    bool brushTurnGesture(Qt::KeyboardModifiers modifiers) const;
    bool m_sizing = false;
    bool m_turning = false; // the drag turns the brush instead of sizing it
    bool m_scaling = false; // the drag scales the custom brush instead of sizing it
    qreal m_sizeStartX = 0;
    int m_sizeStartValue = 1;

    bool m_panning = false;
    bool m_spaceHeld = false;
    bool m_needsFit = true;
    QRectF m_viewArea;
    QPointF m_lastPos;
    int m_wheelAccum = 0;
    QTimer m_antsTimer;
    int m_antsPhase = 0;
};
