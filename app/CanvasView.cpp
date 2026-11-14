#include "CanvasView.h"

#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QQuickWindow>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {

constexpr std::array kZoomLevels{0.25, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 16.0,
                                 20.0, 24.0, 32.0, 40.0, 48.0, 64.0, 80.0, 96.0, 128.0};
constexpr qreal kMinZoom = kZoomLevels.front();
constexpr qreal kMaxZoom = kZoomLevels.back();

// Checkerboard colors by checkerStyle: dark, light, gray.
constexpr QRgb kCheckers[][2] = {{0x48484e, 0x3a3a40}, {0xe6e6e6, 0xc4c4c4}, {0x8c8c8c, 0x727272}};
// (style 3 is the custom pair)

// Must match the uniform block in canvas.vert / canvas.frag (std140, vec4s only).
struct Uniforms {
    float viewport[4];  // xy: target size (px), zw: sprite size
    float xform[4];     // xy: offset (px), z: zoom (px per canvas px), w: NDC y sign
    float background[4];
    float checkA[4];
    float checkB[4];
    float grid[4];      // rgb + alpha
    float hoverColor[4];
    float hoverRect[4]; // x, y, w, h in canvas pixels
    float params[4];    // x: checker size (canvas px), y: grid strength, z: ants phase, w: dpr
    float selection[4]; // x, y, w, h in canvas pixels; w = 0: none
    float symmetry[4]; // xy: where the axes cross (canvas pixels); z: axes shown, bits 1 vertical | 2 horizontal | 4 rising | 8 falling diagonal
    float customGrid[4]; // x, y offset and cell w, h in canvas pixels; w = 0: off
    float brushStyle[4]; // x: 1 = the brush texture shows its own colors (at opacity y)
    float border[4];     // the line around the canvas; alpha blends it
    float tiling[4];     // x / y: 1 = the sprite repeats across that axis; z: 1 = the checkerboard is fixed on screen, w: its cell (px)
    float docGrid[4];    // the document grid's color; alpha 0: automatic
    float tileDim[4];    // x: how far the tiled view's copies fade toward the background; y: 1 = the onion skin is drawn over the sprite
};

void setColor(float* out, const QColor& c, float alpha = -1)
{
    out[0] = float(c.redF());
    out[1] = float(c.greenF());
    out[2] = float(c.blueF());
    out[3] = alpha < 0 ? float(c.alphaF()) : alpha;
}

QShader loadShader(const QString& name)
{
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Failed to load shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

class CanvasRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    QRhiShaderResourceBindings* makeBindings(QRhiTexture* texture, QRhiTexture* overlay, QRhiTexture* onion,
                                             QRhiTexture* selection, QRhiTexture* brush);

    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_dummyTexture;
    std::unique_ptr<QRhiShaderResourceBindings> m_layoutSrb; // used to build the pipeline
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiTexture> m_overlayTexture;
    std::unique_ptr<QRhiTexture> m_onionTexture;
    std::unique_ptr<QRhiTexture> m_selectionTexture;
    std::unique_ptr<QRhiTexture> m_brushTexture; // the brush footprint, for the preview
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QRhiResourceUpdateBatch* m_initialUpdates = nullptr;

    QSize m_textureSize;
    QSize m_wantedSize;
    QImage m_upload;
    QPoint m_uploadPos;
    QImage m_overlayUpload;
    QPoint m_overlayUploadPos;
    QImage m_onionUpload;
    QPoint m_onionUploadPos;
    QImage m_selectionUpload;
    QPoint m_selectionUploadPos;
    QImage m_brushUpload;   // a new footprint to upload
    qint64 m_brushKey = -1; // size and shape of the uploaded one
    Uniforms m_uniforms{};
};

QRhiShaderResourceBindings* CanvasRenderer::makeBindings(QRhiTexture* texture, QRhiTexture* overlay,
                                                         QRhiTexture* onion, QRhiTexture* selection,
                                                         QRhiTexture* brush)
{
    QRhiShaderResourceBindings* srb = m_rhi->newShaderResourceBindings();
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture,
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, overlay,
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, onion,
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage, selection,
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(5, QRhiShaderResourceBinding::FragmentStage, brush,
                                                  m_sampler.get()),
    });
    srb->create();
    return srb;
}

void CanvasRenderer::initialize(QRhiCommandBuffer*)
{
    if (m_rhi != rhi()) {
        m_pipeline.reset();
        m_srb.reset();
        m_texture.reset();
        m_overlayTexture.reset();
        m_onionTexture.reset();
        m_selectionTexture.reset();
        m_brushTexture.reset();
        m_brushKey = -1;
        m_layoutSrb.reset();
        m_dummyTexture.reset();
        m_sampler.reset();
        m_ubuf.reset();
        m_vbuf.reset();
        m_textureSize = {};
        m_rhi = rhi();
    }
    if (m_pipeline)
        return;

    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(quad)));
    m_vbuf->create();
    m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    m_ubuf->create();
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    m_dummyTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_dummyTexture->create();
    m_layoutSrb.reset(makeBindings(m_dummyTexture.get(), m_dummyTexture.get(), m_dummyTexture.get(),
                                   m_dummyTexture.get(), m_dummyTexture.get()));

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/canvas.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/canvas.frag.qsb"))},
    });
    QRhiVertexInputLayout layout;
    layout.setBindings({{2 * sizeof(float)}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setSampleCount(renderTarget()->sampleCount());
    m_pipeline->setShaderResourceBindings(m_layoutSrb.get());
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->create();

    m_initialUpdates = m_rhi->nextResourceUpdateBatch();
    m_initialUpdates->uploadStaticBuffer(m_vbuf.get(), quad);
}

void CanvasRenderer::synchronize(QQuickRhiItem* item)
{
    auto* view = static_cast<CanvasView*>(item);
    const qreal dpr = view->window() ? view->window()->effectiveDevicePixelRatio() : 1.0;
    Editor* editor = view->editor();

    const QSize size = editor ? QSize(editor->canvasWidth(), editor->canvasHeight()) : QSize(1, 1);
    m_wantedSize = size;
    if (editor) {
        const QRect dirty = editor->takeDirty();
        const QRect overlayDirty = editor->takeOverlayDirty();
        const QRect onionDirty = editor->takeOnionDirty();
        const QRect selectionDirty = editor->takeSelectionDirty();
        if (size != m_textureSize) {
            m_upload = editor->composite().copy();
            m_uploadPos = {};
            m_overlayUpload = editor->overlay().copy();
            m_overlayUploadPos = {};
            m_onionUpload = editor->onion().copy();
            m_onionUploadPos = {};
            m_selectionUpload = editor->selectionImage().copy();
            m_selectionUploadPos = {};
        } else {
            if (!selectionDirty.isEmpty()) {
                // A pending upload (no render since) is merged into this one.
                const QRect area = m_selectionUpload.isNull()
                    ? selectionDirty
                    : selectionDirty.united(QRect(m_selectionUploadPos, m_selectionUpload.size()));
                m_selectionUpload = editor->selectionImage().copy(area);
                m_selectionUploadPos = area.topLeft();
            }
            if (!onionDirty.isEmpty()) {
                m_onionUpload = editor->onion().copy(onionDirty);
                m_onionUploadPos = onionDirty.topLeft();
            }
            if (!dirty.isEmpty()) {
                m_upload = editor->composite().copy(dirty);
                m_uploadPos = dirty.topLeft();
            }
            if (!overlayDirty.isEmpty()) {
                m_overlayUpload = editor->overlay().copy(overlayDirty);
                m_overlayUploadPos = overlayDirty.topLeft();
            }
        }
    }

    Uniforms& u = m_uniforms;
    const qreal zoom = view->zoom();
    u.viewport[2] = float(size.width());
    u.viewport[3] = float(size.height());
    u.xform[0] = float(view->offset().x() * dpr);
    u.xform[1] = float(view->offset().y() * dpr);
    u.xform[2] = float(zoom * dpr);
    setColor(u.background, view->backgroundColor(), 1);
    setColor(u.border, view->borderColor());
    if (editor) {
        u.tiling[0] = editor->tileX() ? 1.f : 0.f;
        u.tiling[1] = editor->tileY() ? 1.f : 0.f;
        u.tileDim[1] = editor->onionFront() ? 1.f : 0.f;
    }
    setColor(u.checkA, view->checkerA(), 1);
    setColor(u.checkB, view->checkerB(), 1);
    setColor(u.grid, view->gridColor());
    setColor(u.docGrid, view->documentGridColor());
    u.tileDim[0] = view->dimTiledCopies() ? 0.4f : 0.f;
    u.tiling[2] = view->checkerFixed() ? 1.f : 0.f;
    u.tiling[3] = float(12.0 * dpr);

    // Checker cells are a power of two of canvas pixels, ~12 screen pixels wide.
    u.params[0] = view->checkerSize() > 0
        ? float(view->checkerSize())
        : float(std::max(1.0, std::exp2(std::round(std::log2(12.0 / zoom)))));
    const qreal t = std::clamp((zoom - 4.0) / 6.0, 0.0, 1.0);
    u.params[1] = view->showGrid() ? float(t * t * (3 - 2 * t)) : 0.f;

    u.params[2] = float(view->antsPhase());
    u.params[3] = float(dpr);
    const QRect sel = editor ? editor->selection() : QRect();
    u.selection[0] = float(sel.x());
    u.selection[1] = float(sel.y());
    u.selection[2] = float(sel.width());
    u.selection[3] = float(sel.height());

    if (editor) {
        const QPointF c = editor->symmetryCenter();
        u.symmetry[0] = float(c.x());
        u.symmetry[1] = float(c.y());
        u.symmetry[2] = float((editor->symmetryHorizontal() ? 1 : 0) | (editor->symmetryVertical() ? 2 : 0)
                              | (editor->symmetryRising() ? 4 : 0) | (editor->symmetryFalling() ? 8 : 0));
    }

    const QRect grid = editor ? editor->grid() : QRect();
    const bool customGrid = view->showCustomGrid() && grid.width() > 0 && grid.height() > 0
                         && grid.width() * zoom >= 3 && grid.height() * zoom >= 3; // too dense to read
    u.customGrid[0] = float(grid.x());
    u.customGrid[1] = float(grid.y());
    u.customGrid[2] = customGrid ? float(grid.width()) : 0.f;
    u.customGrid[3] = customGrid ? float(grid.height()) : 0.f;

    u.hoverColor[3] = 0;
    if (editor && view->showsBrushPreview()) {
        // The eraser, blur and jumble don't paint a color: a neutral preview.
        const bool eraser = editor->tool() == Editor::Eraser || editor->tool() == Editor::Blur
                            || editor->tool() == Editor::Jumble;
        const bool brush = editor->usesBrush(editor->tool());
        const QSize extent = brush ? editor->brushExtent() : QSize(1, 1);
        const qint64 key = brush ? editor->brushKey() : -2;
        if (key != m_brushKey) {
            if (brush) {
                m_brushUpload = editor->brushImage();
            } else {
                m_brushUpload = QImage(1, 1, QImage::Format_RGBA8888);
                m_brushUpload.fill(Qt::white);
            }
            m_brushKey = key;
        }
        const QPoint p = view->hoverPixel();
        u.hoverRect[0] = float(p.x() - extent.width() / 2);
        u.hoverRect[1] = float(p.y() - extent.height() / 2);
        u.hoverRect[2] = float(extent.width());
        u.hoverRect[3] = float(extent.height());
        u.brushStyle[0] = brush && editor->previewsBrushColors() ? 1.f : 0.f;
        u.brushStyle[1] = 0.75f;
        if (editor->usesBrush(editor->tool()) && !eraser)
            setColor(u.hoverColor, editor->primaryColor(), 0.6f * float(editor->primaryColor().alphaF()));
        else
            setColor(u.hoverColor, QColor(255, 255, 255), eraser ? 0.3f : 0.15f);
    }
}

void CanvasRenderer::render(QRhiCommandBuffer* cb)
{
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    if (m_initialUpdates) {
        updates->merge(m_initialUpdates);
        m_initialUpdates->release();
        m_initialUpdates = nullptr;
    }

    if (m_textureSize != m_wantedSize) {
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_wantedSize));
        m_texture->create();
        m_overlayTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_wantedSize));
        m_overlayTexture->create();
        m_onionTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_wantedSize));
        m_onionTexture->create();
        m_selectionTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_wantedSize));
        m_selectionTexture->create();
        m_textureSize = m_wantedSize;
        m_srb.reset();
    }
    if (!m_brushUpload.isNull() && (!m_brushTexture || m_brushTexture->pixelSize() != m_brushUpload.size())) {
        m_brushTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_brushUpload.size()));
        m_brushTexture->create();
        m_srb.reset();
    }
    if (!m_brushTexture) { // until the first preview: something to bind
        m_brushTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        m_brushTexture->create();
    }
    if (!m_srb)
        m_srb.reset(makeBindings(m_texture.get(), m_overlayTexture.get(), m_onionTexture.get(),
                                 m_selectionTexture.get(), m_brushTexture.get()));
    if (!m_brushUpload.isNull()) {
        updates->uploadTexture(m_brushTexture.get(), m_brushUpload);
        m_brushUpload = {};
    }
    if (!m_upload.isNull()) {
        QRhiTextureSubresourceUploadDescription desc(m_upload);
        desc.setDestinationTopLeft(m_uploadPos);
        updates->uploadTexture(m_texture.get(), QRhiTextureUploadEntry(0, 0, desc));
        m_upload = {};
    }
    if (!m_overlayUpload.isNull()) {
        QRhiTextureSubresourceUploadDescription desc(m_overlayUpload);
        desc.setDestinationTopLeft(m_overlayUploadPos);
        updates->uploadTexture(m_overlayTexture.get(), QRhiTextureUploadEntry(0, 0, desc));
        m_overlayUpload = {};
    }
    if (!m_onionUpload.isNull()) {
        QRhiTextureSubresourceUploadDescription desc(m_onionUpload);
        desc.setDestinationTopLeft(m_onionUploadPos);
        updates->uploadTexture(m_onionTexture.get(), QRhiTextureUploadEntry(0, 0, desc));
        m_onionUpload = {};
    }
    if (!m_selectionUpload.isNull()) {
        QRhiTextureSubresourceUploadDescription desc(m_selectionUpload);
        desc.setDestinationTopLeft(m_selectionUploadPos);
        updates->uploadTexture(m_selectionTexture.get(), QRhiTextureUploadEntry(0, 0, desc));
        m_selectionUpload = {};
    }

    const QSize out = renderTarget()->pixelSize();
    m_uniforms.viewport[0] = float(out.width());
    m_uniforms.viewport[1] = float(out.height());
    m_uniforms.xform[3] = m_rhi->isYUpInNDC() ? 1.f : -1.f;
    updates->updateDynamicBuffer(m_ubuf.get(), 0, sizeof(Uniforms), &m_uniforms);

    cb->beginPass(renderTarget(), Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport(QRhiViewport(0, 0, float(out.width()), float(out.height())));
    cb->setShaderResources(m_srb.get());
    const QRhiCommandBuffer::VertexInput input(m_vbuf.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4);
    cb->endPass();
}

} // namespace

CanvasView::CanvasView(QQuickItem* parent)
    : QQuickRhiItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, false);
    setActiveFocusOnTab(false);
    m_zoomTimer.setInterval(8);
    connect(&m_zoomTimer, &QTimer::timeout, this, &CanvasView::animationTick);
    m_antsTimer.setInterval(120);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        m_antsPhase = (m_antsPhase + 1) % 8; // one dash period is 8 px
        update();
    });
    updateCursor();
}

QQuickRhiItemRenderer* CanvasView::createRenderer() { return new CanvasRenderer; }

void CanvasView::setEditor(Editor* editor)
{
    if (m_editor == editor)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &Editor::canvasChanged, this, &QQuickItem::update);
        connect(m_editor, &Editor::gridChanged, this, &QQuickItem::update);
        connect(m_editor, &Editor::colorsChanged, this, &QQuickItem::update);
        connect(m_editor, &Editor::brushSizeChanged, this, &QQuickItem::update);
        connect(m_editor, &Editor::toolOptionsChanged, this, &QQuickItem::update); // preview colors, symmetry axes
        connect(m_editor, &Editor::symmetryCenterChanged, this, &QQuickItem::update);
        connect(m_editor, &Editor::toolChanged, this, [this] {
            updateCursor();
            updateLayerPick();
            update();
        });
        connect(m_editor, &Editor::effectChanged, this, &CanvasView::updateCursor);
        connect(m_editor, &Editor::playingChanged, this, [this] {
            updateCursor();
            update();
        });
        connect(m_editor, &Editor::liveChanged, this, &CanvasView::updateCursor);
        connect(m_editor, &Editor::tileModeChanged, this, &QQuickItem::update);
        connect(m_editor, &Editor::selectionChanged, this, [this] {
            updateCursor();
            if (m_editor->hasSelection())
                m_antsTimer.start();
            else
                m_antsTimer.stop();
            update();
        });
        connect(m_editor, &Editor::documentChanged, this, &CanvasView::documentOrSizeChanged);
        connect(m_editor, &Editor::canvasSizeChanged, this, &CanvasView::documentOrSizeChanged);
    }
    m_viewDocument.clear();
    documentOrSizeChanged();
    emit editorChanged();
}

// Another tab: keep the old one's view and bring back this one's (fit the
// first time). Same tab with a new canvas size (sprite / canvas size, undo):
// fit again.
void CanvasView::documentOrSizeChanged()
{
    if (!m_editor)
        return;
    const QString id = m_editor->documentId();
    const QSize canvas(m_editor->canvasWidth(), m_editor->canvasHeight());
    if (id == m_viewDocument) {
        if (canvas != m_viewCanvas) {
            m_viewCanvas = canvas;
            fitToView();
        }
        return;
    }
    if (!m_viewDocument.isEmpty()) {
        m_zoomTimer.stop();
        m_views.insert(m_viewDocument, {m_zoom, m_offset, m_viewCanvas});
    }
    m_viewDocument = id;
    m_viewCanvas = canvas;
    const auto it = m_views.constFind(id);
    if (it == m_views.cend() || it->canvas != canvas) {
        fitToView();
        return;
    }
    m_zoomTimer.stop();
    m_zoom = m_targetZoom = it->zoom;
    m_offset = it->offset;
    emit viewChanged();
    update();
}

void CanvasView::setShowGrid(bool show)
{
    if (m_showGrid == show)
        return;
    m_showGrid = show;
    emit showGridChanged();
    update();
}

void CanvasView::setShowSlices(bool show)
{
    if (m_showSlices == show)
        return;
    m_showSlices = show;
    emit showSlicesChanged();
}

void CanvasView::setShowCustomGrid(bool show)
{
    if (m_showCustomGrid == show)
        return;
    m_showCustomGrid = show;
    emit showCustomGridChanged();
    update();
}

void CanvasView::setCheckerStyle(int style)
{
    style = std::clamp(style, 0, 3);
    if (m_checkerStyle == style)
        return;
    m_checkerStyle = style;
    emit checkerChanged();
    update();
}

void CanvasView::setCustomCheckerA(const QColor& c)
{
    if (!c.isValid() || m_customCheckerA == c)
        return;
    m_customCheckerA = c;
    emit checkerChanged();
    update();
}

void CanvasView::setCustomCheckerB(const QColor& c)
{
    if (!c.isValid() || m_customCheckerB == c)
        return;
    m_customCheckerB = c;
    emit checkerChanged();
    update();
}

void CanvasView::setDimTiledCopies(bool dim)
{
    if (m_dimTiledCopies == dim)
        return;
    m_dimTiledCopies = dim;
    emit gridColorsChanged();
    update();
}

void CanvasView::setCheckerFixed(bool fixed)
{
    if (m_checkerFixed == fixed)
        return;
    m_checkerFixed = fixed;
    emit checkerChanged();
    update();
}

void CanvasView::setGridColor(const QColor& c)
{
    if (!c.isValid() || m_gridColor == c)
        return;
    m_gridColor = c;
    emit gridColorsChanged();
    update();
}

void CanvasView::setDocumentGridColor(const QColor& c)
{
    if (!c.isValid() || m_documentGridColor == c)
        return;
    m_documentGridColor = c;
    emit gridColorsChanged();
    update();
}

void CanvasView::setCheckerSize(int size)
{
    size = std::clamp(size, 0, 64);
    if (m_checkerSize == size)
        return;
    m_checkerSize = size;
    emit checkerChanged();
    update();
}

void CanvasView::setCheckerFollowsTheme(bool follows)
{
    if (m_checkerThemed == follows)
        return;
    m_checkerThemed = follows;
    emit checkerChanged();
    update();
}

void CanvasView::setThemeCheckerA(const QColor& c)
{
    if (m_themeCheckerA == c)
        return;
    m_themeCheckerA = c;
    emit checkerChanged();
    update();
}

void CanvasView::setThemeCheckerB(const QColor& c)
{
    if (m_themeCheckerB == c)
        return;
    m_themeCheckerB = c;
    emit checkerChanged();
    update();
}

void CanvasView::setBackgroundColor(const QColor& c)
{
    if (m_background == c)
        return;
    m_background = c;
    emit backgroundColorChanged();
    update();
}

void CanvasView::resetPreferences()
{
    setCheckerStyle(kCheckerStyle);
    setCheckerSize(kCheckerSize);
    setCustomCheckerA(kCustomCheckerA);
    setCustomCheckerB(kCustomCheckerB);
    setCheckerFixed(kCheckerFixed);
    setCheckerFollowsTheme(kCheckerThemed);
    setDimTiledCopies(kDimTiledCopies);
    setGridColor(kGridColor);
    setDocumentGridColor(kDocumentGridColor);
    setScrollZooms(kScrollZooms);
}

void CanvasView::setScrollZooms(bool zooms)
{
    if (m_scrollZooms == zooms)
        return;
    m_scrollZooms = zooms;
    emit scrollZoomsChanged();
}

void CanvasView::setBorderColor(const QColor& c)
{
    if (m_border == c)
        return;
    m_border = c;
    emit backgroundColorChanged();
    update();
}

bool CanvasView::showsBrushPreview() const
{
    if (!m_hovering || m_panning || m_spaceHeld || !m_editor)
        return false;
    const Editor::Tool tool = m_editor->tool();
    if (m_editor->lockedToPan())
        return false; // playing: no brush to show
    if (m_editor->tilesActive() && (tool == Editor::Pencil || tool == Editor::Eraser || tool == Editor::Eyedropper))
        return false; // tile mode shows the grid cell instead (Main.qml)
    return tool != Editor::Hand && tool != Editor::Select && tool != Editor::Move && tool != Editor::Lasso
        && tool != Editor::MagicWand && tool != Editor::Text && tool != Editor::SliceTool && tool != Editor::Gradient;
}

int CanvasView::antsPhase() const { return m_antsPhase; }

// --- view transform ---------------------------------------------------------

QPoint CanvasView::toPixel(QPointF pos) const
{
    const QPointF p = (pos - m_offset) / m_zoom;
    return {int(std::floor(p.x())), int(std::floor(p.y()))};
}

void CanvasView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickRhiItem::geometryChange(newGeometry, oldGeometry);
    if (m_needsFit) {
        fitToView();
    } else {
        // Keep the canvas centered relative to the resize. (viewChanged: the
        // overlays in QML, e.g. the transform handles, follow `offset`.)
        // (With a view area, its own changes keep it centered instead.)
        if (m_viewArea.isEmpty())
            m_offset += QPointF(newGeometry.width() - oldGeometry.width(),
                                newGeometry.height() - oldGeometry.height()) / 2;
        emit viewChanged();
        update();
    }
}

void CanvasView::setViewArea(const QRectF& area)
{
    const QRectF before = freeArea();
    if (area == m_viewArea)
        return;
    m_viewArea = area;
    emit viewAreaChanged();
    // The canvas stays in the middle of what's free as the panels change.
    if (!m_needsFit)
        m_offset += freeArea().center() - before.center();
    emit viewChanged();
    update();
}

void CanvasView::fitToView()
{
    if (!m_editor || width() <= 0 || height() <= 0) {
        m_needsFit = true;
        return;
    }
    m_needsFit = false;
    m_zoomTimer.stop();
    const qreal cw = m_editor->canvasWidth();
    const qreal ch = m_editor->canvasHeight();
    const QRectF free = freeArea();
    const qreal fit = 0.75 * std::min(free.width() / cw, free.height() / ch);
    qreal zoom = kMinZoom;
    for (qreal level : kZoomLevels)
        if (level <= fit)
            zoom = level;
    m_zoom = m_targetZoom = zoom;
    m_offset = QPointF(std::round(free.center().x() - cw * zoom / 2), std::round(free.center().y() - ch * zoom / 2));
    emit viewChanged();
    update();
}

QColor CanvasView::checkerA() const
{
    return m_checkerThemed ? m_themeCheckerA : m_checkerStyle == 3 ? m_customCheckerA : QColor(kCheckers[std::clamp(m_checkerStyle, 0, 2)][0]);
}
QColor CanvasView::checkerB() const
{
    return m_checkerThemed ? m_themeCheckerB : m_checkerStyle == 3 ? m_customCheckerB : QColor(kCheckers[std::clamp(m_checkerStyle, 0, 2)][1]);
}

void CanvasView::zoomTo(qreal zoom)
{
    m_zoomTimer.stop();
    applyZoom(zoom, freeArea().center());
    m_targetZoom = m_zoom;
    m_offset = QPointF(std::round(m_offset.x()), std::round(m_offset.y()));
    emit viewChanged();
    update();
}

void CanvasView::zoomIn() { stepZoom(1, freeArea().center()); }
void CanvasView::zoomOut() { stepZoom(-1, freeArea().center()); }

void CanvasView::stepZoom(int direction, QPointF anchor)
{
    const qreal from = m_targetZoom;
    qreal to = from;
    if (direction > 0) {
        for (qreal level : kZoomLevels)
            if (level > from + 1e-6) {
                to = level;
                break;
            }
    } else {
        for (auto it = kZoomLevels.rbegin(); it != kZoomLevels.rend(); ++it)
            if (*it < from - 1e-6) {
                to = *it;
                break;
            }
    }
    animateZoomTo(to, anchor);
}

void CanvasView::animateZoomTo(qreal zoom, QPointF anchor)
{
    m_targetZoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m_zoomAnchor = anchor;
    if (!m_zoomTimer.isActive())
        m_zoomTimer.start();
}

void CanvasView::animationTick()
{
    // Exponential ease in log space so zooming feels uniform at every level.
    const qreal current = std::log(m_zoom);
    const qreal target = std::log(m_targetZoom);
    qreal next = current + (target - current) * 0.3;
    if (std::abs(target - next) < 0.01) {
        next = target;
        m_zoomTimer.stop();
    }
    applyZoom(std::exp(next), m_zoomAnchor);
    if (!m_zoomTimer.isActive()) {
        // Snap to whole screen pixels so the canvas stays crisp at rest.
        m_offset = QPointF(std::round(m_offset.x()), std::round(m_offset.y()));
        emit viewChanged();
        update();
    }
}

void CanvasView::applyZoom(qreal zoom, QPointF anchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m_offset = anchor - (anchor - m_offset) * (zoom / m_zoom);
    m_zoom = zoom;
    emit viewChanged();
    update();
}

void CanvasView::zoomBy(qreal factor, QPointF center)
{
    m_zoomTimer.stop();
    applyZoom(m_zoom * factor, center);
    m_targetZoom = m_zoom;
}

// --- input ------------------------------------------------------------------

namespace {

// The pipette, white with a dark outline so it shows on any color; the hot
// spot is its tip.
const QCursor& pipetteCursor()
{
    static const QCursor cursor = [] {
        constexpr int size = 24, pad = 1, scale = 2; // logical px, drawn at 2x
        QImageReader reader(QStringLiteral(":/icons/pipette.svg"));
        reader.setScaledSize(QSize(size * scale, size * scale));
        const QImage white = reader.read();
        if (white.isNull())
            return QCursor(Qt::CrossCursor);
        QImage dark = white;
        {
            QPainter p(&dark);
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(dark.rect(), QColor(20, 20, 24));
        }
        QImage out((size + 2 * pad) * scale, (size + 2 * pad) * scale, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        const int o = pad * scale;
        for (int dy = -2; dy <= 2; dy += 2)
            for (int dx = -2; dx <= 2; dx += 2)
                if (dx || dy)
                    p.drawImage(o + dx, o + dy, dark);
        p.drawImage(o, o, white);
        p.end();
        QPixmap pixmap = QPixmap::fromImage(out);
        pixmap.setDevicePixelRatio(scale);
        // The tip of Lucide's pipette is at (2, 22) of 24.
        return QCursor(pixmap, pad + 2, pad + 22);
    }();
    return cursor;
}

} // namespace

void CanvasView::setModifiers(Qt::KeyboardModifiers modifiers)
{
    if (modifiers == m_modifiers)
        return;
    m_modifiers = modifiers;
    updateCursor();
    updateLayerPick();
}

// Ctrl+Alt held with a tool that has a brush size. On a Mac the Control key is
// Qt's Meta (Cmd is Control there); Ctrl+click arrives as a right click, so either
// button works.
bool CanvasView::brushResizeGesture(Qt::KeyboardModifiers modifiers) const
{
    if (!m_editor || m_editor->effectActive())
        return false;
    switch (m_editor->tool()) {
    case Editor::Pencil: case Editor::Eraser:
        if (m_editor->brushShape() == Editor::CustomBrush && !m_editor->hasCustomBrush())
            return false;
        break;
    case Editor::Blur: case Editor::Jumble:
        if (m_editor->brushShape() == Editor::CustomBrush)
            return false;
        break;
    default: return false;
    }
#ifdef Q_OS_MACOS
    const Qt::KeyboardModifier ctrl = Qt::MetaModifier;
#else
    const Qt::KeyboardModifier ctrl = Qt::ControlModifier;
#endif
    return (modifiers & ctrl) && (modifiers & Qt::AltModifier) && !(modifiers & Qt::ShiftModifier);
}

// Ctrl+Alt+Shift: the same drag turns the brush instead (the pencil and eraser).
bool CanvasView::brushTurnGesture(Qt::KeyboardModifiers modifiers) const
{
    if (!m_editor || m_editor->effectActive())
        return false;
    if (m_editor->tool() != Editor::Pencil && m_editor->tool() != Editor::Eraser)
        return false;
#ifdef Q_OS_MACOS
    const Qt::KeyboardModifier ctrl = Qt::MetaModifier;
#else
    const Qt::KeyboardModifier ctrl = Qt::ControlModifier;
#endif
    return (modifiers & ctrl) && (modifiers & Qt::AltModifier) && (modifiers & Qt::ShiftModifier);
}

void CanvasView::updateLayerPick()
{
    const bool picking = m_editor && m_hovering && (m_modifiers & Qt::ControlModifier)
                         && !brushResizeGesture(m_modifiers) && !brushTurnGesture(m_modifiers)
                         && m_editor->tool() != Editor::Hand && !m_editor->effectActive();
    const QString name = picking ? m_editor->layerNameAt(hoverPixel()) : QString();
    if (picking == m_layerPicking && name == m_layerPickName)
        return;
    m_layerPicking = picking;
    m_layerPickName = name;
    emit layerPickChanged();
}

void CanvasView::updateCursor()
{
    const Editor::Tool tool = m_editor ? m_editor->tool() : Editor::Pencil;
    const bool altPick = (m_modifiers & Qt::AltModifier)
                         && (tool == Editor::Pencil || tool == Editor::Eraser || tool == Editor::Fill);
    if (m_panning)
        setCursor(Qt::ClosedHandCursor);
    else if (m_spaceHeld || (m_editor && (m_editor->tool() == Editor::Hand || m_editor->lockedToPan())))
        setCursor(Qt::OpenHandCursor);
    else if (m_sizing || brushResizeGesture(m_modifiers) || brushTurnGesture(m_modifiers))
        setCursor(Qt::SizeHorCursor); // drag sideways: the brush size
    else if (m_editor && m_modifiers & Qt::ControlModifier && !m_editor->effectActive())
        setCursor(Qt::PointingHandCursor); // a click picks the layer
    else if (m_editor && !m_editor->effectActive() && (tool == Editor::Eyedropper || altPick))
        setCursor(pipetteCursor());
    else if (m_editor && tool == Editor::Text)
        setCursor(m_hovering && m_editor->floating() && m_editor->selectionContains(hoverPixel()) ? Qt::SizeAllCursor
                                                                                                  : Qt::IBeamCursor);
    else if (m_editor
             && (m_editor->tool() == Editor::Move
                 || (m_editor->tool() == Editor::Select && m_hovering
                     && m_editor->selectionContains(hoverPixel()))))
        setCursor(Qt::SizeAllCursor);
    else if (m_editor && m_hovering && (tool == Editor::Pencil || tool == Editor::Eraser)
             && m_editor->brushShape() == Editor::CustomBrush && m_editor->hasCustomBrush())
        setCursor(Qt::BlankCursor); // the brush's own picture under the pointer is the cursor
    else
        setCursor(Qt::CrossCursor);
}

void CanvasView::setHover(QPointF pos)
{
    const QPoint p = toPixel(pos);
    m_hoverPos = pos;
    if (m_hovering && p == m_hover) {
        emit hoverChanged(); // the label follows the pointer within a pixel too
        return;
    }
    m_hover = p;
    m_hovering = true;
    emit hoverChanged();
    if (m_editor && (m_editor->tool() == Editor::Select || m_editor->tool() == Editor::Text
                     || m_editor->brushShape() == Editor::CustomBrush))
        updateCursor();
    updateLayerPick();
    update();
}

void CanvasView::mousePressEvent(QMouseEvent* event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    emit pressed();
    m_lastPos = event->position();
    const bool panTool = m_spaceHeld || (m_editor && (m_editor->tool() == Editor::Hand || m_editor->lockedToPan()));
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && panTool)) {
        m_panning = true;
        updateCursor();
        update();
    } else if (m_editor && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
               && (brushResizeGesture(event->modifiers()) || brushTurnGesture(event->modifiers()))) {
        m_sizing = true;
        m_turning = brushTurnGesture(event->modifiers());
        m_sizeStartX = event->position().x();
        m_scaling = !m_turning && m_editor->brushShape() == Editor::CustomBrush;
        m_sizeStartValue = m_turning ? m_editor->brushAngle()
                           : m_scaling ? m_editor->customBrushScale() : m_editor->brushSize();
        emit resizingBrushChanged();
        updateCursor();
    } else if (m_editor && m_editor->effectActive()) {
        if (event->button() == Qt::LeftButton)
            emit effectPicked(m_editor->wrapPixel(toPixel(event->position())));
    } else if (m_editor && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        m_editor->beginStroke(toPixel(event->position()), event->button() == Qt::RightButton, event->modifiers());
    }
    event->accept();
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_sizing) {
        // One size step per 6 px; the brush outline stays where the drag began.
        const double dx = event->position().x() - m_sizeStartX;
        if (m_turning) {
            int angle = m_sizeStartValue + int(std::lround(dx / 2.0)); // a degree per 2 px
            if (event->modifiers() & Qt::ShiftModifier) // Shift snaps to 15 degree steps
                angle = int(std::lround(angle / 15.0)) * 15;
            m_editor->setBrushAngle(angle);
        }
        else if (m_scaling)
            m_editor->setCustomBrushScale(m_sizeStartValue + int(std::lround(dx / 24.0))); // a step per 24 px
        else
            m_editor->setBrushSize(m_sizeStartValue + int(std::lround(dx / 6.0)));
        m_lastPos = event->position();
        return;
    }
    if (m_panning) {
        m_offset += event->position() - m_lastPos;
        emit viewChanged();
        update();
    } else if (m_editor) {
        m_editor->moveStroke(toPixel(event->position()), event->modifiers());
    }
    m_lastPos = event->position();
    setHover(event->position());
}

// With the select tool and the document grid shown, a double-click selects
// the grid cell under it; with a polygon lasso being placed, it closes it.
void CanvasView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_editor && event->button() == Qt::LeftButton && m_editor->tool() == Editor::Lasso && m_editor->polygonActive()) {
        m_editor->closePolygon();
        event->accept();
        return;
    }
    if (m_editor && event->button() == Qt::LeftButton && m_editor->tool() == Editor::Select && m_showCustomGrid
        && !m_spaceHeld) {
        m_editor->selectGridCell(m_editor->wrapPixel(toPixel(event->position())), event->modifiers());
        event->accept();
        return;
    }
    QQuickRhiItem::mouseDoubleClickEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_sizing) {
        if (event->buttons() == Qt::NoButton) {
            m_sizing = false;
            m_turning = false;
            m_scaling = false;
            emit resizingBrushChanged();
            updateCursor();
        }
        return;
    }
    if (m_panning && (event->buttons() & (Qt::LeftButton | Qt::MiddleButton)) == 0) {
        m_panning = false;
        m_offset = QPointF(std::round(m_offset.x()), std::round(m_offset.y()));
        emit viewChanged();
        updateCursor();
        update();
    }
    if (m_editor && event->buttons() == Qt::NoButton)
        m_editor->endStroke();
}

void CanvasView::hoverMoveEvent(QHoverEvent* event)
{
    setModifiers(event->modifiers());
    setHover(event->position());
    if (m_editor) {
        m_editor->setHoverInside(true);
        m_editor->setLinePreview(hoverPixel(), event->modifiers() & Qt::ShiftModifier);
    }
}

void CanvasView::hoverLeaveEvent(QHoverEvent*)
{
    if (m_editor) {
        m_editor->setLinePreview(hoverPixel(), false);
        m_editor->setHoverInside(false);
    }
    m_hovering = false;
    emit hoverChanged();
    updateLayerPick();
    update();
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    const bool modifier = event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    const bool zoom = m_scrollZooms != modifier; // the modifier does the other one
    if (!event->pixelDelta().isNull() && !zoom) {
        // Trackpad two-finger scroll (or a smooth-scrolling mouse) pans.
        m_offset += QPointF(event->pixelDelta());
        emit viewChanged();
        update();
    } else if (!event->pixelDelta().isNull()) {
        zoomBy(std::pow(1.01, event->pixelDelta().y()), event->position());
    } else if (m_scrollZooms && modifier) {
        // A notched wheel with the modifier pans by a few steps' worth.
        m_offset += QPointF(event->angleDelta()) / 120.0 * 40.0;
        emit viewChanged();
        update();
    } else {
        // Mouse wheel steps through zoom levels.
        m_wheelAccum += event->angleDelta().y();
        while (std::abs(m_wheelAccum) >= 120) {
            const int dir = m_wheelAccum > 0 ? 1 : -1;
            stepZoom(dir, event->position());
            m_wheelAccum -= dir * 120;
        }
    }
    event->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* event)
{
    setModifiers(QGuiApplication::queryKeyboardModifiers());
    if (m_editor && (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt)) {
        const auto mods = QGuiApplication::queryKeyboardModifiers();
        m_editor->setStrokeModifiers(mods);
        m_editor->setLinePreview(hoverPixel(), m_hovering && (mods & Qt::ShiftModifier));
    }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        update();
        event->accept();
        return;
    }
    QQuickRhiItem::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event)
{
    setModifiers(QGuiApplication::queryKeyboardModifiers());
    if (m_editor && (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt)) {
        const auto mods = QGuiApplication::queryKeyboardModifiers();
        m_editor->setStrokeModifiers(mods);
        m_editor->setLinePreview(hoverPixel(), m_hovering && (mods & Qt::ShiftModifier));
    }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor();
        update();
        event->accept();
        return;
    }
    QQuickRhiItem::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent* event)
{
    if (m_editor)
        m_editor->setLinePreview(hoverPixel(), false);
    m_spaceHeld = false;
    m_modifiers = {};
    updateCursor();
    updateLayerPick();
    QQuickRhiItem::focusOutEvent(event);
}
