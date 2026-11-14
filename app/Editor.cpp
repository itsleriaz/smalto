#include "Editor.h"

#include "core/ColorConvert.h"
#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Effects.h"
#include "core/Gradient.h"
#include "core/PaletteTools.h"
#include "core/Raster.h"
#include "core/Symmetry.h"
#include "core/Text.h"
#include "core/Tilemap.h"
#include "formats/LiveLayerData.h"
#include "formats/Aseprite.h"
#include "formats/Bdf.h"
#include "formats/Export.h"
#include "formats/TilesetExport.h"
#include "formats/FileIO.h"
#include "formats/PaletteIO.h"
#include "formats/ImageImport.h"

#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QMetaEnum>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QProcess>
#include <QLocale>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

#include <climits>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>

using namespace core;

namespace {

Rgba toRgba(const QColor& c) { return {uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), uint8_t(c.alpha())}; }
QColor toQColor(Rgba c) { return QColor(c.r, c.g, c.b, c.a); }
Point toPoint(QPoint p) { return {p.x(), p.y()}; }
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

std::filesystem::path toFsPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}

int indexIn(const Layer& parent, const Layer* layer)
{
    const auto& c = parent.children;
    const auto it = std::find_if(c.begin(), c.end(), [&](const auto& p) { return p.get() == layer; });
    return it == c.end() ? -1 : int(it - c.begin());
}

int depthOf(const Layer& layer)
{
    int depth = 0;
    for (const Layer* p = layer.parent; p && p->parent; p = p->parent)
        ++depth;
    return depth;
}

// Visible and unlocked, including every enclosing group.
bool effectivelyVisible(const Layer& layer)
{
    for (const Layer* l = &layer; l && l->parent; l = l->parent)
        if (!l->visible)
            return false;
    return true;
}

bool effectivelyLocked(const Layer& layer)
{
    if (layer.isLive())
        return true; // its pixels come from the recipe
    for (const Layer* l = &layer; l && l->parent; l = l->parent)
        if (l->locked)
            return true;
    return false;
}

const char* kindName(const Layer& layer)
{
    if (layer.isLive())
        return "particles";
    const LayerKind kind = layer.kind;
    switch (kind) {
    case LayerKind::Group: return "group";
    case LayerKind::Tilemap: return "tilemap";
    default: return "image";
    }
}

} // namespace

Editor::Editor(QObject* parent)
    : QObject(parent)
{
    m_undo.setByteLimit(size_t(m_undoMemory) * 1024 * 1024);
    m_playTimer.setSingleShot(true);
    m_playTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playTimer, &QTimer::timeout, this, &Editor::advancePlayback);
    connect(this, &Editor::historyChanged, this, &Editor::updateModified);
    connect(this, &Editor::historyChanged, this, &Editor::checkGradient);
    if (auto* clipboard = QGuiApplication::clipboard())
        connect(clipboard, &QClipboard::dataChanged, this, &Editor::clipboardChanged);
    connect(this, &Editor::historyChanged, this, &Editor::liveChanged);
    connect(this, &Editor::currentLayerChanged, this, &Editor::liveChanged);
    connect(this, &Editor::liveChanged, this, &Editor::syncLiveTool);
    connect(this, &Editor::currentLayerChanged, this, [this] { m_particleContext = 0; });
    for (auto signal : {&Editor::toolChanged, &Editor::colorsChanged, &Editor::brushSizeChanged,
                        &Editor::selectionChanged, &Editor::currentLayerChanged, &Editor::toolOptionsChanged})
        connect(this, signal, this, &Editor::refreshOverlay);
    m_autosaveTimer.setSingleShot(true);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &Editor::autosaveNow);
    connect(this, &Editor::historyChanged, this, &Editor::syncTileLink);
    connect(this, &Editor::historyChanged, this, [this] {
        if (m_tileLink.active)
            return; // a tile tab's pixels live in the tileset of its source
        m_autosavePending = true;
        if (m_autosaveDelay > 0)
            m_autosaveTimer.start(m_autosaveDelay * 1000); // after the last change
    });
    // The tileset shown is the current layer's.
    connect(this, &Editor::currentLayerChanged, this, &Editor::bumpTileset);
    connect(this, &Editor::currentLayerChanged, this, [this] {
        if (syncSmearAsDrawn())
            invalidateAll();
    });
    for (auto signal : {&Editor::historyChanged, &Editor::currentLayerChanged, &Editor::currentFrameChanged,
                        &Editor::layersChanged, &Editor::celsChanged, &Editor::documentChanged})
        connect(this, signal, this, &Editor::refreshSmearState);
    // Slices can change from frame to frame (their keys).
    connect(this, &Editor::currentFrameChanged, this, &Editor::slicesChanged);
    // Floating text follows the primary color.
    connect(this, &Editor::colorsChanged, this, [this] {
        if (m_text.active && m_float.active)
            renderTextFloat();
    });
    m_docs.resize(1);
    newDocument(64, 64);
    m_placeholder = true;
    connect(this, &Editor::fileChanged, this, &Editor::documentsChanged);
    connect(this, &Editor::modifiedChanged, this, &Editor::documentsChanged);
}

Editor::~Editor()
{
    if (m_recovery)
        m_recovery->endSession(m_keepRecovery);
}

void Editor::newDocument(int width, int height)
{
    newSprite({{"width", width}, {"height", height}});
}

void Editor::newSprite(const QVariantMap& options)
{
    const int width = std::clamp(options.value("width", 64).toInt(), 1, 4096);
    const int height = std::clamp(options.value("height", 64).toInt(), 1, 4096);
    const QString modeName = options.value("colorMode", "rgba").toString();
    const ColorMode mode = modeName == "grayscale" ? ColorMode::Grayscale
                         : modeName == "indexed"   ? ColorMode::Indexed
                                                   : ColorMode::Rgba;
    const QString background = options.value("background", "transparent").toString();
    const int frames = std::clamp(options.value("frames", 1).toInt(), 1, 9999);

    auto doc = std::make_unique<Document>(width, height, mode);
    doc->palette = Palette::defaultPalette();
    doc->grid = {0, 0, m_newGridSize.width(), m_newGridSize.height()};
    for (int f = 1; f < frames; ++f)
        doc->insertFrame(f, Frame{});

    if (background == "white" || background == "black" || background == "color") {
        // An opaque Background layer, as in Aseprite. In indexed mode it uses
        // the nearest palette entry (a background layer may use any index).
        const QColor chosen = options.value("backgroundColor").value<QColor>();
        const Rgba color = background == "white" ? Rgba{255, 255, 255, 255}
                         : background == "black" ? Rgba{0, 0, 0, 255}
                                                 : Rgba{uint8_t(chosen.red()), uint8_t(chosen.green()), uint8_t(chosen.blue()), 255};
        uint32_t value = fromRgba(*doc, color);
        if (mode == ColorMode::Indexed) {
            int best = 0, bestDistance = INT_MAX;
            for (int i = 0; i < doc->palette.size(); ++i) {
                const Rgba p = doc->palette.color(i);
                const int d = std::abs(p.r - color.r) + std::abs(p.g - color.g) + std::abs(p.b - color.b);
                if (d < bestDistance) {
                    best = i;
                    bestDistance = d;
                }
            }
            value = uint32_t(best);
        }
        auto layer = doc->makeImageLayer("Background");
        layer->extraFlags |= Layer::kBackgroundFlag;
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(width, height, doc->pixelFormat());
        cel->image->clear(value);
        // Each frame gets its own copy, as when Aseprite adds frames.
        for (auto& slot : layer->cels)
            slot = cloneCel(cel);
        doc->insertLayer(doc->root(), 0, std::move(layer));
        doc->insertLayer(doc->root(), 1, doc->makeImageLayer("Layer 1"));
    } else {
        doc->insertLayer(doc->root(), 0, doc->makeImageLayer("Layer 1"));
    }
    setDocument(std::move(doc), {}, nextUntitledName());
}

// Shows `doc` in a new tab, or in place of the untouched startup sprite.
void Editor::setDocument(std::unique_ptr<Document> doc, const QString& path, const QString& untitledName)
{
    endStroke();
    commitFloating();
    setPlaying(false);
    const bool replace = m_doc == nullptr
        || (m_placeholder && m_filePath.isEmpty() && !m_undo.canUndo() && !m_undo.canRedo());
    m_placeholder = false;
    if (!replace) {
        park(m_docs[size_t(m_currentDoc)]);
        m_docs.emplace_back();
        m_currentDoc = int(m_docs.size()) - 1;
    }
    m_tileLink = {}; // (park restored the other tabs' tiled view)
    if (replace && m_recovery && !m_recoveryId.isEmpty())
        m_recovery->remove(m_recoveryId);
    m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_autosavePending = false;
    m_untitledName = untitledName;
    m_currentSlice = -1;
    m_stroke = {};
    m_undo.clear();
    m_doc = std::move(doc);
    if (m_doc->root().children.empty())
        m_doc->insertLayer(m_doc->root(), 0, m_doc->makeImageLayer("Layer 1"));

    // Start on the topmost image layer.
    m_current = nullptr;
    m_layerCounter = 0;
    m_doc->forEachLayer([&](Layer& layer) {
        ++m_layerCounter;
        if (layer.kind == LayerKind::Image)
            m_current = &layer;
    });
    if (!m_current)
        m_current = m_doc->root().children.back().get();
    m_currentRow = 0;
    m_currentFrame = 0;
    // Before any signal: m_layerList still points into the old, freed document.
    rebuildLayerList();
    m_filePath = path;
    m_rangeType = NoRange;
    m_range = {};
    m_lastPainted.reset();
    m_symmetrySize = {}; // centered again in resetCanvas
    m_recordedSelection = m_doc->selection;
    documentSwitched();
    if (!path.isEmpty())
        emit fileUsed(path);
}

// After the current document was replaced or switched: everything follows.
void Editor::documentSwitched()
{
    m_float = {};
    m_stroke = {};
    if (m_polygon.active) {
        m_polygon = {};
        emit polygonChanged();
        emit lassoPathChanged();
    }
    m_mergeKey.clear();
    emit floatingChanged();
    emit rangeChanged();
    resetCanvas();
    emit documentChanged();
    emit fileChanged();
    emit historyChanged();
    emit clipboardChanged(); // pasting cels depends on the color mode
    updateModified();
    structureChanged();
    emit documentsChanged();
}

void Editor::park(DocumentSlot& slot)
{
    cancelEffect(); // a preview never leaves with its document
    m_currentSlice = -1; // the picked slice is the document's own
    slot.tileLink = m_tileLink;
    if (m_tileLink.active) { // the tiled view is this tab's: the others get theirs back
        slot.tileLink.viewMode = m_tileMode;
        setTileMode(m_tileLink.outerMode);
    }
    slot.doc = std::move(m_doc);
    slot.undo = std::move(m_undo);
    m_undo = {};
    slot.recordedSelection = std::move(m_recordedSelection);
    slot.filePath = m_filePath;
    slot.untitledName = m_untitledName;
    slot.current = m_current;
    slot.currentRow = m_currentRow;
    slot.currentFrame = m_currentFrame;
    slot.layerCounter = m_layerCounter;
    slot.rangeType = m_rangeType;
    slot.layerBlock = m_layerBlock;
    slot.range = m_range;
    slot.lastPainted = m_lastPainted;
    slot.symmetryCenter2 = m_symmetryCenter2;
    slot.symmetrySize = m_symmetrySize;
    slot.recoveryId = m_recoveryId;
    slot.autosavePending = m_autosavePending;
}

void Editor::unpark(DocumentSlot& slot)
{
    const TileLink leaving = m_tileLink; // a tile tab closed or parked
    m_tileLink = slot.tileLink;
    m_doc = std::move(slot.doc);
    m_undo = std::move(slot.undo);
    slot.undo = {};
    m_undo.setByteLimit(size_t(m_undoMemory) * 1024 * 1024);
    m_recordedSelection = std::move(slot.recordedSelection);
    m_filePath = slot.filePath;
    m_untitledName = slot.untitledName;
    m_current = slot.current;
    m_currentRow = slot.currentRow;
    m_currentFrame = slot.currentFrame;
    m_layerCounter = slot.layerCounter;
    m_rangeType = slot.rangeType;
    m_layerBlock = slot.layerBlock;
    m_range = slot.range;
    m_lastPainted = slot.lastPainted;
    m_symmetryCenter2 = slot.symmetryCenter2;
    m_symmetrySize = slot.symmetrySize;
    m_recoveryId = slot.recoveryId;
    m_autosavePending = slot.autosavePending;
    // Before any signal: m_layerList still points into the other document.
    rebuildLayerList();
    // The tiled view, now that the document is in place (QML reads it on the change).
    if (leaving.active)
        setTileMode(leaving.outerMode);
    if (m_tileLink.active) {
        m_tileLink.outerMode = m_tileMode;
        setTileMode(m_tileLink.viewMode);
    }
    documentSwitched();
}

void Editor::setCurrentDocument(int index)
{
    if (index == m_currentDoc || index < 0 || index >= int(m_docs.size()))
        return;
    endStroke();
    closeGradient();
    commitFloating(); // a paste or transform lands where it started
    setPlaying(false);
    park(m_docs[size_t(m_currentDoc)]);
    m_currentDoc = index;
    m_placeholder = false;
    setShadeRange({-1, -1}); // a range of the other document's palette
    unpark(m_docs[size_t(index)]);
}

void Editor::closeDocument(int index)
{
    if (index < 0 || index >= int(m_docs.size()))
        return;
    if (m_recovery)
        m_recovery->remove(index == m_currentDoc ? m_recoveryId : m_docs[size_t(index)].recoveryId);
    if (index != m_currentDoc) {
        m_docs.erase(m_docs.begin() + index);
        if (index < m_currentDoc)
            --m_currentDoc;
        emit documentsChanged();
        return;
    }
    endStroke();
    m_float = {}; // dropped with the document
    setPlaying(false);
    if (m_docs.size() == 1) { // never none: a new untitled sprite takes its place
        m_doc.reset();
        newDocument(64, 64);
        m_placeholder = true;
        return;
    }
    m_docs.erase(m_docs.begin() + index);
    m_currentDoc = std::min(index, int(m_docs.size()) - 1);
    unpark(m_docs[size_t(m_currentDoc)]);
}

void Editor::moveDocument(int from, int to)
{
    const int count = int(m_docs.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;
    // The current document's state lives in the members; its slot just
    // moves with it.
    const auto begin = m_docs.begin();
    if (from < to)
        std::rotate(begin + from, begin + from + 1, begin + to + 1);
    else
        std::rotate(begin + to, begin + from, begin + from + 1);
    if (m_currentDoc == from)
        m_currentDoc = to;
    else if (from < m_currentDoc && to >= m_currentDoc)
        --m_currentDoc;
    else if (from > m_currentDoc && to <= m_currentDoc)
        ++m_currentDoc;
    emit documentsChanged();
}

bool Editor::fileExists(const QString& path)
{
    return QFileInfo(path).isFile();
}

void Editor::revealFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return;
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

QVariantList Editor::documents() const
{
    QVariantList list;
    for (int i = 0; i < int(m_docs.size()); ++i) {
        const bool current = i == m_currentDoc;
        const DocumentSlot& slot = m_docs[size_t(i)];
        list.append(QVariantMap{
            {"name", current ? fileName() : slotName(slot)},
            {"path", current ? m_filePath : slot.filePath},
            {"modified", isDocumentModified(i)},
        });
    }
    return list;
}

bool Editor::isDocumentModified(int index) const
{
    if (index == m_currentDoc)
        return m_modified;
    return index >= 0 && index < int(m_docs.size()) && !m_docs[size_t(index)].undo.isClean()
        && !m_docs[size_t(index)].tileLink.active;
}

int Editor::documentForPath(const QString& path) const
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < int(m_docs.size()); ++i) {
        const QString p = i == m_currentDoc ? m_filePath : m_docs[size_t(i)].filePath;
        if (!p.isEmpty() && QFileInfo(p) == QFileInfo(absolute)) // case rules of the platform
            return i;
    }
    return -1;
}

// --- crash recovery ----------------------------------------------------------

RecoveryStore& Editor::recovery()
{
    if (!m_recovery) {
        // SMALTO_RECOVERY_DIR points it elsewhere (trying crashes out).
        QString root = m_recoveryRoot;
        if (root.isEmpty())
            root = qEnvironmentVariable("SMALTO_RECOVERY_DIR");
        if (root.isEmpty())
            root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recovery");
        m_recovery = std::make_unique<RecoveryStore>(root);
    }
    return *m_recovery;
}

void Editor::setAutosaveDelay(int seconds)
{
    seconds = std::clamp(seconds, 0, 3600);
    if (seconds == m_autosaveDelay)
        return;
    m_autosaveDelay = seconds;
    if (seconds == 0) {
        m_autosaveTimer.stop(); // pending changes are copied if it's turned on again
    } else if (m_autosavePending || std::any_of(m_docs.begin(), m_docs.end(),
                                                [](const DocumentSlot& s) { return s.autosavePending; })) {
        m_autosaveTimer.start(seconds * 1000);
    }
    emit preferencesChanged();
}

void Editor::setUndoMemory(int megabytes)
{
    megabytes = std::clamp(megabytes, 0, 16384);
    if (megabytes == m_undoMemory)
        return;
    m_undoMemory = megabytes;
    const size_t bytes = size_t(megabytes) * 1024 * 1024;
    m_undo.setByteLimit(bytes);
    for (DocumentSlot& slot : m_docs)
        slot.undo.setByteLimit(bytes);
    emit historyChanged(); // steps may have gone
    emit preferencesChanged();
}

void Editor::resetUndoMemory() { setUndoMemory(kUndoMemory); }

void Editor::resetNewGridSize() { setNewGridSize(kNewGridSize); }
void Editor::resetAutosaveDelay() { setAutosaveDelay(kAutosaveDelay); }

void Editor::setNewGridSize(QSize size)
{
    size = {std::clamp(size.width(), 1, 4096), std::clamp(size.height(), 1, 4096)};
    if (size == m_newGridSize)
        return;
    m_newGridSize = size;
    // The untouched startup sprite was made before the preference was read.
    if (m_placeholder && !m_undo.canUndo() && !m_undo.canRedo()) {
        m_doc->grid = {m_doc->grid.x, m_doc->grid.y, size.width(), size.height()};
        emit gridChanged();
    }
    emit preferencesChanged();
}

// Copies every document changed since its last autosave: unsaved ones are
// written, ones that are clean again lose their copy.
void Editor::autosaveNow()
{
    if (m_stroke.active || m_float.active) { // not mid-drag or mid-paste: soon
        m_autosaveTimer.start(2000);
        return;
    }
    if (m_autosavePending) {
        if (m_modified)
            recovery().save(m_recoveryId, *m_doc, m_filePath, fileName(), m_currentFrame, m_currentRow);
        else if (m_recovery)
            m_recovery->remove(m_recoveryId);
        m_autosavePending = false;
    }
    for (int i = 0; i < int(m_docs.size()); ++i) {
        DocumentSlot& slot = m_docs[size_t(i)];
        if (i == m_currentDoc || !slot.autosavePending || !slot.doc)
            continue;
        if (!slot.undo.isClean())
            recovery().save(slot.recoveryId, *slot.doc, slot.filePath, slotName(slot), slot.currentFrame, slot.currentRow);
        else if (m_recovery)
            m_recovery->remove(slot.recoveryId);
        slot.autosavePending = false;
    }
}

void Editor::waitForAutosave()
{
    if (m_recovery)
        m_recovery->wait();
}

QVariantList Editor::recoverableDocuments()
{
    m_recoverable = recovery().crashed();
    if (m_recoverable.empty())
        recovery().discardCrashed(); // crashed sessions with nothing in them
    QVariantList list;
    for (const auto& e : m_recoverable)
        list.append(QVariantMap{
            {"name", e.name},
            {"path", e.path},
            {"saved", QLocale().toString(e.saved.toLocalTime(), QLocale::ShortFormat)},
        });
    return list;
}

int Editor::restoreRecovered()
{
    int restored = 0;
    for (const auto& e : m_recoverable) {
        std::string error;
        auto doc = formats::loadAseprite(toFsPath(e.file), &error);
        if (!doc) {
            emit errorOccurred(tr("Couldn't restore %1: %2.").arg(e.name, QString::fromStdString(error)));
            continue;
        }
        // Back as it was: its own file (Save writes there) or its tab name,
        // and unsaved.
        setDocument(std::move(doc), e.path.isEmpty() ? QString() : QFileInfo(e.path).absoluteFilePath(), e.name);
        // Where the work was: the frame and the layer (kept in range if the copy is shorter).
        setCurrentFrame(e.frame);
        setCurrentLayer(e.layer);
        m_undo.setUnsaved();
        updateModified();
        m_autosavePending = true;
        ++restored;
    }
    m_recoverable.clear();
    recovery().discardCrashed();
    autosaveNow(); // into this session, at once
    return restored;
}

void Editor::discardRecovered()
{
    m_recoverable.clear();
    recovery().discardCrashed();
}

QString Editor::slotName(const DocumentSlot& slot) const
{
    if (!slot.filePath.isEmpty())
        return QFileInfo(slot.filePath).fileName();
    return slot.untitledName.isEmpty() ? tr("Untitled") : slot.untitledName;
}

// "Untitled", then "Untitled 2", ... skipping names other untitled tabs use.
QString Editor::nextUntitledName() const
{
    QStringList used;
    for (int i = 0; i < int(m_docs.size()); ++i) {
        const bool current = i == m_currentDoc;
        if (current && m_doc && m_filePath.isEmpty() && !(m_placeholder && !m_undo.canUndo()))
            used << fileName();
        else if (!current && m_docs[size_t(i)].filePath.isEmpty())
            used << slotName(m_docs[size_t(i)]);
    }
    QString name = tr("Untitled");
    for (int n = 2; used.contains(name); ++n)
        name = tr("Untitled %1").arg(n);
    return name;
}

// --- files ------------------------------------------------------------------

QString Editor::fileName() const
{
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).fileName();
    return m_untitledName.isEmpty() ? tr("Untitled") : m_untitledName;
}

QUrl Editor::fileUrl() const
{
    return m_filePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_filePath);
}

bool Editor::openFile(const QUrl& url)
{
    return openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

QString Editor::toLocalPath(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

namespace {

QString fromFsPath(const std::filesystem::path& path)
{
    const std::u8string s = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(s.data()), qsizetype(s.size()));
}


} // namespace

bool Editor::isImageFile(const QString& path) const
{
    return formats::isImageExtension(QFileInfo(path).suffix().toStdString());
}

QStringList Editor::imageSequence(const QString& path) const
{
    QStringList out;
    for (const auto& p : formats::findImageSequence(toFsPath(path)))
        out.append(fromFsPath(p));
    return out;
}

bool Editor::openImages(const QStringList& paths)
{
    std::vector<std::filesystem::path> files;
    for (const QString& p : paths)
        files.push_back(toFsPath(p));
    std::string error;
    auto doc = formats::loadImageDocument(files, &error);
    if (!doc) {
        emit errorOccurred(tr("Couldn't open %1: %2.")
                               .arg(paths.isEmpty() ? QString() : QFileInfo(paths.first()).fileName(),
                                    QString::fromStdString(error)));
        return false;
    }
    // "walk-01.png" ... -> "walk"
    QString name = QFileInfo(paths.first()).completeBaseName();
    if (paths.size() > 1) {
        while (!name.isEmpty() && name.back().isDigit())
            name.chop(1);
        while (!name.isEmpty() && QStringLiteral("-_ .").contains(name.back()))
            name.chop(1);
        if (name.isEmpty())
            name = QFileInfo(paths.first()).completeBaseName();
    }
    setDocument(std::move(doc), {}, name);
    return true;
}

bool Editor::importLayer(const QUrl& url)
{
    endStroke();
    commitFloating();
    const QString path = toLocalPath(url);
    std::string error;
    auto frames = formats::loadImageFrames(toFsPath(path), &error);
    if (!frames || frames->empty()) {
        emit errorOccurred(tr("Couldn't import %1: %2.")
                               .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return false;
    }
    if (frames->size() > 1) {
        std::vector<Image> images;
        std::vector<int> durations;
        for (auto& f : *frames) {
            images.push_back(std::move(f.image));
            durations.push_back(f.durationMs);
        }
        return importFrameLayer(std::move(images), durations, QFileInfo(path).completeBaseName().toStdString());
    }
    auto image = std::move(frames->front().image);
    auto layer = m_doc->makeImageLayer(QFileInfo(path).completeBaseName().toStdString());
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(convertToDocument(*m_doc, image));
    layer->cels[size_t(m_currentFrame)] = cel;
    insertLayerAboveCurrent(std::move(layer));

    // Straight into a transform; a bigger image starts fitted to the canvas,
    // centered.
    if (liftForTransform("Transform", false)) {
        Rect& box = m_float.box;
        const double fit = std::min(double(m_doc->width()) / box.w, double(m_doc->height()) / box.h);
        if (fit < 1) {
            const int w = std::max(1, int(std::lround(box.w * fit))), h = std::max(1, int(std::lround(box.h * fit)));
            box = {(m_doc->width() - w) / 2, (m_doc->height() - h) / 2, w, h};
            renderFloat();
        }
    }
    return true;
}

namespace {

// A layer of `source` as one for `target`: its cels in the target's color mode (through RGBA), its
// settings and, for a group, its children. Tilemap layers have no copy (nullptr), and a group none of
// whose children could be copied has none either.
std::unique_ptr<Layer> convertLayer(const Document& source, const Layer& layer, const Document& target)
{
    if (layer.kind == LayerKind::Tilemap)
        return nullptr;
    auto copy = layer.kind == LayerKind::Group ? target.makeGroupLayer(layer.name) : target.makeImageLayer(layer.name);
    copy->visible = layer.visible;
    copy->locked = layer.locked;
    copy->opacity = layer.opacity;
    copy->blend = layer.blend;
    copy->extraFlags = layer.extraFlags & uint16_t(~Layer::kBackgroundFlag);
    if (layer.kind == LayerKind::Group) {
        for (const auto& child : layer.children)
            if (auto c = convertLayer(source, *child, target)) {
                c->parent = copy.get();
                copy->children.push_back(std::move(c));
            }
        if (copy->children.empty() && !layer.children.empty())
            return nullptr;
        return copy;
    }
    copy->cels.assign(size_t(target.frameCount()), nullptr);
    const bool background = layer.isBackground();
    std::unordered_map<const Image*, std::shared_ptr<Image>> converted; // an image shared by cels stays shared
    for (size_t f = 0; f < layer.cels.size() && f < copy->cels.size(); ++f) {
        const CelRef& cel = layer.cels[f];
        if (!cel || !cel->image)
            continue;
        auto& image = converted[cel->image.get()];
        if (!image) {
            Image rgba(cel->image->width(), cel->image->height());
            for (int y = 0; y < rgba.height(); ++y)
                for (int x = 0; x < rgba.width(); ++x)
                    rgba.setPixel(x, y, pack(core::toRgba(source, *cel->image, x, y, background)));
            image = std::make_shared<Image>(convertToDocument(target, rgba));
        }
        auto made = std::make_shared<CelData>();
        made->image = image;
        made->position = cel->position;
        made->opacity = cel->opacity;
        made->zIndex = cel->zIndex;
        copy->cels[f] = std::move(made);
    }
    return copy;
}

} // namespace

bool Editor::copyLayers()
{
    endStroke();
    commitFloating();
    // Kept in an RGBA document of the same size and frames, so it needs nothing from this one.
    auto clipDoc = std::make_unique<Document>(m_doc->width(), m_doc->height(), ColorMode::Rgba);
    for (int f = 1; f < m_doc->frameCount(); ++f)
        clipDoc->insertFrame(f, Frame{});
    std::vector<std::unique_ptr<Layer>> clip;
    for (const Layer* layer : selectedLayers())
        if (auto copy = convertLayer(*m_doc, *layer, *clipDoc))
            clip.push_back(std::move(copy));
    if (clip.empty()) {
        emit errorOccurred(tr("Tilemap layers can't be copied: their tilesets belong to one document."));
        return false;
    }
    m_layerClipDoc = std::move(clipDoc);
    m_layerClip = std::move(clip);
    emit layerClipboardChanged();
    return true;
}

bool Editor::pasteLayers()
{
    if (m_layerClip.empty() || !m_layerClipDoc)
        return false;
    endStroke();
    commitFloating();
    std::vector<std::unique_ptr<Command>> parts;
    Layer& parent = m_current && m_current->parent ? *m_current->parent : m_doc->root();
    int index = m_current ? indexIn(parent, m_current) + 1 : int(parent.children.size());
    Layer* last = nullptr;
    for (const auto& layer : m_layerClip) {
        auto copy = convertLayer(*m_layerClipDoc, *layer, *m_doc);
        if (!copy)
            continue;
        last = copy.get();
        auto insert = std::make_unique<InsertLayerCommand>(parent, index++, std::move(copy));
        insert->redo(*m_doc);
        parts.push_back(std::move(insert));
    }
    if (parts.empty())
        return false;
    pushCommand(std::make_unique<CommandGroup>(m_layerClip.size() > 1 ? "Paste Layers" : "Paste Layer", std::move(parts)));
    emit historyChanged();
    for (Layer* p = &parent; p->parent; p = p->parent)
        p->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    m_current = last;
    setRange(NoRange, {});
    structureChanged();
    return true;
}

// A GIF's frames as the cels of one new layer, starting at the current frame.
bool Editor::importFrameLayer(std::vector<Image> images, const std::vector<int>& durations, const std::string& name)
{
    const int first = m_currentFrame;
    const int needed = first + int(images.size());
    std::vector<std::unique_ptr<Command>> parts;
    // Frames the sprite doesn't have yet, with the image's own delays.
    for (int f = m_doc->frameCount(); f < needed; ++f) {
        auto insert = std::make_unique<InsertFrameCommand>(f, Frame{durations[size_t(f - first)]});
        insert->redo(*m_doc);
        parts.push_back(std::move(insert));
    }
    auto layer = m_doc->makeImageLayer(name);
    layer->cels.resize(size_t(m_doc->frameCount()));
    for (size_t i = 0; i < images.size(); ++i) {
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(convertToDocument(*m_doc, images[i]));
        layer->cels[size_t(first) + i] = std::move(cel);
    }
    Layer& parent = m_current && m_current->parent ? *m_current->parent : m_doc->root();
    const int index = m_current ? indexIn(parent, m_current) + 1 : int(parent.children.size());
    Layer* added = layer.get();
    auto insertLayer = std::make_unique<InsertLayerCommand>(parent, index, std::move(layer));
    insertLayer->redo(*m_doc);
    parts.push_back(std::move(insertLayer));
    pushCommand(std::make_unique<CommandGroup>("Import Layer", std::move(parts)));
    emit historyChanged();
    for (Layer* p = &parent; p->parent; p = p->parent)
        p->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    m_current = added;
    structureChanged();
    return true;
}

bool Editor::openPath(const QString& path)
{
    if (const int open = documentForPath(path); open >= 0) {
        setCurrentDocument(open);
        return true;
    }
    if (isImageFile(path))
        return openImages({path});
    std::string error;
    auto doc = formats::loadAseprite(toFsPath(path), &error);
    if (!doc) {
        emit errorOccurred(tr("Couldn't open %1: %2.")
                               .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return false;
    }
    setDocument(std::move(doc), QFileInfo(path).absoluteFilePath());
    if (!staleLiveLayers().isEmpty())
        QMetaObject::invokeMethod(this, &Editor::staleLiveFound, Qt::QueuedConnection);
    return true;
}

bool Editor::saveFile()
{
    return !m_filePath.isEmpty() && saveTo(m_filePath);
}

bool Editor::saveFileAs(const QUrl& url)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("aseprite") && suffix != QLatin1String("ase"))
        path += QLatin1String(".aseprite");
    return saveTo(path);
}

bool Editor::saveTo(const QString& path)
{
    endStroke();
    commitFloating();
    std::string error;
    // The crossing point goes along when it isn't in the middle (a file without one opens centered).
    if (m_symmetryCenter2 != Point{m_doc->width(), m_doc->height()})
        m_doc->symmetryCenter2 = m_symmetryCenter2;
    const bool saved = formats::saveAseprite(*m_doc, toFsPath(path), &error);
    m_doc->symmetryCenter2.reset();
    if (!saved) {
        emit errorOccurred(tr("Couldn't save %1: %2.")
                               .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute != m_filePath) {
        m_filePath = absolute;
        emit fileChanged();
    }
    emit fileUsed(m_filePath);
    m_undo.setClean();
    if (m_recovery)
        m_recovery->remove(m_recoveryId);
    m_autosavePending = false;
    updateModified();
    return true;
}

// --- export -----------------------------------------------------------------

QVariantList Editor::tags() const
{
    QVariantList list;
    for (const Tag& tag : m_doc->tags)
        list.append(QVariantMap{
            {"name", QString::fromStdString(tag.name)},
            {"from", tag.from},
            {"to", tag.to},
            {"color", toQColor(tag.color)},
            {"direction", int(tag.direction)},
            {"repeat", tag.repeat},
        });
    return list;
}

namespace {

formats::SheetOptions exportOptions(const Document& doc, int currentFrame, const QVariantMap& map)
{
    formats::SheetOptions options;
    options.scale = std::clamp(map.value("scale", 1).toInt(), 1, 64);
    options.indexedPng = map.value("indexedPng").toBool();
    const QVariant range = map.value("range", "all");
    if (range == "current") {
        options.fromFrame = options.toFrame = currentFrame;
    } else if (range.typeId() != QMetaType::QString) {
        const int i = range.toInt();
        if (i >= 0 && i < int(doc.tags.size())) {
            options.fromFrame = doc.tags[size_t(i)].from;
            options.toFrame = doc.tags[size_t(i)].to;
        }
    }
    const QString layout = map.value("layout").toString();
    options.layout = layout == "vertical" ? formats::SheetLayout::Vertical
                   : layout == "grid"     ? formats::SheetLayout::Grid
                   : layout == "tags"     ? formats::SheetLayout::TagRows
                                          : formats::SheetLayout::Horizontal;
    return options;
}

} // namespace

bool Editor::exportFile(const QUrl& url, const QVariantMap& map)
{
    endStroke();
    commitFloating();
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const auto options = exportOptions(*m_doc, m_currentFrame, map);
    const QString format = map.value("format", "png").toString();
    std::string error;
    bool ok;
    if (format == "gif")
        ok = formats::exportGif(*m_doc, options, toFsPath(path), &error);
    else if (format == "sheet")
        ok = formats::exportSpriteSheet(*m_doc, options, toFsPath(path), &error);
    else
        ok = formats::exportPng(*m_doc, options, toFsPath(path), &error);
    if (!ok)
        emit errorOccurred(tr("Couldn't export %1: %2.")
                               .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
    return ok;
}

QVariantMap Editor::exportInfo(const QVariantMap& map) const
{
    auto options = exportOptions(*m_doc, m_currentFrame, map);
    const bool sheet = map.value("format").toString() == "sheet";
    if (!sheet)
        options.layout = formats::SheetLayout::Horizontal; // rows only matter for sheets
    const auto rows = formats::sheetRows(*m_doc, options);
    int frames = 0;
    size_t columns = 0;
    for (const auto& row : rows) {
        frames += int(row.size());
        columns = std::max(columns, row.size());
    }
    int w = m_doc->width() * options.scale;
    int h = m_doc->height() * options.scale;
    if (sheet) {
        w *= int(columns);
        h *= int(rows.size());
    }
    return {{"frames", frames}, {"rows", int(rows.size())}, {"width", w}, {"height", h}};
}


QVariantMap Editor::prepareExportPreview(const QVariantMap& map)
{
    auto options = exportOptions(*m_doc, m_currentFrame, map);
    options.scale = 1;
    const QString format = map.value("format", "png").toString();
    m_exportPreview = {};
    QVariantList durations;
    if (format == "sheet") {
        m_exportPreview.images.push_back(formats::buildSpriteSheet(*m_doc, options, {}).image);
        durations.append(0);
        // Where each frame sits on the sheet (the padding is 0 for now).
        QVariantList cells;
        const auto rows = formats::sheetRows(*m_doc, options);
        for (size_t r = 0; r < rows.size(); ++r)
            for (size_t c = 0; c < rows[r].size(); ++c)
                cells.append(QRectF(double(int(c) * (m_doc->width() + options.padding)),
                                    double(int(r) * (m_doc->height() + options.padding)), m_doc->width(),
                                    m_doc->height()));
        return {{"durations", durations}, {"cells", cells}};
    }
    options.layout = formats::SheetLayout::Horizontal;
    const auto rows = formats::sheetRows(*m_doc, options);
    for (const auto& row : rows)
        for (int f : row)
            m_exportPreview.frames.push_back(f);
    const qint64 pixels = qint64(m_exportPreview.frames.size()) * m_doc->width() * m_doc->height();
    if (format == "gif" && pixels <= kGifPreviewPixels) {
        auto decoded = formats::decodeImageFrames(formats::encodeGif(*m_doc, options));
        if (decoded && decoded->size() == m_exportPreview.frames.size()) {
            for (auto& frame : *decoded) {
                durations.append(frame.durationMs);
                m_exportPreview.images.push_back(std::move(frame.image));
            }
            m_exportPreview.frames.clear();
            return {{"durations", durations}};
        }
    }
    for (int f : m_exportPreview.frames)
        durations.append(m_doc->frame(f).durationMs);
    return {{"durations", durations}};
}

QImage Editor::exportPreviewFrame(int index) const
{
    const auto toQImage = [](const Image& image) {
        return QImage(image.data(), image.width(), image.height(), image.stride(), QImage::Format_RGBA8888).copy();
    };
    if (index >= 0 && index < int(m_exportPreview.images.size()))
        return toQImage(m_exportPreview.images[size_t(index)]);
    if (index >= 0 && index < int(m_exportPreview.frames.size()))
        return toQImage(formats::renderScaled(*m_doc, m_exportPreview.frames[size_t(index)], 1));
    return {};
}

QImage Editor::frameImage(int frame) const
{
    if (frame < 0 || frame >= m_doc->frameCount())
        return {};
    // The canvas shows a Smear group as drawn while its art is painted; the frame itself is posed.
    bool asDrawn = false;
    m_doc->forEachLayer([&](const Layer& l) { asDrawn = asDrawn || (l.isSmear() && l.smearAsDrawn); });
    if (frame == m_currentFrame && !asDrawn)
        return composite().copy();
    if (m_previewCache.size() != size_t(m_doc->frameCount()))
        m_previewCache.assign(size_t(m_doc->frameCount()), QImage());
    QImage& cached = m_previewCache[size_t(frame)];
    if (!cached.isNull() && frame != m_currentFrame)
        return cached; // shared, not copied
    const Image image = formats::renderScaled(*m_doc, frame, 1);
    QImage result = QImage(image.data(), image.width(), image.height(), image.stride(), QImage::Format_RGBA8888).copy();
    if (frame != m_currentFrame && m_previewCacheBytes + size_t(result.sizeInBytes()) <= kFrameCacheLimit) {
        cached = result;
        m_previewCacheBytes += size_t(result.sizeInBytes());
    }
    return result;
}

int Editor::frameDurationAt(int frame) const
{
    return frame >= 0 && frame < m_doc->frameCount() ? m_doc->frame(frame).durationMs : 100;
}

void Editor::updateModified()
{
    const bool modified = !m_undo.isClean() && !m_tileLink.active;
    if (m_modified == modified)
        return;
    m_modified = modified;
    emit modifiedChanged();
}

// --- properties -------------------------------------------------------------

bool Editor::toolUsableOnLive(int tool)
{
    return tool == Eyedropper || tool == Hand || tool == Move || tool == SourceTool || tool == FieldTool;
}

bool Editor::toolUsableOnSmear(int tool)
{
    return tool == PoseTool || tool == Move || tool == Eyedropper || tool == Hand;
}

// A live layer became current (or stopped being): put a tool that works on it in hand, and give the
// old one back afterwards unless another was chosen in the meantime.
bool Editor::lockedToPan() const
{
    return m_playing && !(liveLayer() && (m_tool == Move || m_tool == Eyedropper));
}

void Editor::syncLiveTool()
{
    const bool smear = m_current && m_current->isSmear();
    const auto ownTool = [](int tool) { return tool == SourceTool || tool == FieldTool || tool == PoseTool; };
    if (liveLayer() || smear) {
        if (liveLayer() ? !toolUsableOnLive(m_tool) : !toolUsableOnSmear(m_tool)) {
            const int before = m_tool;
            const int kept = m_toolBeforeLive; // from one live layer straight to another: the first one's
            setTool(liveLayer() ? Move : PoseTool);
            m_toolBeforeLive = ownTool(before) && kept >= 0 ? kept : before;
        }
    } else {
        // Off a live layer its own tools have nothing to work on: the old one comes back, else the pencil.
        const bool liveOnly = ownTool(m_tool);
        const int before = m_toolBeforeLive;
        m_toolBeforeLive = -1;
        if (before >= 0 && (m_tool == Move || liveOnly))
            setTool(Tool(before));
        else if (liveOnly)
            setTool(Pencil);
    }
}

void Editor::setTool(Tool tool)
{
    const bool smear = m_current && m_current->isSmear();
    if (m_tool == tool || (liveLayer() && !toolUsableOnLive(tool)) || (smear && !toolUsableOnSmear(tool)))
        return;
    if ((tool == SourceTool || tool == FieldTool) && !liveLayer())
        return; // they work on a live layer only
    if (tool == PoseTool && !smear)
        return; // a Smear group's only
    if (tool != SourceTool && tool != FieldTool && tool != PoseTool)
        m_toolBeforeLive = -1; // chosen, not forced
    endStroke();
    cancelPolygon();
    if (tool != Move && tool != Select)
        commitFloating();
    if (m_text.active && !m_float.active) { // a text clicked but not typed yet
        m_text = {};
        emit textChanged();
    }
    m_tool = tool;
    emit toolChanged();
    checkGradient();
}

void Editor::setBrushShape(BrushShape shape)
{
    if (m_brushShape == shape || (shape == CustomBrush && !hasCustomBrush()))
        return;
    m_brushShape = shape;
    emit brushSizeChanged(); // the preview's shape follows
}

void Editor::setShading(bool on)
{
    if (m_shading == on)
        return;
    m_shading = on;
    emit toolOptionsChanged();
}

void Editor::setShadeRange(QPoint range)
{
    if (range.x() < 0 || range.y() < 0)
        range = {-1, -1};
    else if (range.x() > range.y())
        range = {range.y(), range.x()};
    if (m_shadeRange == range)
        return;
    m_shadeRange = range;
    emit shadeRangeChanged();
}

void Editor::setSymmetryRising(bool on)
{
    if (m_symmetryRising == on)
        return;
    m_symmetryRising = on;
    fitSymmetryCenter();
    emit toolOptionsChanged();
}

void Editor::setSymmetryFalling(bool on)
{
    if (m_symmetryFalling == on)
        return;
    m_symmetryFalling = on;
    fitSymmetryCenter();
    emit toolOptionsChanged();
}

void Editor::setSymmetryRadial(int copies)
{
    copies = copies < 2 ? 0 : std::min(copies, 12);
    if (m_symmetryRadial == copies)
        return;
    m_symmetryRadial = copies;
    emit toolOptionsChanged();
}

void Editor::setSymmetryRadialMirror(bool on)
{
    if (m_symmetryRadialMirror == on)
        return;
    m_symmetryRadialMirror = on;
    emit toolOptionsChanged();
}

void Editor::setSymmetryRadialTurn(bool on)
{
    if (m_symmetryRadialTurn == on)
        return;
    m_symmetryRadialTurn = on;
    emit toolOptionsChanged();
}

void Editor::setSymmetryCenter(QPointF center)
{
    Point c2{std::clamp(int(std::lround(center.x() * 2)), 0, 2 * m_doc->width()),
             std::clamp(int(std::lround(center.y() * 2)), 0, 2 * m_doc->height())};
    // Moving one coordinate: it is the one that gives way to the diagonals.
    const bool xMoved = c2.x != m_symmetryCenter2.x && c2.y == m_symmetryCenter2.y;
    c2 = fittedForDiagonals(c2, xMoved, xMoved ? c2.x - m_symmetryCenter2.x : c2.y - m_symmetryCenter2.y);
    if (c2 == m_symmetryCenter2)
        return;
    m_symmetryCenter2 = c2;
    emit symmetryCenterChanged();
    refreshOverlay(); // the Shift-line preview mirrors around it
}

// With a diagonal axis on, the crossing point's x and y (in half pixels) must have the same parity, or the
// mirrored pixels would land between pixels. The coordinate that gives way moves one half pixel on, in
// `direction`'s sign (up when it is 0).
Point Editor::fittedForDiagonals(Point c2, bool changeX, int direction) const
{
    if (!(m_symmetryRising || m_symmetryFalling) || !((c2.x ^ c2.y) & 1))
        return c2;
    int& v = changeX ? c2.x : c2.y;
    const int max = 2 * (changeX ? m_doc->width() : m_doc->height());
    v += direction < 0 ? -1 : 1;
    if (v < 0 || v > max)
        v += v < 0 ? 2 : -2;
    return c2;
}

void Editor::fitSymmetryCenter()
{
    const Point c2 = fittedForDiagonals(m_symmetryCenter2, false, 0);
    if (c2 == m_symmetryCenter2)
        return;
    m_symmetryCenter2 = c2;
    emit symmetryCenterChanged();
    refreshOverlay();
}

void Editor::centerSymmetry()
{
    setSymmetryCenter({m_doc->width() / 2.0, m_doc->height() / 2.0});
}

void Editor::setSymmetryHorizontal(bool on)
{
    if (m_symmetryH == on)
        return;
    m_symmetryH = on;
    emit toolOptionsChanged();
}

QVariantList Editor::slices() const
{
    QVariantList list;
    for (int i = 0; i < int(m_doc->slices.size()); ++i) {
        const Slice& s = m_doc->slices[size_t(i)];
        const SliceKey* k = s.keyAt(m_currentFrame);
        if (!k || k->bounds.isEmpty())
            continue;
        list.append(QVariantMap{
            {"index", i},
            {"name", QString::fromStdString(s.name)},
            {"color", toQColor(s.color)},
            {"bounds", QRect(k->bounds.x, k->bounds.y, k->bounds.w, k->bounds.h)},
            {"nineSlice", s.nineSlice},
            {"center", QRect(k->center.x, k->center.y, k->center.w, k->center.h)},
            {"hasPivot", s.hasPivot},
            {"pivot", QPoint(k->pivot.x, k->pivot.y)},
            {"keyFrame", k->frame}, // the frame its key in effect starts at
            {"keys", int(s.keys.size())},
        });
    }
    return list;
}

const Tileset* Editor::currentTileset() const
{
    if (!m_current || m_current->kind != LayerKind::Tilemap || m_current->tilesetIndex >= m_doc->tilesets.size())
        return nullptr;
    const Tileset& set = m_doc->tilesets[m_current->tilesetIndex];
    return set.tiles.empty() ? nullptr : &set;
}

void Editor::bumpTileset()
{
    ++m_tilesetRevision;
    if (const Tileset* set = currentTileset())
        m_currentTile = std::clamp(m_currentTile, 0, int(set->tiles.size()) - 1);
    emit tilesetChanged();
}

void Editor::setEditTiles(bool on)
{
    if (m_editTiles == on)
        return;
    m_editTiles = on;
    emit toolOptionsChanged();
}

QString Editor::tilesetName() const
{
    const Tileset* set = currentTileset();
    return set ? QString::fromStdString(set->name) : QString();
}

QSize Editor::tileSize() const
{
    const Tileset* set = currentTileset();
    return set ? QSize(set->tileWidth, set->tileHeight) : QSize();
}

int Editor::tileBaseIndex() const
{
    const Tileset* set = currentTileset();
    return set ? set->baseIndex : 1;
}

int Editor::tileCount() const
{
    const Tileset* set = currentTileset();
    return set ? int(set->tiles.size()) : 0;
}

void Editor::setCurrentTile(int index)
{
    const int count = tileCount();
    index = count > 0 ? std::clamp(index, 0, count - 1) : 0;
    clearTileStamp();
    if (m_currentTile == index)
        return;
    m_currentTile = index;
    emit tilesetChanged();
}

QImage Editor::tileImage(int index) const
{
    const Tileset* set = currentTileset();
    if (!set || index < 0 || index >= int(set->tiles.size()) || !set->tiles[size_t(index)])
        return {};
    const Image& tile = *set->tiles[size_t(index)];
    QImage out(tile.width(), tile.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < tile.height(); ++y)
        for (int x = 0; x < tile.width(); ++x)
            out.setPixelColor(x, y, toQColor(core::toRgba(*m_doc, tile, x, y)));
    return out;
}

void Editor::setTileEditMode(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (m_tileEditMode == mode)
        return;
    m_tileEditMode = mode;
    emit toolOptionsChanged();
}

std::shared_ptr<Image> Editor::beginTilePixels(Point& origin, bool& created)
{
    m_stroke = {};
    auto map = editableTilemap(origin, &created);
    if (!map)
        return nullptr;
    const size_t id = m_current->tilesetIndex;
    Tileset& live = m_doc->tilesets[id];
    m_stroke.map = map;
    m_stroke.mapBefore = *map;
    m_stroke.tilesetId = id;
    m_stroke.tilesetBefore = live;
    m_stroke.tilesAtStart = live.tiles.size();
    // Copy on write: the tiles an undo step holds are never changed.
    for (auto& tile : live.tiles)
        tile = tile ? std::make_shared<Image>(*tile) : nullptr;
    return std::make_shared<Image>(flattenTilemap(*map, live, m_doc->pixelFormat(), transparentValue()));
}

void Editor::retile(Rect area)
{
    Tileset& set = m_doc->tilesets[m_stroke.tilesetId];
    Image& map = *m_stroke.map;
    Image& shown = *m_stroke.image;
    const int tw = set.tileWidth, th = set.tileHeight;
    const uint32_t blank = transparentValue();
    area = area.intersected(shown.bounds());
    if (area.isEmpty())
        return;
    std::vector<uint32_t> edited; // tiles whose pixels changed: their other cells redraw
    for (int cy = area.y / th; cy <= (area.bottom() - 1) / th; ++cy)
        for (int cx = area.x / tw; cx <= (area.right() - 1) / tw; ++cx) {
            if (!map.bounds().contains(cx, cy))
                continue;
            const Rect cell{cx * tw, cy * th, tw, th};
            const uint32_t v = map.pixel(cx, cy);
            uint32_t id = v & kTileIdMask;
            const uint32_t flips = v & ~kTileIdMask;
            const bool known = id > 0 && id < set.tiles.size() && set.tiles[id];
            Image pixels = unorientTile(shown.copy(cell), flips);
            const bool empty = isBlank(pixels, blank);
            const auto setTile = [&](uint32_t tile) {
                if (*set.tiles[tile] == pixels)
                    return;
                *set.tiles[tile] = pixels;
                edited.push_back(tile);
            };
            const auto addTile = [&] {
                set.tiles.push_back(std::make_shared<Image>(pixels));
                return uint32_t(set.tiles.size() - 1);
            };
            if (m_tileEditMode == 2) { // Stack: a new tile for each painted cell
                const int key = cy * map.width() + cx;
                if (const auto it = m_stroke.stackTiles.find(key); it != m_stroke.stackTiles.end())
                    setTile(it->second);
                else if (!(empty && !known))
                    map.setPixel(cx, cy, (m_stroke.stackTiles[key] = addTile()) | flips);
                continue;
            }
            if (known) {
                // A tile this stroke made that's empty again goes back to the empty cell.
                if (m_tileEditMode == 1 && empty && id >= m_stroke.tilesAtStart)
                    map.setPixel(cx, cy, 0);
                else
                    setTile(id);
                continue;
            }
            if (m_tileEditMode == 0) { // Manual: empty cells stay empty
                shown.fillRect(cell, blank);
                continue;
            }
            if (empty)
                continue;
            // Auto on an empty cell: a tile that looks the same, else a new one.
            id = 0;
            for (uint32_t k = 1; k < set.tiles.size() && !id; ++k)
                if (set.tiles[k] && *set.tiles[k] == pixels)
                    id = k;
            map.setPixel(cx, cy, id ? id : addTile());
        }
    if (edited.empty())
        return;
    // Every other cell showing an edited tile shows the change too.
    for (int cy = 0; cy < map.height(); ++cy)
        for (int cx = 0; cx < map.width(); ++cx) {
            const uint32_t v = map.pixel(cx, cy);
            if (std::find(edited.begin(), edited.end(), v & kTileIdMask) == edited.end())
                continue;
            const uint32_t flips = v & ~kTileIdMask;
            shown.blit(flips ? orientTile(*set.tiles[v & kTileIdMask], flips) : *set.tiles[v & kTileIdMask],
                       {cx * tw, cy * th});
            invalidate({m_stroke.origin.x + cx * tw, m_stroke.origin.y + cy * th, tw, th});
        }
    ++m_tilesetRevision;
    emit tilesetChanged();
}

void Editor::finishTileStroke()
{
    const size_t id = m_stroke.tilesetId;
    Tileset& live = m_doc->tilesets[id];
    // Tiles the stroke added that no cel uses any more (e.g. where a shape
    // was dragged past) go, from the end.
    std::vector<bool> used(live.tiles.size(), false);
    m_doc->forEachLayer([&](const Layer& layer) {
        if (layer.kind != LayerKind::Tilemap || layer.tilesetIndex != id)
            return;
        for (const CelRef& cel : layer.cels)
            if (cel && cel->image)
                for (int y = 0; y < cel->image->height(); ++y)
                    for (int x = 0; x < cel->image->width(); ++x)
                        if (const uint32_t t = cel->image->pixel(x, y) & kTileIdMask; t < used.size())
                            used[t] = true;
    });
    while (live.tiles.size() > m_stroke.tilesAtStart && !used[live.tiles.size() - 1])
        live.tiles.pop_back();

    Rect mapChanged;
    const Image& map = *m_stroke.map;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.pixel(x, y) != m_stroke.mapBefore.pixel(x, y))
                mapChanged = mapChanged.united({x, y, 1, 1});
    bool tilesChanged = live.tiles.size() != m_stroke.tilesetBefore.tiles.size();
    for (size_t i = 0; !tilesChanged && i < live.tiles.size(); ++i)
        tilesChanged = *live.tiles[i] != *m_stroke.tilesetBefore.tiles[i];
    if (!tilesChanged && mapChanged.isEmpty()) {
        live = m_stroke.tilesetBefore; // nothing to keep
        if (m_stroke.createdCel)
            dropCreatedCel();
        return;
    }
    std::vector<std::unique_ptr<Command>> parts;
    if (m_stroke.createdCel)
        parts.push_back(std::make_unique<SetCelCommand>(*m_current, m_currentFrame, nullptr,
                                                        m_current->cels[size_t(m_currentFrame)]));
    if (tilesChanged)
        parts.push_back(std::make_unique<SetTilesetCommand>(id, m_stroke.tilesetBefore, live, "Tiles"));
    if (!mapChanged.isEmpty())
        parts.push_back(std::make_unique<ImagePatchCommand>(m_stroke.map, Point{mapChanged.x, mapChanged.y},
                                                            m_stroke.mapBefore.copy(mapChanged),
                                                            m_stroke.map->copy(mapChanged), "Tilemap"));
    pushCommand(std::make_unique<CommandGroup>(m_stroke.label.toStdString(), std::move(parts)));
    emit historyChanged();
    bumpTileset();
}

void Editor::setTileFlips(int flips)
{
    const uint32_t bits = uint32_t(flips & 7) << 29;
    if (m_tileFlips == bits)
        return;
    // A picked block turns with the buttons.
    flipStamp(bits ^ m_tileFlips);
    m_tileFlips = bits;
    emit toolOptionsChanged();
}

void Editor::flipStamp(uint32_t bits)
{
    if (m_stampW * m_stampH < 2)
        return;
    const Tileset* set = currentTileset();
    const bool square = set && set->tileWidth == set->tileHeight;
    std::vector<uint32_t> cells = m_stamp;
    int w = m_stampW, h = m_stampH;
    const auto empty = [](uint32_t v) { return (v & kTileIdMask) == 0; };
    // A mirrored image is the tile with its x (or y) flip toggled, whatever else is set.
    if (bits & kTileFlipX) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w / 2 + (w & 1); ++x)
                std::swap(cells[size_t(y * w + x)], cells[size_t(y * w + (w - 1 - x))]);
        for (uint32_t& v : cells)
            if (!empty(v))
                v ^= kTileFlipX;
    }
    if (bits & kTileFlipY) {
        for (int y = 0; y < h / 2 + (h & 1); ++y)
            for (int x = 0; x < w; ++x)
                std::swap(cells[size_t(y * w + x)], cells[size_t((h - 1 - y) * w + x)]);
        for (uint32_t& v : cells)
            if (!empty(v))
                v ^= kTileFlipY;
    }
    // Turned (x and y swapped): the block is transposed; each tile's diagonal flag toggles and
    // its x and y flips trade places (only for square tiles, the only ones that can turn).
    if ((bits & kTileFlipDiagonal) && square) {
        std::vector<uint32_t> t(cells.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint32_t v = cells[size_t(y * w + x)];
                if (!empty(v)) {
                    const bool fx = v & kTileFlipX, fy = v & kTileFlipY;
                    v = (v & kTileIdMask) | ((v & kTileFlipDiagonal) ^ kTileFlipDiagonal) | (fy ? kTileFlipX : 0)
                        | (fx ? kTileFlipY : 0);
                }
                t[size_t(x * h + y)] = v;
            }
        cells = std::move(t);
        std::swap(w, h);
    }
    m_stamp = std::move(cells);
    m_stampW = w;
    m_stampH = h;
    emit tileCursorChanged();
}

QVariantList Editor::tileStampIds() const
{
    QVariantList out;
    std::vector<uint32_t> seen;
    if (m_stampW * m_stampH > 1)
        for (const uint32_t v : m_stamp) {
            const uint32_t id = v & kTileIdMask;
            if (id != 0 && std::find(seen.begin(), seen.end(), id) == seen.end()) {
                seen.push_back(id);
                out.append(int(id));
            }
        }
    return out;
}

Editor::CellList Editor::selectedCells(const Image& map, Point origin) const
{
    CellList out;
    const Tileset* set = currentTileset();
    if (!set || m_doc->selection.isEmpty())
        return out;
    const Rect sel = m_doc->selection.bounds();
    const int tw = std::max(1, set->tileWidth), th = std::max(1, set->tileHeight);
    const int x0 = std::max(0, floorDiv(sel.x - origin.x, tw)), y0 = std::max(0, floorDiv(sel.y - origin.y, th));
    const int x1 = std::min(map.width() - 1, floorDiv(sel.right() - 1 - origin.x, tw));
    const int y1 = std::min(map.height() - 1, floorDiv(sel.bottom() - 1 - origin.y, th));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (cellTouchesSelection({x, y}, origin, *set))
                out.push_back({{x, y}, map.pixel(x, y)});
    return out;
}

Point Editor::clampCellOffset(const CellList& cells, Point offset, const Image& map)
{
    if (cells.empty())
        return {0, 0};
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (const auto& c : cells) {
        minX = std::min(minX, c.first.x);
        minY = std::min(minY, c.first.y);
        maxX = std::max(maxX, c.first.x);
        maxY = std::max(maxY, c.first.y);
    }
    return {std::clamp(offset.x, -minX, map.width() - 1 - maxX), std::clamp(offset.y, -minY, map.height() - 1 - maxY)};
}

Rect Editor::placeCells(Image& map, const CellList& cells, Point offset, bool copy)
{
    Rect changed;
    if (!copy)
        for (const auto& c : cells)
            if (map.pixel(c.first.x, c.first.y) != 0) {
                map.setPixel(c.first.x, c.first.y, 0);
                changed = changed.united({c.first.x, c.first.y, 1, 1});
            }
    for (const auto& c : cells) {
        if ((c.second & kTileIdMask) == 0)
            continue; // an empty cell of the block leaves what is there
        const Point d{c.first.x + offset.x, c.first.y + offset.y};
        if (!map.bounds().contains(d.x, d.y))
            continue;
        map.setPixel(d.x, d.y, c.second);
        changed = changed.united({d.x, d.y, 1, 1});
    }
    return changed;
}

// A press on the selected cells: lift them to be dragged a whole cell at a time. They land as the
// drag goes (and let go as one undo step); with Alt the originals stay.
void Editor::beginMoveCells(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    const Tileset* set = currentTileset();
    if (!set || m_doc->selection.isEmpty() || !m_current)
        return;
    const CelRef existing = m_current->cel(m_currentFrame);
    if (!existing || !existing->image)
        return; // no cells placed on this frame
    Point origin;
    bool created = false;
    auto map = editableTilemap(origin, &created);
    if (!map)
        return;
    CellList cells = selectedCells(*map, origin);
    if (cells.empty())
        return;
    m_stroke = {};
    m_stroke.active = true;
    m_stroke.kind = Stroke::MoveCells;
    m_stroke.image = map;
    m_stroke.before = *map;
    m_stroke.origin = origin;
    m_stroke.cells = std::move(cells);
    m_stroke.copyCells = modifiers & Qt::AltModifier;
    m_stroke.selectionStart = m_doc->selection;
    m_stroke.label = m_stroke.copyCells ? QStringLiteral("Copy Cells") : QStringLiteral("Move Cells");
    m_stroke.start = m_stroke.last = {pixel.x() - origin.x, pixel.y() - origin.y};
}

void Editor::updateMoveCells()
{
    const Tileset* set = currentTileset();
    if (!set)
        return;
    Image& map = *m_stroke.image;
    const int tw = std::max(1, set->tileWidth), th = std::max(1, set->tileHeight);
    // The nearest whole cell to where the pointer has dragged.
    const Point raw{floorDiv(m_stroke.last.x - m_stroke.start.x + tw / 2, tw),
                    floorDiv(m_stroke.last.y - m_stroke.start.y + th / 2, th)};
    const Point offset = clampCellOffset(m_stroke.cells, raw, map);
    if (offset == m_stroke.cellOffset && !m_stroke.drawn.isEmpty())
        return;
    const Rect previous = m_stroke.drawn;
    if (!previous.isEmpty())
        map.blit(m_stroke.before.copy(previous), {previous.x, previous.y});
    // Everything this step can touch: where the cells were and where they go.
    Rect source;
    for (const auto& c : m_stroke.cells)
        source = source.united({c.first.x, c.first.y, 1, 1});
    const Rect drawn = source.united(source.translated(offset.x, offset.y));
    map.blit(m_stroke.before.copy(drawn), {drawn.x, drawn.y});
    placeCells(map, m_stroke.cells, offset, m_stroke.copyCells);
    m_stroke.cellOffset = offset;
    m_stroke.drawn = drawn;
    m_stroke.bounds = m_stroke.bounds.united(drawn).united(previous);
    const Rect touched = previous.united(drawn);
    invalidate({m_stroke.origin.x + touched.x * tw, m_stroke.origin.y + touched.y * th, touched.w * tw, touched.h * th});
    setSelection(m_stroke.selectionStart.translated(offset.x * tw, offset.y * th));
}

bool Editor::nudgeCells(int dx, int dy)
{
    const Tileset* set = currentTileset();
    if (!set || m_doc->selection.isEmpty() || !m_current)
        return false;
    const int tw = std::max(1, set->tileWidth), th = std::max(1, set->tileHeight);
    const CelRef existing = m_current->cel(m_currentFrame);
    Point origin;
    std::shared_ptr<Image> map;
    if (existing && existing->image)
        map = editableTilemap(origin, nullptr);
    CellList cells = map ? selectedCells(*map, origin) : CellList{};
    if (cells.empty()) { // nothing there to carry: the selection alone moves
        setSelection(m_doc->selection.translated(dx * tw, dy * th));
        recordSelection("Move Selection", continuing(QStringLiteral("nudge")));
        return true;
    }
    const Point offset = clampCellOffset(cells, {dx, dy}, *map);
    if (offset == Point{0, 0})
        return true; // at the edge of the map
    const Image before = *map;
    const Rect changed = placeCells(*map, cells, offset, false);
    setSelection(m_doc->selection.translated(offset.x * tw, offset.y * th));
    if (!changed.isEmpty())
        invalidate({origin.x + changed.x * tw, origin.y + changed.y * th, changed.w * tw, changed.h * th});
    pushPixelEdit(map, changed, before, "Move Cells", false);
    return true;
}

void Editor::setTileStamp(int columns, const QVariantList& ids)
{
    columns = std::max(1, columns);
    const Tileset* set = currentTileset();
    if (!set || ids.isEmpty())
        return;
    const int count = int(ids.size());
    if (count == 1 || columns == 1 && count == 1) {
        clearTileStamp();
        setCurrentTile(ids[0].toInt());
        return;
    }
    const int w = std::min(columns, count), h = (count + w - 1) / w;
    std::vector<uint32_t> cells(size_t(w * h), 0);
    for (int i = 0; i < count; ++i) {
        const int id = ids[i].toInt();
        cells[size_t(i)] = id > 0 && id < int(set->tiles.size()) ? uint32_t(id) : 0;
    }
    m_stamp = std::move(cells);
    m_stampW = w;
    m_stampH = h;
    flipStamp(m_tileFlips); // as the buttons show it
    emit tileCursorChanged();
}

bool Editor::cellTouchesSelection(Point cell, Point origin, const Tileset& set) const
{
    const Mask& sel = m_doc->selection;
    if (sel.isEmpty())
        return true;
    const Rect rect{origin.x + cell.x * set.tileWidth, origin.y + cell.y * set.tileHeight, set.tileWidth, set.tileHeight};
    const Rect box = rect.intersected(sel.bounds());
    for (int y = box.y; y < box.bottom(); ++y)
        for (int x = box.x; x < box.right(); ++x)
            if (sel.contains(x, y))
                return true;
    return false;
}

QRect Editor::tileCellAt(QPoint pixel) const
{
    const Tileset* set = currentTileset();
    if (!set)
        return {};
    const CelRef cel = m_current->cel(m_currentFrame);
    const Point origin = cel ? cel->position : Point{};
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int cx = floorDiv(pixel.x() - origin.x, set->tileWidth), cy = floorDiv(pixel.y() - origin.y, set->tileHeight);
    return {origin.x + cx * set->tileWidth, origin.y + cy * set->tileHeight, set->tileWidth, set->tileHeight};
}

std::shared_ptr<Image> Editor::editableTilemap(Point& origin, bool* created)
{
    if (created)
        *created = false;
    const Tileset* set = currentTileset();
    if (!set || !effectivelyVisible(*m_current) || effectivelyLocked(*m_current))
        return nullptr;
    const int tw = set->tileWidth, th = set->tileHeight;
    CelRef& cel = m_current->cels[size_t(m_currentFrame)];
    if (!cel) {
        cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>((m_doc->width() + tw - 1) / tw, (m_doc->height() + th - 1) / th,
                                             PixelFormat::Tilemap32);
        cel->image->clear(0);
        if (created)
            *created = true;
        emit celsChanged();
    } else {
        // Whole cells, so the grid stays where it is: grow to cover the canvas.
        const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        const Point p = cel->position;
        const int x0 = std::min(0, floorDiv(-p.x, tw)), y0 = std::min(0, floorDiv(-p.y, th));
        const int x1 = std::max(cel->image->width(), floorDiv(m_doc->width() - p.x + tw - 1, tw));
        const int y1 = std::max(cel->image->height(), floorDiv(m_doc->height() - p.y + th - 1, th));
        if (x0 < 0 || y0 < 0 || x1 > cel->image->width() || y1 > cel->image->height()) {
            auto grown = std::make_shared<Image>(x1 - x0, y1 - y0, PixelFormat::Tilemap32);
            grown->clear(0);
            grown->blit(*cel->image, {-x0, -y0});
            cel->image = grown;
            cel->position = {p.x + x0 * tw, p.y + y0 * th};
        }
    }
    origin = cel->position;
    return cel->image;
}

void Editor::stampTile(Point pixel)
{
    const Tileset* set = currentTileset();
    if (!set)
        return;
    Image& map = *m_stroke.image;
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int cx = floorDiv(pixel.x - m_stroke.origin.x, set->tileWidth);
    const int cy = floorDiv(pixel.y - m_stroke.origin.y, set->tileHeight);
    if (!map.bounds().contains(cx, cy))
        return;
    if (m_stroke.useStamp) {
        // A picked block, its top left cell here.
        for (int j = 0; j < m_stampH; ++j)
            for (int i = 0; i < m_stampW; ++i) {
                const uint32_t v = m_stamp[size_t(j * m_stampW + i)];
                if (map.bounds().contains(cx + i, cy + j) && map.pixel(cx + i, cy + j) != v) {
                    map.setPixel(cx + i, cy + j, v);
                    m_stroke.bounds = m_stroke.bounds.united({cx + i, cy + j, 1, 1});
                }
            }
        invalidate({m_stroke.origin.x + cx * set->tileWidth, m_stroke.origin.y + cy * set->tileHeight,
                    m_stampW * set->tileWidth, m_stampH * set->tileHeight});
        return;
    }
    const uint32_t value = m_stroke.value;
    // Pixel perfect: a freehand run of cells keeps its diagonals one cell thin. As with pixels,
    // the middle cell of an L-shaped corner goes back to what it was before the stroke.
    if (m_pixelPerfect && (!m_stroke.lastPushed || *m_stroke.lastPushed != Point{cx, cy})) {
        const auto key = [](Point p) { return uint64_t(uint32_t(p.x)) << 32 | uint32_t(p.y); };
        const int step = ++m_stroke.step;
        const int cornerStep = m_stroke.lastStep;
        m_stroke.lastPushed = Point{cx, cy};
        m_stroke.lastStep = step;
        const auto corner = m_stroke.filter.push({cx, cy});
        if (corner && map.bounds().contains(*corner) && m_stroke.firstPainted[key(*corner)] == cornerStep) {
            map.setPixel(corner->x, corner->y, m_stroke.before.pixel(corner->x, corner->y));
            invalidate({m_stroke.origin.x + corner->x * set->tileWidth, m_stroke.origin.y + corner->y * set->tileHeight,
                        set->tileWidth, set->tileHeight});
            m_stroke.firstPainted.erase(key(*corner)); // may be painted again later
        }
        m_stroke.firstPainted.emplace(key({cx, cy}), step);
    }
    if (map.pixel(cx, cy) == value)
        return;
    map.setPixel(cx, cy, value);
    m_stroke.bounds = m_stroke.bounds.united({cx, cy, 1, 1});
    invalidate({m_stroke.origin.x + cx * set->tileWidth, m_stroke.origin.y + cy * set->tileHeight, set->tileWidth,
                set->tileHeight});
}

void Editor::clearTileStamp()
{
    if (m_stampW == 0 && m_stampH == 0)
        return;
    m_stamp.clear();
    m_stampW = m_stampH = 0;
    emit tileCursorChanged();
}

QImage Editor::tileCursorImage() const
{
    const Tileset* set = currentTileset();
    if (!set)
        return {};
    const bool stamp = m_stampW * m_stampH > 1;
    const int cols = stamp ? m_stampW : 1, rows = stamp ? m_stampH : 1;
    QImage out(cols * set->tileWidth, rows * set->tileHeight, QImage::Format_RGBA8888);
    out.fill(Qt::transparent);
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const uint32_t v = stamp ? m_stamp[size_t(cy * cols + cx)] : (uint32_t(m_currentTile) | m_tileFlips);
            const uint32_t id = v & kTileIdMask;
            if (id == 0 || id >= set->tiles.size())
                continue;
            const Image shown = orientTile(*set->tiles[id], v & ~kTileIdMask);
            for (int y = 0; y < shown.height() && y < set->tileHeight; ++y)
                for (int x = 0; x < shown.width() && x < set->tileWidth; ++x)
                    out.setPixelColor(cx * set->tileWidth + x, cy * set->tileHeight + y,
                                      toQColor(core::toRgba(*m_doc, shown, x, y)));
        }
    return out;
}

// The eyedropper let go after dragging over several cells: they become the stamp.
void Editor::finishTilePick()
{
    const Rect r = m_stroke.drawn;
    const bool block = !m_tilePickRect.isEmpty();
    if (!m_tilePickRect.isNull()) {
        m_tilePickRect = {};
        emit tileCursorChanged();
    }
    if (!block || r.isEmpty() || r.w * r.h < 2)
        return;
    std::vector<uint32_t> cells;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            cells.push_back(m_stroke.image->pixel(x, y));
    m_stamp = std::move(cells);
    m_stampW = r.w;
    m_stampH = r.h;
    emit tileCursorChanged();
}

// Places the picked tile on the cells joined to `cell` that show what it shows.
void Editor::tileFill(Point cell)
{
    Image& map = *m_stroke.image;
    if (!map.bounds().contains(cell.x, cell.y))
        return;
    const Tileset* tileset = currentTileset();
    // With a selection, only the cells it touches are filled (the others stop the flood).
    const auto allowed = [&](Point p) { return !tileset || cellTouchesSelection(p, m_stroke.origin, *tileset); };
    const uint32_t target = map.pixel(cell.x, cell.y), value = m_stroke.value;
    if (target == value || !allowed(cell))
        return;
    std::vector<Point> todo{cell};
    Rect changed;
    while (!todo.empty()) {
        const Point p = todo.back();
        todo.pop_back();
        if (!map.bounds().contains(p.x, p.y) || map.pixel(p.x, p.y) != target || !allowed(p))
            continue;
        map.setPixel(p.x, p.y, value);
        changed = changed.united({p.x, p.y, 1, 1});
        if (m_fillContiguous) {
            todo.push_back({p.x + 1, p.y});
            todo.push_back({p.x - 1, p.y});
            todo.push_back({p.x, p.y + 1});
            todo.push_back({p.x, p.y - 1});
        }
    }
    if (!m_fillContiguous) // every cell showing the same tile
        for (int y = 0; y < map.height(); ++y)
            for (int x = 0; x < map.width(); ++x)
                if (map.pixel(x, y) == target && allowed({x, y})) {
                    map.setPixel(x, y, value);
                    changed = changed.united({x, y, 1, 1});
                }
    const Tileset* set = currentTileset();
    m_stroke.bounds = changed;
    invalidate({m_stroke.origin.x + changed.x * set->tileWidth, m_stroke.origin.y + changed.y * set->tileHeight,
                changed.w * set->tileWidth, changed.h * set->tileHeight});
}

// The line / rectangle of cells from where the drag began to where it is now,
// drawn again from the map as it was at every step.
void Editor::drawTileShape()
{
    const Tileset* set = currentTileset();
    if (!set)
        return;
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    Image& map = *m_stroke.image;
    const Point a = m_stroke.start;
    const Point b{floorDiv(m_stroke.last.x, set->tileWidth), floorDiv(m_stroke.last.y, set->tileHeight)};
    const Rect previous = m_stroke.bounds;
    map = m_stroke.before;
    Rect now;
    const uint32_t value = m_stroke.value;
    const auto put = [&](int x, int y) {
        if (!map.bounds().contains(x, y))
            return;
        map.setPixel(x, y, value);
        now = now.united({x, y, 1, 1});
    };
    if (m_tool == Line) {
        bresenham(a, b, [&](Point p) { put(p.x, p.y); });
    } else {
        const int x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
        if (m_tool == Ellipse) {
            const Rect r{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
            if (m_fillShapes) {
                const auto spans = ellipseSpans(r);
                for (int i = 0; i < r.h; ++i)
                    for (int x = spans[size_t(i)].first; x <= spans[size_t(i)].second; ++x)
                        put(x, r.y + i);
            } else {
                ellipseOutline(r, [&](Point p) { put(p.x, p.y); });
            }
        } else {
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if (m_fillShapes || x == x0 || x == x1 || y == y0 || y == y1)
                        put(x, y);
        }
    }
    m_stroke.bounds = now;
    const Rect touched = previous.united(now);
    if (!touched.isEmpty())
        invalidate({m_stroke.origin.x + touched.x * set->tileWidth, m_stroke.origin.y + touched.y * set->tileHeight,
                    touched.w * set->tileWidth, touched.h * set->tileHeight});
}

void Editor::pickTile(Point pixel)
{
    const CelRef cel = m_current ? m_current->cel(m_currentFrame) : nullptr;
    const Tileset* set = currentTileset();
    if (!cel || !cel->image || !set)
        return;
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int cx = floorDiv(pixel.x - cel->position.x, set->tileWidth);
    const int cy = floorDiv(pixel.y - cel->position.y, set->tileHeight);
    if (!cel->image->bounds().contains(cx, cy))
        return;
    const uint32_t v = cel->image->pixel(cx, cy);
    setCurrentTile(int(v & kTileIdMask));
    setTileFlips(int(v >> 29));
}

int Editor::addTile()
{
    const Tileset* set = currentTileset();
    if (!set)
        return -1;
    const size_t id = m_current->tilesetIndex;
    Tileset after = *set;
    auto tile = std::make_shared<Image>(set->tileWidth, set->tileHeight, m_doc->pixelFormat());
    tile->clear(transparentValue());
    after.tiles.push_back(std::move(tile));
    execute(std::make_unique<SetTilesetCommand>(id, *set, std::move(after), "New Tile"));
    m_currentTile = tileCount() - 1;
    bumpTileset();
    return m_currentTile;
}

// The Tilemap32 image of every tilemap cel that uses the current layer's
// tileset, each image once (linked cels share it).
std::vector<std::shared_ptr<Image>> Editor::tilemapImages(size_t tilesetId) const
{
    std::vector<std::shared_ptr<Image>> out;
    std::unordered_set<const Image*> seen;
    m_doc->forEachLayer([&](const Layer& layer) {
        if (layer.kind != LayerKind::Tilemap || layer.tilesetIndex != tilesetId)
            return;
        for (const CelRef& cel : layer.cels)
            if (cel && cel->image && seen.insert(cel->image.get()).second)
                out.push_back(cel->image);
    });
    return out;
}

// Which tiles some cell of some tilemap shows.
std::vector<bool> Editor::usedTiles() const
{
    const Tileset* set = currentTileset();
    std::vector<bool> used(set ? set->tiles.size() : 0, false);
    if (!set)
        return used;
    for (const auto& image : tilemapImages(m_current->tilesetIndex))
        for (int y = 0; y < image->height(); ++y)
            for (int x = 0; x < image->width(); ++x) {
                const uint32_t id = image->pixel(x, y) & kTileIdMask;
                if (id < used.size())
                    used[id] = true;
            }
    return used;
}

bool Editor::applyTileEdit(const TileEdit& edit, const char* label)
{
    if (!edit.changes)
        return false;
    commitFloating();
    const size_t id = m_current->tilesetIndex;
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<SetTilesetCommand>(id, m_doc->tilesets[id], edit.after, label));
    for (const auto& image : tilemapImages(id)) {
        Image after = remapTilemap(*image, edit.map);
        if (after != *image)
            parts.push_back(std::make_unique<ImagePatchCommand>(image, Point{0, 0}, *image, std::move(after), label));
    }
    execute(std::make_unique<CommandGroup>(label, std::move(parts)));
    bumpTileset();
    invalidateAll();
    return true;
}

bool Editor::editTileInTab(int index)
{
    const Tileset* set = currentTileset();
    if (!set || index <= 0 || index >= int(set->tiles.size()))
        return false;
    endStroke();
    commitFloating();
    auto doc = std::make_unique<Document>(set->tileWidth, set->tileHeight, m_doc->colorMode());
    doc->palette = m_doc->palette;
    doc->transparentIndex = m_doc->transparentIndex;
    doc->grid = {0, 0, set->tileWidth, set->tileHeight};
    auto layer = doc->makeImageLayer("Tile");
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(set->tiles[size_t(index)]->copy(set->tiles[size_t(index)]->bounds()));
    layer->cels[0] = cel;
    doc->insertLayer(doc->root(), 0, std::move(layer));
    TileLink link;
    link.active = true;
    link.sourceId = m_recoveryId;
    link.tilesetId = m_current->tilesetIndex;
    link.tile = index;
    link.outerMode = m_tileMode;
    const QString name = tr("Tile %1 \u00b7 %2").arg(index - 1 + set->baseIndex)
                             .arg(QString::fromStdString(set->name));
    setDocument(std::move(doc), {}, name);
    m_tileLink = link;
    setTileMode(3);
    m_autosavePending = false;
    updateModified();
    emit documentsChanged();
    return true;
}

// A tile tab's pixels back into its source document's tileset, as one undo
// step there (a parked document: its undo stack takes the step).
void Editor::syncTileLink()
{
    if (!m_tileLink.active || !m_doc)
        return;
    DocumentSlot* source = nullptr;
    for (int i = 0; i < int(m_docs.size()); ++i)
        if (i != m_currentDoc && m_docs[size_t(i)].recoveryId == m_tileLink.sourceId)
            source = &m_docs[size_t(i)];
    if (!source || !source->doc || m_tileLink.tilesetId >= source->doc->tilesets.size()) {
        m_tileLink.active = false; // the source was closed
        return;
    }
    Tileset& set = source->doc->tilesets[m_tileLink.tilesetId];
    if (m_tileLink.tile <= 0 || m_tileLink.tile >= int(set.tiles.size())
        || m_doc->width() != set.tileWidth || m_doc->height() != set.tileHeight) {
        m_tileLink.active = false; // the tile is gone or not that size any more
        return;
    }
    // One layer with a full cel: its pixels as they are; otherwise what shows.
    Image tile;
    int layers = 0;
    const Layer* only = nullptr;
    m_doc->forEachLayer([&](const Layer& l) { ++layers; only = &l; });
    const CelRef cel = layers == 1 && only->kind == LayerKind::Image ? only->cel(0) : nullptr;
    if (cel && cel->image && cel->position == Point{0, 0} && cel->image->width() == set.tileWidth
        && cel->image->height() == set.tileHeight && only->opacity == 255 && only->visible)
        tile = cel->image->copy(cel->image->bounds());
    else
        tile = convertToDocument(*m_doc, m_composite);
    if (tile == *set.tiles[size_t(m_tileLink.tile)])
        return;
    Tileset before = set, after = set;
    after.tiles[size_t(m_tileLink.tile)] = std::make_shared<Image>(std::move(tile));
    source->undo.execute(*source->doc, std::make_unique<SetTilesetCommand>(m_tileLink.tilesetId, std::move(before),
                                                                          std::move(after), "Edit Tile"));
}

int Editor::duplicateTile(int index)
{
    const Tileset* set = currentTileset();
    if (!set || index <= 0 || index >= int(set->tiles.size()))
        return -1;
    const size_t id = m_current->tilesetIndex;
    Tileset after = *set;
    after.tiles.push_back(std::make_shared<Image>(set->tiles[size_t(index)]->copy(set->tiles[size_t(index)]->bounds())));
    execute(std::make_unique<SetTilesetCommand>(id, *set, std::move(after), "Duplicate Tile"));
    m_currentTile = tileCount() - 1;
    bumpTileset();
    return m_currentTile;
}

bool Editor::deleteTile(int index)
{
    const Tileset* set = currentTileset();
    if (!set || index <= 0)
        return false;
    const bool done = applyTileEdit(core::deleteTile(*set, size_t(index)), "Delete Tile");
    if (done)
        setCurrentTile(std::min(index, tileCount() - 1));
    return done;
}

bool Editor::moveTile(int from, int to)
{
    const Tileset* set = currentTileset();
    if (!set || from <= 0 || to <= 0)
        return false;
    const bool done = applyTileEdit(core::moveTile(*set, size_t(from), size_t(to)), "Move Tile");
    if (done)
        setCurrentTile(to);
    return done;
}

int Editor::removeUnusedTiles()
{
    const Tileset* set = currentTileset();
    if (!set)
        return 0;
    const size_t before = set->tiles.size();
    if (!applyTileEdit(core::removeUnusedTiles(*set, usedTiles()), "Remove Unused Tiles"))
        return 0;
    setCurrentTile(m_currentTile);
    return int(before - currentTileset()->tiles.size());
}

int Editor::mergeDuplicateTiles()
{
    const Tileset* set = currentTileset();
    if (!set)
        return 0;
    const size_t before = set->tiles.size();
    if (!applyTileEdit(core::mergeDuplicateTiles(*set), "Merge Duplicate Tiles"))
        return 0;
    setCurrentTile(m_currentTile);
    return int(before - currentTileset()->tiles.size());
}

int Editor::unusedTileCount() const
{
    const Tileset* set = currentTileset();
    return set ? int(countUnusedTiles(*set, usedTiles())) : 0;
}

int Editor::duplicateTileCount() const
{
    const Tileset* set = currentTileset();
    return set ? int(countDuplicateTiles(*set)) : 0;
}

QVariantList Editor::tileUseCounts() const
{
    const Tileset* set = currentTileset();
    std::vector<int> counts(set ? set->tiles.size() : 0, 0);
    if (set)
        for (const auto& image : tilemapImages(m_current->tilesetIndex))
            for (int y = 0; y < image->height(); ++y)
                for (int x = 0; x < image->width(); ++x) {
                    const uint32_t id = image->pixel(x, y) & kTileIdMask;
                    if (id < counts.size())
                        ++counts[id];
                }
    QVariantList out;
    out.reserve(qsizetype(counts.size()));
    for (const int n : counts)
        out.append(n);
    return out;
}

int Editor::tileUses(int index) const
{
    const Tileset* set = currentTileset();
    if (!set || index < 0)
        return 0;
    int n = 0;
    for (const auto& image : tilemapImages(m_current->tilesetIndex))
        for (int y = 0; y < image->height(); ++y)
            for (int x = 0; x < image->width(); ++x)
                n += int(image->pixel(x, y) & kTileIdMask) == index;
    return n;
}

QVariantList Editor::allSlices() const
{
    QVariantList list;
    for (int i = 0; i < int(m_doc->slices.size()); ++i) {
        const Slice& s = m_doc->slices[size_t(i)];
        list.append(QVariantMap{{"index", i}, {"name", QString::fromStdString(s.name)}, {"color", toQColor(s.color)},
                                {"nineSlice", s.nineSlice}, {"hasPivot", s.hasPivot}});
    }
    return list;
}

void Editor::setCurrentSlice(int index)
{
    index = index >= 0 && index < int(m_doc->slices.size()) ? index : -1;
    if (m_currentSlice == index)
        return;
    m_currentSlice = index;
    emit slicesChanged();
}

// One undo step (merged with the last one of `mergeKey` while continuing).
void Editor::editSlices(const std::function<void(std::vector<Slice>&)>& change, const char* label,
                        const QString& mergeKey)
{
    std::vector<Slice> after = m_doc->slices;
    change(after);
    if (after == m_doc->slices)
        return;
    const bool merge = !mergeKey.isEmpty() && continuing(mergeKey);
    if (mergeKey.isEmpty())
        m_mergeKey.clear();
    execute(std::make_unique<SetSlicesCommand>(m_doc->slices, std::move(after), label), merge);
    if (m_currentSlice >= int(m_doc->slices.size()))
        m_currentSlice = int(m_doc->slices.size()) - 1;
    emit slicesChanged();
}

int Editor::addSlice(const QRect& bounds)
{
    const Rect b = Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
    if (b.isEmpty())
        return -1;
    // "Slice N": one past the highest number in use.
    int n = 0;
    for (const Slice& s : m_doc->slices)
        if (QString::fromStdString(s.name).startsWith(QStringLiteral("Slice ")))
            n = std::max(n, QString::fromStdString(s.name).mid(6).toInt());
    editSlices([&](std::vector<Slice>& slices) {
        Slice slice;
        slice.name = "Slice " + std::to_string(n + 1);
        slice.keys.push_back({0, b, {}, {}});
        slices.push_back(std::move(slice));
    }, "New Slice");
    setCurrentSlice(int(m_doc->slices.size()) - 1);
    return m_currentSlice;
}

SliceKey* Editor::editableKey(Slice& slice) const
{
    auto it = std::find_if(slice.keys.rbegin(), slice.keys.rend(), [&](const SliceKey& k) { return k.frame <= m_currentFrame; });
    if (it == slice.keys.rend())
        return nullptr;
    if (!m_sliceKeys || it->frame == m_currentFrame)
        return &*it;
    // A key of its own at this frame, starting as a copy of the one in effect.
    SliceKey key = *it;
    key.frame = m_currentFrame;
    const auto at = std::find_if(slice.keys.begin(), slice.keys.end(), [&](const SliceKey& k) { return k.frame > m_currentFrame; });
    return &*slice.keys.insert(at, key);
}

void Editor::setSliceKeys(bool on)
{
    if (m_sliceKeys == on)
        return;
    m_sliceKeys = on;
    emit toolOptionsChanged();
}

void Editor::setSliceBounds(int index, const QRect& bounds)
{
    if (index < 0 || index >= int(m_doc->slices.size()) || bounds.width() < 1 || bounds.height() < 1)
        return;
    editSlices([&](std::vector<Slice>& slices) {
        if (SliceKey* k = editableKey(slices[size_t(index)])) {
            // A 9-slice center stays inside the new bounds.
            k->bounds = {bounds.x(), bounds.y(), bounds.width(), bounds.height()};
            k->center = k->center.intersected({0, 0, bounds.width(), bounds.height()});
        }
    }, "Slice", QStringLiteral("slice:%1").arg(index));
}

void Editor::setSliceNineSlice(int index, bool on)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    editSlices([&](std::vector<Slice>& slices) {
        Slice& s = slices[size_t(index)];
        s.nineSlice = on;
        if (on)
            for (SliceKey& k : s.keys)
                if (k.center.isEmpty() && !k.bounds.isEmpty()) {
                    const int cx = k.bounds.w / 3, cy = k.bounds.h / 3;
                    k.center = {cx, cy, std::max(1, k.bounds.w - 2 * cx), std::max(1, k.bounds.h - 2 * cy)};
                }
    }, on ? "9-Slice" : "No 9-Slice");
}

void Editor::setSliceCenter(int index, const QRect& center)
{
    if (index < 0 || index >= int(m_doc->slices.size()) || center.width() < 1 || center.height() < 1)
        return;
    editSlices([&](std::vector<Slice>& slices) {
        if (SliceKey* k = editableKey(slices[size_t(index)]))
            k->center = Rect{center.x(), center.y(), center.width(), center.height()}.intersected(
                {0, 0, k->bounds.w, k->bounds.h});
    }, "Slice Center", QStringLiteral("slice-center:%1").arg(index));
}

void Editor::setSliceHasPivot(int index, bool on)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    editSlices([&](std::vector<Slice>& slices) {
        Slice& s = slices[size_t(index)];
        if (on && !s.hasPivot)
            for (SliceKey& k : s.keys)
                k.pivot = {k.bounds.w / 2, k.bounds.h / 2};
        s.hasPivot = on;
    }, on ? "Slice Pivot" : "No Slice Pivot");
}

void Editor::setSlicePivot(int index, const QPoint& pivot)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    editSlices([&](std::vector<Slice>& slices) {
        if (SliceKey* k = editableKey(slices[size_t(index)]))
            k->pivot = {pivot.x(), pivot.y()};
    }, "Slice Pivot", QStringLiteral("slice-pivot:%1").arg(index));
}

void Editor::removeSliceKey(int index)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    editSlices([&](std::vector<Slice>& slices) {
        auto& keys = slices[size_t(index)].keys;
        const auto it = std::find_if(keys.begin(), keys.end(), [&](const SliceKey& k) { return k.frame == m_currentFrame; });
        if (it != keys.end() && it != keys.begin())
            keys.erase(it);
    }, "Remove Slice Key");
}

void Editor::setSliceName(int index, const QString& name)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    editSlices([&](std::vector<Slice>& slices) { slices[size_t(index)].name = name.toStdString(); }, "Rename Slice");
}

void Editor::setSliceColor(int index, const QColor& color)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    editSlices([&](std::vector<Slice>& slices) { slices[size_t(index)].color = toRgba(color); }, "Slice Color");
}

void Editor::removeSlice(int index)
{
    if (index < 0 || index >= int(m_doc->slices.size()))
        return;
    const int current = m_currentSlice; // before editSlices keeps it in range
    editSlices([&](std::vector<Slice>& slices) { slices.erase(slices.begin() + index); }, "Delete Slice");
    setCurrentSlice(current == index ? -1 : current > index ? current - 1 : current);
}

QRect Editor::grid() const
{
    // A file can say 0 x 0 for "no grid"; shown (and edited) as the default size.
    const Rect& g = m_doc->grid;
    return {g.x, g.y, g.w > 0 ? g.w : 16, g.h > 0 ? g.h : 16};
}

// One undo step; scrubbing the fields merges into it.
void Editor::setGrid(int x, int y, int width, int height)
{
    const Rect after{x, y, std::clamp(width, 1, 4096), std::clamp(height, 1, 4096)};
    if (after == m_doc->grid)
        return;
    execute(std::make_unique<SetGridCommand>(m_doc->grid, after), continuing(QStringLiteral("grid")));
    emit gridChanged();
}

void Editor::setSnapToGrid(bool on)
{
    if (m_snapToGrid == on)
        return;
    m_snapToGrid = on;
    emit snapToGridChanged();
}

// `r`, in coordinates `origin` away from the document's, grown out to the
// grid's lines (whole cells).
Rect Editor::snapCells(Rect r, Point origin) const
{
    if (!m_snapToGrid || r.isEmpty())
        return r;
    const QRect g = grid();
    const int x0 = r.x + origin.x, y0 = r.y + origin.y, x1 = r.right() + origin.x, y1 = r.bottom() + origin.y;
    const int left = g.x() + floorDiv(x0 - g.x(), g.width()) * g.width();
    const int top = g.y() + floorDiv(y0 - g.y(), g.height()) * g.height();
    const int right = g.x() + (floorDiv(x1 - 1 - g.x(), g.width()) + 1) * g.width();
    const int bottom = g.y() + (floorDiv(y1 - 1 - g.y(), g.height()) + 1) * g.height();
    return {left - origin.x, top - origin.y, right - left, bottom - top};
}

Rect Editor::snapToTileCells(Rect r) const
{
    const Tileset* set = currentTileset();
    if (!tilesActive() || !set || r.isEmpty())
        return r;
    const CelRef cel = m_current ? m_current->cel(m_currentFrame) : nullptr;
    const Point o = cel ? cel->position : Point{0, 0};
    const int tw = std::max(1, set->tileWidth), th = std::max(1, set->tileHeight);
    const int left = o.x + floorDiv(r.x - o.x, tw) * tw;
    const int top = o.y + floorDiv(r.y - o.y, th) * th;
    const int right = o.x + (floorDiv(r.right() - 1 - o.x, tw) + 1) * tw;
    const int bottom = o.y + (floorDiv(r.bottom() - 1 - o.y, th) + 1) * th;
    return {left, top, right - left, bottom - top};
}

// `p` on the nearest grid corner (a pixel just right of and below the lines).
Point Editor::snapVertex(Point p, Point origin) const
{
    if (!m_snapToGrid)
        return p;
    const QRect g = grid();
    const auto nearest = [](int v, int offset, int size) {
        return offset + int(std::lround(double(v - offset) / size)) * size;
    };
    return {nearest(p.x + origin.x, g.x(), g.width()) - origin.x, nearest(p.y + origin.y, g.y(), g.height()) - origin.y};
}

// Something at `at` (document coordinates) moved by `offset` lands on the
// nearest grid corner.
Point Editor::snapMoveBy(Point at, Point offset) const
{
    if (!m_snapToGrid)
        return offset;
    const Point target = snapVertex({at.x + offset.x, at.y + offset.y});
    return {target.x - at.x, target.y - at.y};
}

QRect Editor::snapRect(const QRect& r) const
{
    const Rect s = snapCells({r.x(), r.y(), r.width(), r.height()});
    return {s.x, s.y, s.w, s.h};
}

int Editor::snapEdge(int value, bool vertical) const
{
    const Point p = snapVertex({vertical ? 0 : value, vertical ? value : 0});
    return vertical ? p.y : p.x;
}

QPoint Editor::snapMove(QPoint at, QPoint offset) const
{
    const Point o = snapMoveBy({at.x(), at.y()}, {offset.x(), offset.y()});
    return {o.x, o.y};
}

void Editor::setSymmetryVertical(bool on)
{
    if (m_symmetryV == on)
        return;
    m_symmetryV = on;
    emit toolOptionsChanged();
}

core::BrushShape Editor::coreBrush() const
{
    return m_brushShape == RoundBrush ? core::BrushShape::Round : core::BrushShape::Square;
}

// The custom brush is picked and the tool uses it (blur / jumble don't).
bool Editor::customBrushInUse() const
{
    return m_brushShape == CustomBrush && m_tool != Blur && m_tool != Jumble;
}

// A turned brush only differs from the plain one when it is square or custom and big enough.
bool Editor::angleApplies() const
{
    if (m_brushAngle == 0)
        return false;
    return customBrushInUse() || (coreBrush() == core::BrushShape::Square && m_brushSize >= 3);
}

const Image& Editor::turnedBrush(bool custom) const
{
    const std::array<qint64, 5> key{custom ? 1 : 0, custom ? m_customBrushId : m_brushSize,
                                    custom ? 0 : int(coreBrush()), m_brushAngle, 0};
    if (key != m_turnedKey) {
        Image base;
        if (custom) {
            base = m_customBrush;
        } else {
            const int size = std::max(1, m_brushSize);
            base = Image(size, size);
            const auto& spans = brushSpans(size, coreBrush());
            for (int y = 0; y < size; ++y)
                for (int x = spans[size_t(y)].first; x <= spans[size_t(y)].second; ++x)
                    base.setPixel(x, y, pack({255, 255, 255, 255}));
        }
        m_turned = m_brushAngle != 0 ? rotated(base, m_brushAngle, 0) : base;
        m_turnedKey = key;
    }
    return m_turned;
}

void Editor::setBrushAngle(int degrees)
{
    degrees = (degrees % 360 + 360) % 360;
    if (degrees > 180)
        degrees -= 360;
    if (m_brushAngle == degrees)
        return;
    m_brushAngle = degrees;
    emit brushSizeChanged();
    refreshOverlay();
}

QImage Editor::brushImage() const
{
    // Blur and jumble paint a built-in shape (a custom brush counts as a square for them).
    const bool turned = !customBrushInUse() && angleApplies();
    if (customBrushInUse() || turned) {
        const Image& b = turnedBrush(customBrushInUse());
        QImage image(b.width(), b.height(), QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        const bool colors = previewsBrushColors();
        for (int y = 0; y < b.height(); ++y)
            for (int x = 0; x < b.width(); ++x)
                if (const uint32_t v = b.pixel(x, y); v >> 24)
                    image.setPixel(x, y, colors ? v : 0xffffffffu); // RGBA8888 matches our packing
        return image;
    }
    const int size = std::max(1, m_brushSize);
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    const auto& spans = brushSpans(size, coreBrush());
    for (int y = 0; y < size; ++y)
        for (int x = spans[size_t(y)].first; x <= spans[size_t(y)].second; ++x)
            image.setPixelColor(x, y, Qt::white);
    return image;
}

bool Editor::previewsBrushColors() const
{
    return customBrushInUse() && m_brushColors && m_tool != Eraser && !(m_shading && m_tool == Pencil);
}

qint64 Editor::brushKey() const
{
    const qint64 angle = angleApplies() ? m_brushAngle + 360 : 0;
    if (customBrushInUse())
        return ((qint64(m_customBrushId) * 2 + previewsBrushColors()) * 4 + CustomBrush) * 1000 + angle;
    return (qint64(m_brushSize) * 4 + (m_brushShape == RoundBrush ? RoundBrush : SquareBrush)) * 1000 + angle;
}

QSize Editor::brushExtent() const
{
    if (angleApplies()) {
        const Image& b = turnedBrush(customBrushInUse());
        return {b.width(), b.height()};
    }
    return customBrushInUse() ? customBrushSize() : QSize(m_brushSize, m_brushSize);
}

QImage Editor::customBrushImage() const
{
    const Image& b = m_customBrush;
    return b.isNull() ? QImage() : QImage(b.data(), b.width(), b.height(), b.stride(), QImage::Format_RGBA8888).copy();
}

void Editor::setBrushBlend(bool on)
{
    if (m_brushBlend == on)
        return;
    m_brushBlend = on;
    emit toolOptionsChanged();
}

void Editor::setBrushColors(bool on)
{
    if (m_brushColors == on)
        return;
    m_brushColors = on;
    emit toolOptionsChanged();
    emit brushSizeChanged(); // the preview's colors follow
}

void Editor::setDither(bool on)
{
    if (m_dither == on)
        return;
    m_dither = on;
    emit toolOptionsChanged();
}

void Editor::setDitherPattern(int kind)
{
    kind = std::clamp(kind, int(DitherPattern::Checker), int(DitherPattern::DiagonalLines));
    if (m_ditherPattern == kind)
        return;
    m_ditherPattern = kind;
    emit toolOptionsChanged();
}

void Editor::setDitherDensity(int density)
{
    density = std::clamp(density, 1, 15);
    if (m_ditherDensity == density)
        return;
    m_ditherDensity = density;
    emit toolOptionsChanged();
}

void Editor::setDitherOtherColor(bool on)
{
    if (m_ditherOtherColor == on)
        return;
    m_ditherOtherColor = on;
    emit toolOptionsChanged();
}

DitherPattern Editor::coreDither() const
{
    return {DitherPattern::Kind(m_ditherPattern), m_ditherDensity};
}

QImage Editor::ditherPreview(int kind, int density, int size) const
{
    const DitherPattern pattern{DitherPattern::Kind(std::clamp(kind, 0, int(DitherPattern::DiagonalLines))), density};
    QImage image(size, size, QImage::Format_RGBA8888);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            image.setPixelColor(x, y, ditherOn(pattern, x, y) ? QColor(Qt::white) : QColor(Qt::transparent));
    return image;
}

void Editor::ditherStroke(Rect area)
{
    if (m_stroke.covered.isNull())
        return;
    applyDither(*m_stroke.image, m_stroke.before, m_stroke.covered, area, m_stroke.origin, coreDither(),
                m_stroke.ditherOff);
}

bool Editor::brushFromSelection()
{
    endStroke();
    commitFloating();
    const Mask& sel = m_doc->selection;
    const Rect area = sel.isEmpty() ? m_doc->bounds() : sel.bounds().intersected(m_doc->bounds());
    return useCustomBrush(currentCelRgba(area));
}

bool Editor::setCustomBrush(const QImage& image)
{
    if (image.isNull())
        return false;
    const QImage q = image.convertToFormat(QImage::Format_RGBA8888);
    Image rgba(q.width(), q.height());
    for (int y = 0; y < q.height(); ++y)
        std::memcpy(rgba.data() + size_t(y) * size_t(rgba.stride()), q.constScanLine(y), size_t(q.width()) * 4);
    return useCustomBrush(rgba);
}

bool Editor::useCustomBrush(const Image& rgba)
{
    const Rect trim = opaqueBounds(rgba);
    if (trim.isEmpty())
        return false;
    if (trim.w > kMaxCustomBrush || trim.h > kMaxCustomBrush) {
        emit errorOccurred(tr("A brush can be at most %1 \u00d7 %1 pixels; this one would be %2 \u00d7 %3.")
                               .arg(kMaxCustomBrush)
                               .arg(trim.w)
                               .arg(trim.h));
        return false;
    }
    m_customSource = rgba.copy(trim);
    m_customBrush = m_customSource;
    m_customScale = 0;
    m_customFlipH = m_customFlipV = false;
    ++m_customBrushId;
    m_brushShape = CustomBrush;
    if (!usesBrush(m_tool))
        setTool(Pencil); // made with a selection tool: ready to draw
    emit customBrushChanged();
    emit brushSizeChanged();
    refreshOverlay();
    return true;
}

// m_customBrush made from the source: flipped, then scaled up (each pixel becomes n x n) or down (every
// nth pixel). False, and nothing changes, when that would be bigger than kMaxCustomBrush or lose every pixel.
bool Editor::rebuildCustomBrush(int scale)
{
    if (m_customSource.isNull())
        return false;
    const int w = m_customSource.width(), h = m_customSource.height();
    const int up = scale > 0 ? scale + 1 : 1, down = scale < 0 ? 1 - scale : 1;
    const int nw = (w * up + down - 1) / down, nh = (h * up + down - 1) / down;
    if (nw > kMaxCustomBrush || nh > kMaxCustomBrush)
        return false;
    Image out(nw, nh);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            int sx = x * down / up, sy = y * down / up;
            if (m_customFlipH)
                sx = w - 1 - sx;
            if (m_customFlipV)
                sy = h - 1 - sy;
            out.setPixel(x, y, m_customSource.pixel(sx, sy));
        }
    }
    const Rect trim = opaqueBounds(out);
    if (trim.isEmpty())
        return false;
    m_customBrush = out.copy(trim);
    m_customScale = scale;
    ++m_customBrushId;
    emit customBrushChanged();
    emit brushSizeChanged();
    refreshOverlay();
    return true;
}

// The custom brush mirrored (left-right, or top-bottom).
void Editor::flipCustomBrush(bool horizontal)
{
    if (m_customSource.isNull())
        return;
    (horizontal ? m_customFlipH : m_customFlipV) ^= true;
    // Trimming can shift a scaled-down brush, so a flip is rebuilt, not mirrored in place.
    if (!rebuildCustomBrush(m_customScale)) {
        (horizontal ? m_customFlipH : m_customFlipV) ^= true;
    }
}

void Editor::resetCustomBrush()
{
    if (m_customSource.isNull() || !customBrushEdited())
        return;
    const bool flipH = m_customFlipH, flipV = m_customFlipV;
    m_customFlipH = m_customFlipV = false;
    if (!rebuildCustomBrush(0)) {
        m_customFlipH = flipH;
        m_customFlipV = flipV;
    }
}

void Editor::setCustomBrushScale(int step)
{
    step = std::clamp(step, -16, 16);
    if (step != m_customScale)
        rebuildCustomBrush(step);
}

QString Editor::customBrushScaleText() const
{
    return m_customScale >= 0 ? QStringLiteral("×%1").arg(m_customScale + 1)
                              : QStringLiteral("÷%1").arg(1 - m_customScale);
}

// The brush's rect (it covers [p - size/2, p - size/2 + size) each way)
// mapped by each element the symmetry settings make, around the crossing
// point; each copy's center follows from its rect, so odd and even brushes,
// canvases and centers all line up.
std::vector<int> Editor::symmetryElements() const
{
    std::vector<int> generators;
    if (m_symmetryH)
        generators.push_back(MirrorLeftRight);
    if (m_symmetryV)
        generators.push_back(MirrorTopBottom);
    if (m_symmetryRising)
        generators.push_back(MirrorRising);
    if (m_symmetryFalling)
        generators.push_back(MirrorFalling);
    return symmetryGroup(generators);
}

std::vector<Editor::Copy> Editor::mirrored(Point p, int w, int h) const
{
    std::vector<Copy> points;
    const Rect brush{p.x - w / 2, p.y - h / 2, w, h};
    for (const int e : symmetryElements()) {
        const Rect r = mirrorRect(brush, e, m_symmetryCenter2);
        points.push_back({{r.x + r.w / 2, r.y + r.h / 2}, e, 0, 0});
    }
    if (m_symmetryRadial >= 2) {
        // Each copy turned round the crossing point by a whole share of a full turn (and mirrored
        // across the vertical line through it), on the nearest pixel.
        const double cx = m_symmetryCenter2.x / 2.0, cy = m_symmetryCenter2.y / 2.0;
        const double ox = w % 2 ? 0.5 : 0, oy = h % 2 ? 0.5 : 0; // the brush rect's middle, from its pixel
        const auto base = points;
        constexpr double kTwoPi = 6.283185307179586;
        for (const Copy& source : base) {
            const Point q = source.p;
            const int e = source.element;
            for (int mirror = 0; mirror < (m_symmetryRadialMirror ? 2 : 1); ++mirror)
                for (int k = 0; k < m_symmetryRadial; ++k) {
                    if (mirror == 0 && k == 0)
                        continue; // the stroke itself
                    const double angle = kTwoPi * k / m_symmetryRadial;
                    const double gx = q.x + ox - cx, gy = q.y + oy - cy;
                    const double mx = mirror ? -gx : gx;
                    const double rx = mx * std::cos(angle) - gy * std::sin(angle);
                    const double ry = mx * std::sin(angle) + gy * std::cos(angle);
                    const Point p{int(std::lround(cx + rx - ox)), int(std::lround(cy + ry - oy))};
                    const int element = mirror ? composeSymmetry(MirrorLeftRight, e) : e;
                    const int turn = m_symmetryRadialTurn ? k : 0;
                    if (std::none_of(points.begin(), points.end(), [&](const Copy& m) {
                            return m.p == p && m.element == element && m.turn == turn;
                        }))
                        points.push_back({p, element, turn, k});
                }
        }
    }
    return points;
}

std::vector<Point> Editor::mirrored(Point p, int size) const
{
    std::vector<Point> points;
    for (const auto& m : mirrored(p, size, size))
        points.push_back(m.p);
    return points;
}

// The brush oriented by `element`, then turned by `turn` shares of a full turn.
const ImageBrush& Editor::BrushSet::at(int element, int turn) const
{
    const int key = turn * 8 + element;
    if (const auto it = variants.find(key); it != variants.end())
        return it->second;
    Image rgbaVariant = orientedImage(rgba, element);
    if (turn != 0 && copies >= 2)
        rgbaVariant = rotated(rgbaVariant, 360.0 * turn / copies, 0);
    ImageBrush brush{doc ? convertToDocument(*doc, rgbaVariant) : rgbaVariant,
                     Image(rgbaVariant.width(), rgbaVariant.height(), PixelFormat::Indexed8)};
    for (int y = 0; y < rgbaVariant.height(); ++y)
        for (int x = 0; x < rgbaVariant.width(); ++x)
            brush.footprint.setPixel(x, y, rgbaVariant.pixel(x, y) >> 24 ? 1 : 0);
    return variants.emplace(key, std::move(brush)).first->second;
}

Editor::BrushSet Editor::customBrushes(bool documentFormat, bool custom) const
{
    BrushSet set;
    set.rgba = turnedBrush(custom);
    set.doc = documentFormat ? m_doc.get() : nullptr;
    set.copies = m_symmetryRadial;
    return set;
}

bool Editor::radialTurns() const
{
    if (m_symmetryRadial < 2 || !m_symmetryRadialTurn)
        return false;
    return customBrushInUse() || (coreBrush() == core::BrushShape::Square && m_brushSize >= 3);
}

Rect Editor::stampMirrored(Image& img, Point origin, Point p, uint32_t value, const BrushSet* custom,
                           bool ownColors, bool skipFirst) const
{
    Rect changed;
    const Point at{p.x + origin.x, p.y + origin.y};
    if (!custom) {
        const auto points = mirrored(at, m_brushSize);
        for (size_t i = skipFirst ? 1 : 0; i < points.size(); ++i)
            changed = changed.united(
                stampBrush(img, {points[i].x - origin.x, points[i].y - origin.y}, m_brushSize, value, coreBrush()));
        return changed;
    }
    const auto copies = mirrored(at, custom->width(), custom->height());
    for (size_t i = skipFirst ? 1 : 0; i < copies.size(); ++i) {
        const Point c{copies[i].p.x - origin.x, copies[i].p.y - origin.y};
        const ImageBrush& b = custom->at(copies[i].element, copies[i].turn);
        changed = changed.united(ownColors ? (m_brushBlend && img.format() == PixelFormat::Rgba8 ? stampImageBrushBlended(img, c, b)
                                                                                     : stampImageBrush(img, c, b))
                                           : stampImageBrush(img, c, b, value));
    }
    return changed;
}

// The blur / jumble brush at `p` and its mirrored copies (round or square;
// a custom brush paints as a square of its size).
Rect Editor::softenMirrored(Point p) const
{
    Image& img = *m_stroke.image;
    const Point o = m_stroke.origin;
    const Point at{p.x + o.x, p.y + o.y};
    const core::BrushShape shape = coreBrush();
    Rect changed;
    if (m_stroke.brushes) { // turned: its shape, and the mirror images of it
        const BrushSet& set = *m_stroke.brushes;
        for (const Copy& copy : mirrored(at, set.width(), set.height())) {
            const Point c{copy.p.x - o.x, copy.p.y - o.y};
            const Image* footprint = &set.at(copy.element, copy.turn).footprint;
            changed = changed.united(m_stroke.jumble
                                         ? jumbleBrush(img, m_stroke.before, c, m_brushSize, shape, m_stroke.seed, footprint)
                                         : blurBrush(*m_doc, img, m_stroke.before, c, m_brushSize, shape, footprint));
        }
        return changed;
    }
    for (const Point q : mirrored(at, m_brushSize)) {
        const Point c{q.x - o.x, q.y - o.y};
        changed = changed.united(m_stroke.jumble ? jumbleBrush(img, m_stroke.before, c, m_brushSize, shape, m_stroke.seed)
                                                 : blurBrush(*m_doc, img, m_stroke.before, c, m_brushSize, shape));
    }
    return changed;
}

Rect Editor::shadeMirrored(Point p) const
{
    Image& img = *m_stroke.image;
    const Point o = m_stroke.origin;
    const Point at{p.x + o.x, p.y + o.y};
    Rect changed;
    if (m_stroke.brushes) {
        const BrushSet& set = *m_stroke.brushes;
        for (const Copy& copy : mirrored(at, set.width(), set.height()))
            changed = changed.united(shadeImageBrush(img, m_stroke.before, {copy.p.x - o.x, copy.p.y - o.y},
                                                     set.at(copy.element, copy.turn), m_stroke.ramp, m_stroke.shadeStep));
        return changed;
    }
    for (const Point q : mirrored(at, m_brushSize))
        changed = changed.united(shadeBrush(img, m_stroke.before, {q.x - o.x, q.y - o.y}, m_brushSize, m_stroke.ramp,
                                            m_stroke.shadeStep, coreBrush()));
    return changed;
}

void Editor::setPixelPerfect(bool on)
{
    if (m_pixelPerfect == on)
        return;
    m_pixelPerfect = on;
    emit toolOptionsChanged();
}

void Editor::setFillShapes(bool on)
{
    if (m_fillShapes == on)
        return;
    m_fillShapes = on;
    emit toolOptionsChanged();
}

void Editor::setFillContiguous(bool on)
{
    if (m_fillContiguous == on)
        return;
    m_fillContiguous = on;
    emit toolOptionsChanged();
}

namespace {

// The built-in fonts, read once from the resources.
const std::vector<BitmapFont>& builtinFonts()
{
    static const std::vector<BitmapFont> fonts = [] {
        std::vector<BitmapFont> out;
        for (const char* name : {"4x6", "5x7", "6x10"}) {
            QFile file(QStringLiteral(":/fonts/%1.bdf").arg(QLatin1String(name)));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bytes = file.readAll();
            if (auto font = formats::parseBdf(std::string_view(bytes.constData(), size_t(bytes.size())))) {
                font->name = QString::fromLatin1(name).replace('x', QStringLiteral(" \u00d7 ")).toStdString();
                out.push_back(std::move(*font));
            }
        }
        // After the files', so a saved font number stays the same font.
        if (out.size() == 3) {
            out.push_back(tinyFont());
            out.push_back(emboldened(out[1]));
            out.push_back(emboldened(out[2]));
        }
        return out;
    }();
    return fonts;
}

} // namespace

QStringList Editor::fonts() const
{
    QStringList names;
    for (const BitmapFont& f : builtinFonts())
        names.append(QString::fromStdString(f.name));
    return names;
}

void Editor::setTextFont(int index)
{
    index = std::clamp(index, 0, std::max(0, int(builtinFonts().size()) - 1));
    if (m_textFont == index)
        return;
    m_textFont = index;
    emit toolOptionsChanged();
    if (m_text.active)
        renderTextFloat();
}

void Editor::setTextLetterSpacing(int pixels)
{
    pixels = std::clamp(pixels, -2, 16);
    if (m_textLetterSpacing == pixels)
        return;
    m_textLetterSpacing = pixels;
    emit toolOptionsChanged();
    if (m_text.active)
        renderTextFloat();
}

void Editor::setTextLineSpacing(int pixels)
{
    pixels = std::clamp(pixels, -2, 16);
    if (m_textLineSpacing == pixels)
        return;
    m_textLineSpacing = pixels;
    emit toolOptionsChanged();
    if (m_text.active)
        renderTextFloat();
}

void Editor::setGradientShape(int shape)
{
    shape = std::clamp(shape, 0, kGradientShapes - 1);
    if (m_gradientShape == shape)
        return;
    m_gradientShape = shape;
    emit toolOptionsChanged();
    redrawGradient();
}

void Editor::setGradientRamp(bool on)
{
    if (m_gradientRamp == on)
        return;
    m_gradientRamp = on;
    emit toolOptionsChanged();
    redrawGradient();
}

// What the gradient passes through: the stops of your own (when on), else the palette's shade range
// (the whole palette without one, transparent entries left out) evenly spaced; with the ramp off, or
// fewer than two colors, the primary and the secondary.
std::vector<Editor::GradientStop> Editor::gradientStops() const
{
    if (m_gradientCustom && m_gradientCustomStops.size() >= 2)
        return m_gradientCustomStops;
    std::vector<QColor> colors;
    if (m_gradientRamp) {
        const Palette& palette = m_doc->palette;
        int first = 0, last = palette.size() - 1;
        if (m_shadeRange.x() >= 0) {
            first = std::min(m_shadeRange.x(), last);
            last = std::min(m_shadeRange.y(), last);
        }
        for (int i = first; i <= last; ++i)
            if (palette.color(i).a != 0)
                colors.push_back(toQColor(palette.color(i)));
    }
    if (colors.size() < 2)
        colors = {m_primary, m_secondary};
    std::vector<GradientStop> stops;
    for (size_t i = 0; i < colors.size(); ++i)
        stops.push_back({double(i) / double(colors.size() - 1), colors[i]});
    return stops;
}

void Editor::setGradientRepeat(int repeat)
{
    repeat = std::clamp(repeat, 0, kGradientRepeats - 1);
    if (m_gradientRepeat == repeat)
        return;
    m_gradientRepeat = repeat;
    emit toolOptionsChanged();
    redrawGradient();
}

void Editor::setGradientCustom(bool on)
{
    if (m_gradientCustom == on)
        return;
    m_gradientCustom = on;
    if (on && m_gradientCustomStops.size() < 2)
        m_gradientCustomStops = {{0.0, m_primary}, {1.0, m_secondary}};
    emit toolOptionsChanged();
    redrawGradient();
}

QVariantList Editor::gradientCustomStops() const
{
    QVariantList list;
    for (const GradientStop& stop : m_gradientCustomStops)
        list.append(QVariantMap{{"position", stop.position}, {"color", stop.color}});
    return list;
}

void Editor::setGradientCustomStops(const QVariantList& stops)
{
    std::vector<GradientStop> parsed;
    for (const QVariant& v : stops) {
        const QVariantMap m = v.toMap();
        parsed.push_back({std::clamp(m.value("position").toDouble(), 0.0, 1.0), m.value("color").value<QColor>()});
    }
    if (parsed.size() < 2)
        return;
    std::stable_sort(parsed.begin(), parsed.end(),
                     [](const GradientStop& a, const GradientStop& b) { return a.position < b.position; });
    m_gradientCustomStops = parsed;
    emit toolOptionsChanged();
    if (m_gradientCustom)
        redrawGradient();
}

QString Editor::gradientStopsText() const
{
    QStringList parts;
    for (const GradientStop& stop : m_gradientCustomStops)
        parts << QString::number(stop.position, 'f', 4) + ":" + stop.color.name(QColor::HexArgb);
    return parts.join(';');
}

void Editor::setGradientStopsText(const QString& text)
{
    QVariantList list;
    for (const QString& part : text.split(';', Qt::SkipEmptyParts)) {
        const int colon = part.indexOf(':');
        const QColor color(part.mid(colon + 1));
        if (colon < 0 || !color.isValid())
            continue;
        list.append(QVariantMap{{"position", part.left(colon).toDouble()}, {"color", color}});
    }
    setGradientCustomStops(list);
}

// Draws the gradient just made again with the options as they are now.
void Editor::redrawGradient()
{
    if (m_gradientEdit.valid && !m_gradientEdit.redrawing)
        editGradient(m_gradientEdit.a.x, m_gradientEdit.a.y, m_gradientEdit.b.x, m_gradientEdit.b.y);
}

void Editor::editGradient(double ax, double ay, double bx, double by)
{
    checkGradient();
    if (!m_gradientEdit.valid || m_gradientEdit.redrawing || m_stroke.active)
        return;
    const Point a{int(std::lround(ax)), int(std::lround(ay))};
    const Point b{int(std::lround(bx)), int(std::lround(by))};
    if (a == b)
        return;
    m_gradientEdit.redrawing = true;
    const size_t position = m_undo.position();
    undo(); // takes the gradient back, then it is drawn again as a new stroke
    if (m_undo.position() + 1 == position) {
        const GradientEdit keep = m_gradientEdit;
        m_gradientEdit.valid = false;
        beginStroke(QPoint(a.x, a.y), false, {});
        moveStroke(QPoint(b.x, b.y), {});
        endStroke(); // opens the edit again
        if (!m_gradientEdit.valid) { // nothing came of it: the old one is gone, so close
            m_gradientEdit = keep;
            closeGradient();
        }
    } else {
        closeGradient();
    }
    m_gradientEdit.redrawing = false;
}

void Editor::finishGradient() { closeGradient(); }

void Editor::cancelGradient()
{
    checkGradient();
    if (!m_gradientEdit.valid)
        return;
    closeGradient();
    undo();
}

void Editor::closeGradient()
{
    if (!m_gradientEdit.valid && m_gradientLine.isEmpty())
        return;
    m_gradientEdit = {};
    if (!m_stroke.active && !m_gradientLine.isEmpty())
        m_gradientLine.clear();
    emit gradientLineChanged();
}

// Closes the open gradient when something else happened: another edit, tool, layer or frame.
void Editor::checkGradient()
{
    const GradientEdit& e = m_gradientEdit;
    if (!e.valid || e.redrawing)
        return;
    if (m_tool != Gradient || m_undo.position() != e.undoPosition || m_current != e.layer || m_currentFrame != e.frame
        || m_stroke.active)
        closeGradient();
}

void Editor::setGradientLine(Point from, Point to)
{
    const QVariantList line{QPointF(from.x, from.y), QPointF(to.x, to.y)};
    if (line == m_gradientLine)
        return;
    m_gradientLine = line;
    emit gradientLineChanged();
}

void Editor::setGradientDither(int pattern)
{
    pattern = std::clamp(pattern, 0, 4);
    if (m_gradientDither == pattern)
        return;
    m_gradientDither = pattern;
    emit toolOptionsChanged();
    redrawGradient();
}

void Editor::setTextAlign(int align)
{
    align = std::clamp(align, 0, 2);
    if (m_textAlign == align)
        return;
    m_textAlign = align;
    emit toolOptionsChanged();
    if (m_text.active)
        renderTextFloat();
}

void Editor::setTextCursor(int index)
{
    index = std::max(0, index);
    if (m_textCursor == index)
        return;
    m_textCursor = index;
    if (m_text.active)
        emit textChanged();
}

// The insertion point: a 1 pixel line, a text line tall, in sprite pixels.
QRect Editor::textCaret() const
{
    const auto& fonts = builtinFonts();
    if (!m_text.active || fonts.empty())
        return {};
    QString text = m_text.text;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QChar('\r'), QChar('\n'));
    const size_t index = size_t(text.left(m_textCursor).toUcs4().size());
    const TextCaret caret = core::textCaret(fonts[size_t(std::clamp(m_textFont, 0, int(fonts.size()) - 1))],
                                            text.toStdU32String(), index, m_textLetterSpacing, m_textLineSpacing,
                                            TextAlign(m_textAlign));
    const Point origin = m_float.active ? Point{m_float.box.x, m_float.box.y} : m_text.anchor;
    return QRect(origin.x + caret.x, origin.y + caret.y, 1, caret.h);
}

// A click on the text just placed, with nothing else done since: take the
// placing back and type on in it.
bool Editor::reopenLastText(Point pixel)
{
    const PlacedText placed = m_placedText;
    if (!placed.valid || placed.doc != m_doc.get() || placed.frame != m_currentFrame || placed.layer != m_current
        || !placed.box.contains(pixel.x, pixel.y) || m_undo.position() != placed.undoIndex || !m_undo.canUndo())
        return false;
    m_placedText = {};
    undo(); // the text's pixels go; the picture is as it was
    m_text = {true, placed.anchor, placed.text};
    m_textCursor = int(placed.text.size());
    renderTextFloat();
    emit textChanged();
    emit textStarted();
    return true;
}

void Editor::setText(const QString& text)
{
    if (!m_text.active || text == m_text.text)
        return;
    m_text.text = text;
    renderTextFloat();
    emit textChanged();
}

// Shows the text as the float (replacing the last one, where it was moved
// to), or no float while it's empty.
void Editor::renderTextFloat()
{
    const auto& fonts = builtinFonts();
    if (fonts.empty())
        return;
    QString text = m_text.text;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QChar('\r'), QChar('\n'));
    const Image ink = renderText(fonts[size_t(std::clamp(m_textFont, 0, int(fonts.size()) - 1))],
                                 text.toStdU32String(), m_textLetterSpacing, m_textLineSpacing, TextAlign(m_textAlign));
    Point at = m_text.anchor;
    if (m_float.active) {
        at = {m_float.box.x, m_float.box.y};
        m_text.anchor = at;
        m_retypingText = true;
        landFloat(false); // drops the old float without landing it
        m_retypingText = false;
    }
    if (ink.isNull()) {
        setSelection(m_recordedSelection);
        return;
    }
    Image source(ink.width(), ink.height(), m_doc->pixelFormat());
    source.clear(transparentValue());
    const uint32_t value = encodeColor(m_primary);
    for (int y = 0; y < ink.height(); ++y)
        for (int x = 0; x < ink.width(); ++x)
            if (ink.pixel(x, y))
                source.setPixel(x, y, value);
    m_float = {};
    m_float.keepSelection = false;
    startFloat(source, {at.x, at.y, ink.width(), ink.height()}, "Text");
}

void Editor::setTileMode(int mode)
{
    mode = std::clamp(mode, 0, 3);
    if (m_tileMode == mode)
        return;
    m_tileMode = mode;
    emit tileModeChanged();
}

QPoint Editor::wrapPixel(QPoint pixel) const
{
    const auto wrap = [](int v, int n) { return ((v % n) + n) % n; };
    return {tileX() ? wrap(pixel.x(), m_doc->width()) : pixel.x(),
            tileY() ? wrap(pixel.y(), m_doc->height()) : pixel.y()};
}

Rect Editor::wrapStroke(Rect area)
{
    if (!m_tileMode)
        return area;
    Image& img = *m_stroke.image;
    const Point o = m_stroke.origin;
    const Rect canvas = m_doc->bounds();
    Rect out = area;
    area = area.intersected(img.bounds());
    for (int y = area.y; y < area.bottom(); ++y)
        for (int x = area.x; x < area.right(); ++x) {
            const QPoint doc(x + o.x, y + o.y);
            if (canvas.contains(doc.x(), doc.y()))
                continue;
            const QPoint w = wrapPixel(doc);
            const int ix = w.x() - o.x, iy = w.y() - o.y;
            const uint32_t v = img.pixel(x, y), was = m_stroke.before.pixel(x, y);
            if (!canvas.contains(w.x(), w.y()) || v == was || !img.bounds().contains(ix, iy))
                continue; // not on a tiled axis, or not painted
            img.setPixel(ix, iy, v);
            img.setPixel(x, y, was);
            out = out.united({ix, iy, 1, 1});
        }
    return out;
}

void Editor::setSampleMerged(bool on)
{
    if (m_sampleMerged == on)
        return;
    m_sampleMerged = on;
    emit toolOptionsChanged();
}

void Editor::setWandContiguous(bool on)
{
    if (m_wandContiguous == on)
        return;
    m_wandContiguous = on;
    emit toolOptionsChanged();
}

void Editor::setWandTolerance(int tolerance)
{
    tolerance = std::clamp(tolerance, 0, 255);
    if (m_wandTolerance == tolerance)
        return;
    m_wandTolerance = tolerance;
    emit toolOptionsChanged();
}

void Editor::setFillTolerance(int tolerance)
{
    tolerance = std::clamp(tolerance, 0, 255);
    if (m_fillTolerance == tolerance)
        return;
    m_fillTolerance = tolerance;
    emit toolOptionsChanged();
}

bool Editor::usesBrush(Tool tool) const
{
    return tool == Pencil || tool == Eraser || tool == Line || tool == Rectangle || tool == Ellipse || tool == Blur
           || tool == Jumble;
}

void Editor::setBrushSize(int size)
{
    size = std::clamp(size, 1, 64);
    if (m_brushSize == size)
        return;
    m_brushSize = size;
    emit brushSizeChanged();
}

void Editor::setPrimaryColor(const QColor& c)
{
    if (m_primary == c)
        return;
    m_primary = c;
    emit colorsChanged();
    redrawGradient();
}

void Editor::setSecondaryColor(const QColor& c)
{
    if (m_secondary == c)
        return;
    m_secondary = c;
    emit colorsChanged();
    redrawGradient();
}

void Editor::swapColors()
{
    std::swap(m_primary, m_secondary);
    emit colorsChanged();
}

QVariantList Editor::palette() const
{
    QVariantList list;
    for (const auto& e : m_doc->palette.entries)
        list.append(toQColor(e.color));
    return list;
}

QVariantList Editor::layers() const
{
    QVariantList list;
    for (const Layer* layer : m_layerList) {
        list.append(QVariantMap{
            {"name", QString::fromStdString(layer->name)},
            {"kind", QString::fromLatin1(kindName(*layer))},
            {"depth", depthOf(*layer)},
            {"visible", layer->visible},
            {"locked", layer->locked},
            {"alphaLock", layer->isAlphaLocked()},
            {"collapsed", layer->isCollapsed()},
            {"opacity", (layer->opacity * 100 + 127) / 255},
            {"background", layer->isBackground()},
            {"blend", int(layer->blend)},
            {"hasChildren", !layer->children.empty()},
            // False when an enclosing group hides it.
            {"shown", effectivelyVisible(*layer)},
        });
    }
    return list;
}

// --- palette ----------------------------------------------------------------

void Editor::applyPalette(Palette palette, int transparentIndex, const std::vector<int>& map, const char* label,
                          bool keepRange)
{
    endStroke();
    commitFloating();
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<SetPaletteCommand>(m_doc->palette, m_doc->transparentIndex, std::move(palette),
                                                        transparentIndex, label));
    if (m_doc->colorMode() == ColorMode::Indexed && !map.empty()) {
        for (const auto& image : celImages(*m_doc)) {
            if (image->format() != PixelFormat::Indexed8)
                continue;
            Image after = remapped(*image, map);
            if (after == *image)
                continue;
            parts.push_back(std::make_unique<ImagePatchCommand>(image, Point{0, 0}, *image, std::move(after), label));
        }
    }
    auto group = std::make_unique<CommandGroup>(label, std::move(parts));
    if (m_palettePreviewing) { // a look at the result, then taken back
        group->redo(*m_doc);
        Image shown(m_doc->width(), m_doc->height());
        renderFrame(*m_doc, m_currentFrame, shown, m_doc->bounds(), true);
        group->undo(*m_doc);
        m_composite.blit(shown, {0, 0});
        m_dirty = m_doc->bounds();
        emit canvasChanged();
        return;
    }
    execute(std::move(group));
    if (!keepRange)
        setShadeRange({-1, -1}); // its entries may have moved
    emit paletteChanged();
    invalidateAll();
}

bool Editor::loadPalette(const QString& path)
{
    const QString fileName = QFileInfo(path).fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Couldn't open %1: %2.").arg(fileName, file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    const std::string extension = "." + QFileInfo(path).suffix().toLower().toStdString();
    std::string error;
    auto loaded = formats::readPalette({reinterpret_cast<const uint8_t*>(bytes.data()), size_t(bytes.size())},
                                       extension, &error);
    if (!loaded) {
        emit errorOccurred(tr("Couldn't load the palette from %1: %2").arg(fileName, QString::fromStdString(error)));
        return false;
    }
    replacePalette(std::move(loaded->palette), "Load Palette");
    return true;
}

bool Editor::setPaletteColors(const QVariantList& colors)
{
    Palette palette;
    for (const QVariant& v : colors) {
        const QColor c = v.value<QColor>();
        if (c.isValid())
            palette.entries.push_back({toRgba(c), {}});
    }
    if (palette.entries.empty())
        return false;
    replacePalette(std::move(palette), "Load Palette");
    return true;
}

void Editor::setPaletteLoadMode(int mode)
{
    mode = std::clamp(mode, 0, 1);
    if (mode == m_paletteLoadMode)
        return;
    m_paletteLoadMode = mode;
    emit paletteLoadModeChanged();
}

std::vector<int> Editor::paletteSwapMap(const Palette& incoming, int& transparent) const
{
    const int oldSize = m_doc->palette.size();
    const int newSize = incoming.size();
    transparent = m_doc->transparentIndex < newSize ? m_doc->transparentIndex : 0;
    std::vector<int> map;
    const bool match = m_paletteLoadMode == 1;
    if (m_doc->colorMode() != ColorMode::Indexed || (!match && oldSize <= newSize))
        return map;
    // The nearest of the new colors, never the transparent slot (unless it's all there is).
    const auto nearest = [&](Rgba c) {
        int best = -1, bestDistance = INT_MAX;
        for (int j = 0; j < newSize; ++j) {
            if (match && j == transparent && newSize > 1)
                continue;
            const Rgba p = incoming.color(j);
            const int d = std::abs(p.r - c.r) + std::abs(p.g - c.g) + std::abs(p.b - c.b) + std::abs(p.a - c.a);
            if (d < bestDistance) {
                best = j;
                bestDistance = d;
            }
        }
        return best;
    };
    map.resize(size_t(oldSize));
    for (int i = 0; i < oldSize; ++i) {
        if (match)
            map[size_t(i)] = i == m_doc->transparentIndex ? transparent : nearest(m_doc->palette.color(i));
        else // keep indices; the ones past the end take the nearest new color
            map[size_t(i)] = i < newSize ? i : nearest(m_doc->palette.color(i));
    }
    return map;
}

// Indexed pixels keep their indices (a palette swap) or go to the nearest
// new color, by paletteLoadMode; indices past the new palette's end always
// take its nearest color.
void Editor::replacePalette(Palette palette, const char* label)
{
    int transparent = 0;
    const std::vector<int> map = paletteSwapMap(palette, transparent);
    applyPalette(std::move(palette), transparent, map, label);
}

QString Editor::palettePreview(const QVariantList& colors, int maxSize)
{
    if (m_doc->colorMode() != ColorMode::Indexed)
        return {};
    Palette incoming;
    for (const QVariant& v : colors) {
        const QColor c = v.value<QColor>();
        if (c.isValid())
            incoming.entries.push_back({toRgba(c), {}});
    }
    if (incoming.entries.empty())
        return {};
    int transparent = 0;
    const std::vector<int> map = paletteSwapMap(incoming, transparent);
    Palette shown = incoming; // what the sprite's palette would hold after the load
    if (!map.empty()) { // old entry i shows as the color it goes to
        shown = m_doc->palette;
        for (size_t i = 0; i < map.size(); ++i)
            shown.entries[i].color = incoming.color(map[i]);
        shown.entries[size_t(m_doc->transparentIndex)].color.a = 0; // stays see-through
    }
    // Render with the palette swapped in for a moment; nothing else sees it.
    std::swap(m_doc->palette, shown);
    const Image image = formats::renderScaled(*m_doc, m_currentFrame, 1);
    std::swap(m_doc->palette, shown);

    QImage out(image.data(), image.width(), image.height(), image.stride(), QImage::Format_RGBA8888);
    const int scale = std::max(1, maxSize / std::max(1, std::max(out.width(), out.height())));
    if (scale > 1)
        out = out.scaled(out.width() * scale, out.height() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    out.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

bool Editor::savePalette(const QString& path, const QString& name)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "hex" && suffix != "pal" && suffix != "act")
        suffix = "gpl";
    std::optional<std::vector<uint8_t>> bytes;
    if (suffix == "act") {
        bytes = formats::writeAct(m_doc->palette);
    } else if (const auto text = formats::writePalette(m_doc->palette, name.toStdString(), "." + suffix.toStdString())) {
        bytes = std::vector<uint8_t>(text->begin(), text->end());
    }
    std::string error;
    if (!bytes || !formats::writeFileAtomically(toFsPath(path), *bytes, &error)) {
        emit errorOccurred(tr("Couldn't save %1: %2.").arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return false;
    }
    return true;
}

int Editor::addPaletteColor(const QColor& color)
{
    const Rgba c = toRgba(color);
    for (int i = 0; i < m_doc->palette.size(); ++i)
        if (m_doc->palette.color(i) == c)
            return i;
    if (m_doc->colorMode() == ColorMode::Indexed && m_doc->palette.size() >= 256)
        return -1;
    Palette palette = m_doc->palette;
    palette.entries.push_back({c, {}});
    applyPalette(std::move(palette), m_doc->transparentIndex, {}, "Add Color");
    return m_doc->palette.size() - 1;
}

bool Editor::removePaletteColor(int index)
{
    const int n = m_doc->palette.size();
    if (index < 0 || index >= n || n <= 1)
        return false;
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    if (indexed && index == m_doc->transparentIndex)
        return false;
    Palette palette = m_doc->palette;
    const Rgba removed = palette.color(index);
    palette.entries.erase(palette.entries.begin() + index);
    std::vector<int> map;
    int transparent = m_doc->transparentIndex;
    if (indexed) {
        // Its pixels take the nearest remaining color (never the transparent one).
        transparent = transparent > index ? transparent - 1 : transparent;
        int nearest = -1, best = INT_MAX;
        for (int i = 0; i < palette.size(); ++i) {
            if (i == transparent)
                continue;
            const Rgba p = palette.color(i);
            const int d = (p.r - removed.r) * (p.r - removed.r) + (p.g - removed.g) * (p.g - removed.g)
                + (p.b - removed.b) * (p.b - removed.b) + (p.a - removed.a) * (p.a - removed.a);
            if (d < best) {
                best = d;
                nearest = i;
            }
        }
        map.resize(size_t(n));
        for (int j = 0; j < n; ++j)
            map[size_t(j)] = j < index ? j : j > index ? j - 1 : std::max(0, nearest);
    }
    applyPalette(std::move(palette), transparent, map, "Remove Color");
    return true;
}

// --- palette tools ------------------------------------------------------------

std::vector<int> Editor::paletteToolIndices() const
{
    const int n = m_doc->palette.size();
    int first = 0, last = n - 1;
    if (m_shadeRange.x() >= 0) {
        first = std::min(m_shadeRange.x(), n - 1);
        last = std::min(m_shadeRange.y(), n - 1);
    }
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    std::vector<int> indices;
    for (int i = first; i <= last; ++i)
        if (!indexed || i != m_doc->transparentIndex)
            indices.push_back(i);
    return indices;
}

std::vector<uint64_t> Editor::paletteUsage() const
{
    const Palette& palette = m_doc->palette;
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    std::unordered_map<uint32_t, uint64_t> pixels;
    for (const auto& image : celImages(*m_doc)) {
        if (image->format() == PixelFormat::Tilemap32)
            continue;
        for (int y = 0; y < image->height(); ++y)
            for (int x = 0; x < image->width(); ++x)
                ++pixels[image->pixel(x, y)];
    }
    std::vector<uint64_t> usage;
    for (int i = 0; i < palette.size(); ++i) {
        const uint32_t value = indexed ? uint32_t(i) : fromRgba(*m_doc, palette.color(i));
        const auto it = pixels.find(value);
        usage.push_back(it == pixels.end() ? 0 : it->second);
    }
    return usage;
}

bool Editor::rampPalette(bool hueArc)
{
    const std::vector<int> indices = paletteToolIndices();
    if (indices.size() < 3)
        return false;
    Palette palette = m_doc->palette;
    const auto ramp = rampColors(palette.color(indices.front()), palette.color(indices.back()), int(indices.size()),
                                   hueArc);
    for (size_t k = 0; k < indices.size(); ++k)
        palette.entries[size_t(indices[k])].color = ramp[k];
    bool changed = false;
    for (const int i : indices)
        changed = changed || palette.color(i) != m_doc->palette.color(i);
    if (!changed)
        return false;
    applyPalette(std::move(palette), m_doc->transparentIndex, {}, "Color Ramp", true);
    return true;
}

bool Editor::insertRamp(int count, bool hueArc)
{
    const std::vector<int> indices = paletteToolIndices();
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    if (m_shadeRange.x() < 0 || indices.size() < 2 || count < 1 || count > 254
        || (indexed && m_doc->palette.size() + count > 256))
        return false;
    const int at = indices.back(); // the new colors go right before the last picked color
    Palette palette = m_doc->palette;
    const auto ramp = rampColors(palette.color(indices.front()), palette.color(indices.back()), count + 2, hueArc);
    for (int k = 0; k < count; ++k)
        palette.entries.insert(palette.entries.begin() + at + k, {ramp[size_t(k) + 1], {}});
    std::vector<int> map(static_cast<size_t>(m_doc->palette.size()));
    for (int j = 0; j < int(map.size()); ++j)
        map[size_t(j)] = j < at ? j : j + count;
    const int transparent = m_doc->transparentIndex < at ? m_doc->transparentIndex : m_doc->transparentIndex + count;
    const bool ranged = m_shadeRange.x() >= 0;
    applyPalette(std::move(palette), transparent, indexed ? map : std::vector<int>{}, "Insert Colors", true);
    if (ranged)
        setShadeRange({indices.front(), at + count});
    return true;
}

bool Editor::reorderPalette(const std::vector<int>& order, const char* label)
{
    const std::vector<int> indices = paletteToolIndices();
    const int n = m_doc->palette.size();
    Palette palette = m_doc->palette;
    std::vector<int> map(static_cast<size_t>(n));
    for (int j = 0; j < n; ++j)
        map[size_t(j)] = j;
    bool moved = false;
    for (size_t k = 0; k < indices.size(); ++k) {
        const int from = indices[size_t(order[k])];
        palette.entries[size_t(indices[k])] = m_doc->palette.entries[size_t(from)];
        map[size_t(from)] = indices[k];
        moved = moved || from != indices[k];
    }
    if (!moved)
        return false;
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    applyPalette(std::move(palette), m_doc->transparentIndex, indexed ? map : std::vector<int>{}, label, true);
    return true;
}

bool Editor::sortPalette(const QString& key)
{
    const PaletteSort sort = key == "saturation" ? PaletteSort::Saturation
                           : key == "lightness"  ? PaletteSort::Lightness
                                                 : PaletteSort::Hue;
    std::vector<Rgba> colors;
    for (const int i : paletteToolIndices())
        colors.push_back(m_doc->palette.color(i));
    return reorderPalette(sortOrder(colors, sort), "Sort Palette");
}

bool Editor::reversePalette()
{
    std::vector<int> order(paletteToolIndices().size());
    for (size_t k = 0; k < order.size(); ++k)
        order[k] = int(order.size() - 1 - k);
    return reorderPalette(order, "Reverse Palette");
}

bool Editor::reducePalette(int count)
{
    const std::vector<int> indices = paletteToolIndices();
    if (count < 1 || int(indices.size()) <= count)
        return false;
    const std::vector<uint64_t> usage = paletteUsage();
    std::vector<Rgba> colors;
    std::vector<uint64_t> counts;
    for (const int i : indices) {
        colors.push_back(m_doc->palette.color(i));
        counts.push_back(usage[size_t(i)]);
    }
    return applyReduction(indices, reduceColors(colors, counts, count), "Reduce Colors");
}

QVariantList Editor::previewReduce(int count)
{
    QVariantList list;
    const std::vector<int> indices = paletteToolIndices();
    if (count < 1 || int(indices.size()) <= count)
        return list;
    const std::vector<uint64_t> usage = paletteUsage();
    std::vector<Rgba> colors;
    std::vector<uint64_t> counts;
    for (const int i : indices) {
        colors.push_back(m_doc->palette.color(i));
        counts.push_back(usage[size_t(i)]);
    }
    for (const Rgba& c : reduceColors(colors, counts, count).colors)
        list.append(toQColor(c));
    return list;
}

void Editor::previewReducePixels(int count)
{
    if (m_doc->colorMode() != ColorMode::Indexed)
        return;
    if (!m_modePreview)
        beginColorModePreview();
    m_palettePreviewing = true;
    const bool reduced = reducePalette(count);
    m_palettePreviewing = false;
    if (!reduced)
        invalidateAll(); // nothing to reduce: the sprite as it is
}

QVariantList Editor::previewInsert(int count, bool hueArc)
{
    QVariantList list;
    const std::vector<int> indices = paletteToolIndices();
    if (m_shadeRange.x() < 0 || indices.size() < 2 || count < 1 || count > 254)
        return list;
    const auto ramp = rampColors(m_doc->palette.color(indices.front()), m_doc->palette.color(indices.back()),
                                 count + 2, hueArc);
    for (int k = 0; k < count; ++k)
        list.append(toQColor(ramp[size_t(k) + 1]));
    return list;
}

// The tool entries `indices` become `reduced.colors` (`reduced.map` says
// which each one goes to); entries outside them stay.
bool Editor::applyReduction(const std::vector<int>& indices, const ColorReduction& reduced, const char* label)
{
    // The kept colors take the first of the tools' entries; the rest go.
    const int n = m_doc->palette.size();
    std::vector<int> slot(size_t(n), -1); // tool entry -> its place among them
    for (size_t k = 0; k < indices.size(); ++k)
        slot[size_t(indices[k])] = int(k);
    Palette palette;
    std::vector<int> newIndex(size_t(n), -1); // entries that stay -> new index
    std::vector<int> keptAt(reduced.colors.size());
    for (int j = 0; j < n; ++j) {
        const int k = slot[size_t(j)];
        if (k < 0) {
            newIndex[size_t(j)] = palette.size();
            palette.entries.push_back(m_doc->palette.entries[size_t(j)]);
        } else if (k < int(reduced.colors.size())) {
            keptAt[size_t(k)] = palette.size();
            palette.entries.push_back({reduced.colors[size_t(k)], {}});
        }
    }
    std::vector<int> map(static_cast<size_t>(n));
    for (int j = 0; j < n; ++j) {
        const int k = slot[size_t(j)];
        map[size_t(j)] = k < 0 ? newIndex[size_t(j)] : keptAt[size_t(reduced.map[size_t(k)])];
    }
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    const int transparent = indexed ? map[size_t(m_doc->transparentIndex)] : m_doc->transparentIndex;
    const bool ranged = m_shadeRange.x() >= 0;
    applyPalette(std::move(palette), transparent, indexed ? map : std::vector<int>{}, label, true);
    if (ranged && !m_palettePreviewing) // the range shrinks to the kept colors
        setShadeRange({keptAt.front(), keptAt.back()});
    return true;
}

ColorReduction Editor::planRemoval(const std::vector<int>& indices, bool unused, bool duplicates) const
{
    const std::vector<uint64_t> usage = paletteUsage();
    // Each entry goes to the first entry with its color (duplicates), or is
    // dropped when nothing uses it (unused: the pixels would have nowhere to
    // go, so never the last one).
    ColorReduction reduced;
    reduced.map.resize(indices.size());
    std::vector<int> firstOf;
    std::vector<uint64_t> used;
    for (size_t k = 0; k < indices.size(); ++k) {
        const Rgba color = m_doc->palette.color(indices[k]);
        int at = -1;
        if (duplicates)
            for (size_t j = 0; j < reduced.colors.size(); ++j)
                if (reduced.colors[j] == color) {
                    at = int(j);
                    break;
                }
        if (at < 0) {
            at = int(reduced.colors.size());
            reduced.colors.push_back(color);
            used.push_back(0);
        }
        used[size_t(at)] += usage[size_t(indices[k])];
        reduced.map[k] = at;
    }
    if (unused) { // drop groups nothing uses, keeping at least one
        std::vector<int> renumber(reduced.colors.size(), -1);
        ColorReduction kept;
        for (size_t j = 0; j < reduced.colors.size(); ++j)
            if (used[j] > 0 || (j == 0 && std::none_of(used.begin(), used.end(), [](uint64_t u) { return u > 0; }))) {
                renumber[j] = int(kept.colors.size());
                kept.colors.push_back(reduced.colors[j]);
            }
        // Pixels can't use an entry that is dropped, so only the map of kept ones matters.
        kept.map.resize(indices.size());
        for (size_t k = 0; k < indices.size(); ++k) {
            const int r = renumber[size_t(reduced.map[k])];
            kept.map[k] = r >= 0 ? r : 0;
        }
        reduced = std::move(kept);
    }
    return reduced;
}

bool Editor::removeUnusedColors(bool unused, bool duplicates)
{
    const std::vector<int> indices = paletteToolIndices();
    if (indices.size() < 2 || (!unused && !duplicates))
        return false;
    const ColorReduction reduced = planRemoval(indices, unused, duplicates);
    if (reduced.colors.size() == indices.size())
        return false;
    return applyReduction(indices, reduced, "Remove Colors");
}

int Editor::removableColors(bool unused, bool duplicates) const
{
    const std::vector<int> indices = paletteToolIndices();
    if (indices.size() < 2 || (!unused && !duplicates))
        return 0;
    return int(indices.size() - planRemoval(indices, unused, duplicates).colors.size());
}

bool Editor::movePaletteColor(int from, int to)
{
    const int n = m_doc->palette.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;
    Palette palette = m_doc->palette;
    auto entry = palette.entries[size_t(from)];
    palette.entries.erase(palette.entries.begin() + from);
    palette.entries.insert(palette.entries.begin() + to, entry);
    // Where each old index ends up.
    std::vector<int> map(static_cast<size_t>(n));
    for (int j = 0; j < n; ++j) {
        if (j == from)
            map[size_t(j)] = to;
        else if (from < to && j > from && j <= to)
            map[size_t(j)] = j - 1;
        else if (from > to && j >= to && j < from)
            map[size_t(j)] = j + 1;
        else
            map[size_t(j)] = j;
    }
    const bool indexed = m_doc->colorMode() == ColorMode::Indexed;
    const int transparent = indexed ? map[size_t(m_doc->transparentIndex)] : m_doc->transparentIndex;
    applyPalette(std::move(palette), transparent, indexed ? map : std::vector<int>{}, "Move Color");
    return true;
}

bool Editor::setPaletteColor(int index, const QColor& color)
{
    if (index < 0 || index >= m_doc->palette.size())
        return false;
    const Rgba c = toRgba(color);
    if (m_doc->palette.color(index) == c)
        return false;
    Palette palette = m_doc->palette;
    palette.entries[size_t(index)].color = c;
    applyPalette(std::move(palette), m_doc->transparentIndex, {}, "Change Color");
    return true;
}

QImage Editor::layerImage(int index, int maxSide) const
{
    const Layer* layer = layerAt(index);
    if (!layer)
        return {};
    // A group or a tilemap: what it shows, drawn on its own, then thumbnailed like a cel.
    if (layer->kind != LayerKind::Image) {
        Image full(m_doc->width(), m_doc->height());
        renderLayerContent(*m_doc, *layer, m_currentFrame, full, m_doc->bounds());
        if (opaqueBounds(full).isEmpty())
            return {};
        const int cw = m_doc->width(), ch = m_doc->height();
        const double fit = std::min(1.0, double(std::max(1, maxSide)) / double(std::max(cw, ch)));
        const int w = std::max(1, int(std::lround(cw * fit))), h = std::max(1, int(std::lround(ch * fit)));
        QImage out(w, h, QImage::Format_RGBA8888);
        out.fill(Qt::transparent);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int x0 = int(int64_t(x) * cw / w), x1 = std::max(x0 + 1, int(int64_t(x + 1) * cw / w));
                const int y0 = int(int64_t(y) * ch / h), y1 = std::max(y0 + 1, int(int64_t(y + 1) * ch / h));
                int64_t r = 0, g = 0, bl = 0, alpha = 0, count = 0;
                for (int cy = y0; cy < y1; ++cy)
                    for (int cx = x0; cx < x1; ++cx) {
                        ++count;
                        const Rgba c = unpack(full.pixel(cx, cy));
                        r += c.r * c.a;
                        g += c.g * c.a;
                        bl += c.b * c.a;
                        alpha += c.a;
                    }
                if (alpha > 0)
                    out.setPixelColor(x, y, QColor(int(r / alpha), int(g / alpha), int(bl / alpha), int(alpha / count)));
            }
        return out;
    }
    const CelRef cel = layer->cel(m_currentFrame);
    if (!cel || !cel->image || cel->image->width() <= 0)
        return {};
    const int cw = m_doc->width(), ch = m_doc->height();
    const double fit = std::min(1.0, double(std::max(1, maxSide)) / double(std::max(cw, ch)));
    const int w = std::max(1, int(std::lround(cw * fit))), h = std::max(1, int(std::lround(ch * fit)));
    QImage out(w, h, QImage::Format_RGBA8888);
    out.fill(Qt::transparent);
    const Image& image = *cel->image;
    const bool background = layer->isBackground();
    // Each thumbnail pixel is the average (alpha weighted) of the canvas pixels it covers.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int x0 = int(int64_t(x) * cw / w), x1 = std::max(x0 + 1, int(int64_t(x + 1) * cw / w));
            const int y0 = int(int64_t(y) * ch / h), y1 = std::max(y0 + 1, int(int64_t(y + 1) * ch / h));
            int64_t r = 0, g = 0, bl = 0, alpha = 0, count = 0;
            for (int cy = y0; cy < y1; ++cy)
                for (int cx = x0; cx < x1; ++cx) {
                    ++count;
                    const int ix = cx - cel->position.x, iy = cy - cel->position.y;
                    if (ix < 0 || iy < 0 || ix >= image.width() || iy >= image.height())
                        continue;
                    const Rgba c = core::toRgba(*m_doc, image, ix, iy, background);
                    r += c.r * c.a;
                    g += c.g * c.a;
                    bl += c.b * c.a;
                    alpha += c.a;
                }
            if (alpha > 0)
                out.setPixelColor(x, y, QColor(int(r / alpha), int(g / alpha), int(bl / alpha), int(alpha / count)));
        }
    return out;
}

Layer* Editor::layerAt(int index) const
{
    return index >= 0 && index < int(m_layerList.size()) ? m_layerList[size_t(index)] : nullptr;
}

int Editor::currentLayer() const { return m_currentRow; }

void Editor::setCurrentLayer(int index)
{
    commitFloating();
    Layer* layer = layerAt(std::clamp(index, 0, int(m_layerList.size()) - 1));
    if (!layer || layer == m_current)
        return;
    if (!m_picks.empty()) { // moving to another layer on its own drops the picks
        m_picks.clear();
        emit rangeChanged();
    }
    m_current = layer;
    m_currentRow = int(std::find(m_layerList.begin(), m_layerList.end(), layer) - m_layerList.begin());
    emit currentLayerChanged();
    checkGradient();
    bumpTileset(); // the layer may be a tilemap, or on another tileset: the tile panel follows it
}

bool Editor::canRemoveLayer() const
{
    return m_current && !(m_current->parent == &m_doc->root() && m_doc->root().children.size() <= 1);
}

void Editor::rebuildLayerList()
{
    m_layerList.clear();
    // Children before their group, so read top first a group is above its
    // children (as in Aseprite).
    std::function<void(Layer&)> add = [&](Layer& group) {
        for (auto& child : group.children) {
            if (child->kind == LayerKind::Group && !child->isCollapsed())
                add(*child);
            m_layerList.push_back(child.get());
        }
    };
    add(m_doc->root());

    // m_current may have been removed (by undo, say) or hidden in a
    // collapsed group. Only compare the pointer until it's known to be live.
    bool live = false;
    m_doc->forEachLayer([&](const Layer& layer) { live = live || &layer == m_current; });
    if (live) {
        while (m_current->parent && m_current->parent->parent
               && std::find(m_layerList.begin(), m_layerList.end(), m_current) == m_layerList.end())
            m_current = m_current->parent;
    } else {
        m_current = m_layerList.empty()
            ? nullptr
            : m_layerList[size_t(std::clamp(m_currentRow, 0, int(m_layerList.size()) - 1))];
    }
    const auto it = std::find(m_layerList.begin(), m_layerList.end(), m_current);
    m_currentRow = it == m_layerList.end() ? 0 : int(it - m_layerList.begin());
    // Picked layers that are gone (or in another document, or hidden in a closed group) drop out.
    std::erase_if(m_picks, [&](const Layer* l) { return std::find(m_layerList.begin(), m_layerList.end(), l) == m_layerList.end(); });
}

void Editor::setCurrentFrame(int frame)
{
    if (m_currentFrame != std::clamp(frame, 0, m_doc->frameCount() - 1))
        commitFloating();
    frame = std::clamp(frame, 0, m_doc->frameCount() - 1);
    if (m_currentFrame == frame)
        return;
    m_currentFrame = frame;
    emit currentFrameChanged();
    emit frameDurationChanged();
    checkGradient();
    if (m_playing)
        showPlaybackFrame();
    else
        invalidateAll();
}

void Editor::setFrameDuration(int ms)
{
    setDurations(m_currentFrame, m_currentFrame, ms);
}

void Editor::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing && m_doc && m_doc->frameCount() > 1;
    clearFrameCache(); // a loop starts from what the document is now
    if (m_playing) {
        m_playingTag = tagAt(*m_doc, m_currentFrame);
        if (m_playingTag >= 0) {
            const Tag& tag = m_doc->tags[size_t(m_playingTag)];
            m_playback.emplace(tag.from, tag.to, tag.direction, tag.repeat, m_currentFrame);
        } else {
            m_playback.emplace(0, m_doc->frameCount() - 1, AniDirection::Forward, 0, m_currentFrame);
        }
        m_playTimer.start(frameDuration());
    } else {
        m_playTimer.stop();
        m_playback.reset();
        m_playingTag = -1;
    }
    emit playingChanged();
    if (m_onionEnabled)
        invalidateAll(); // the onion skin hides while playing
}

int Editor::startPreviewPlayback(int frame)
{
    m_previewPlayback.reset();
    if (!m_doc || m_doc->frameCount() < 2)
        return -1;
    frame = std::clamp(frame, 0, m_doc->frameCount() - 1);
    const int tag = tagAt(*m_doc, frame);
    if (tag >= 0) {
        const Tag& t = m_doc->tags[size_t(tag)];
        m_previewPlayback.emplace(t.from, t.to, t.direction, t.repeat, frame);
    } else {
        m_previewPlayback.emplace(0, m_doc->frameCount() - 1, AniDirection::Forward, 0, frame);
    }
    return frame;
}

int Editor::nextPreviewFrame()
{
    if (!m_previewPlayback || !m_doc)
        return -1;
    const auto next = m_previewPlayback->next();
    if (!next || *next >= m_doc->frameCount()) {
        m_previewPlayback.reset();
        return -1;
    }
    return *next;
}

void Editor::stopPreviewPlayback()
{
    m_previewPlayback.reset();
}

int Editor::tagIndexAt(int frame) const
{
    return m_doc ? tagAt(*m_doc, frame) : -1;
}

void Editor::advancePlayback()
{
    if (!m_playing || !m_playback)
        return;
    const auto next = m_playback->next();
    if (!next || *next >= m_doc->frameCount()) {
        setPlaying(false); // the last repeat has played
        return;
    }
    setCurrentFrame(*next);
    m_playTimer.start(frameDuration());
}

// --- history ----------------------------------------------------------------

void Editor::execute(std::unique_ptr<Command> command, bool merge)
{
    cancelEffect();   // a preview never mixes with another edit
    commitFloating(); // a paste in progress lands before anything else changes
    m_undo.execute(*m_doc, withSelection(std::move(command)), merge);
    emit historyChanged();
}

// Records a command whose effect is already applied.
void Editor::pushCommand(std::unique_ptr<Command> command)
{
    m_undo.push(withSelection(std::move(command)));
}

// A selection change the history doesn't have yet goes into the same step
// as `command`, so undoing an edit also puts its selection back.
std::unique_ptr<Command> Editor::withSelection(std::unique_ptr<Command> command)
{
    if (m_doc->selection == m_recordedSelection)
        return command;
    const std::string label = command->label();
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::move(command));
    parts.push_back(std::make_unique<SetSelectionCommand>(m_recordedSelection, m_doc->selection, label));
    m_recordedSelection = m_doc->selection;
    return std::make_unique<CommandGroup>(label, std::move(parts));
}

// Makes the current selection its own undo step (if it changed).
void Editor::recordSelection(const char* label, bool merge)
{
    if (m_doc->selection == m_recordedSelection)
        return;
    auto command = std::make_unique<SetSelectionCommand>(m_recordedSelection, m_doc->selection, label);
    m_recordedSelection = m_doc->selection;
    m_undo.execute(*m_doc, std::move(command), merge);
    emit historyChanged();
}

void Editor::undo()
{
    if (effectActive()) { // first puts back an effect being previewed
        cancelEffect();
        return;
    }
    if (m_polygon.active) { // undo first drops a polygon being placed
        cancelPolygon();
        return;
    }
    if (m_float.active) { // undo first drops a paste that hasn't landed
        cancelFloating();
        return;
    }
    if (m_stroke.active)
        return;
    if (m_doc->selection != m_recordedSelection) { // not recorded yet: just put it back
        setSelection(m_recordedSelection);
        return;
    }
    if (!m_undo.canUndo())
        return;
    const Rect oldSelection = m_doc->selection.bounds();
    m_undo.undo(*m_doc);
    m_recordedSelection = m_doc->selection;
    selectionUpdated(oldSelection);
    emit historyChanged();
    structureChanged();
}

void Editor::redo()
{
    if (effectActive()) {
        cancelEffect();
        return;
    }
    cancelPolygon();
    cancelFloating();
    if (m_stroke.active || !m_undo.canRedo())
        return;
    const Rect oldSelection = m_doc->selection.bounds();
    m_undo.redo(*m_doc);
    m_recordedSelection = m_doc->selection;
    selectionUpdated(oldSelection);
    emit historyChanged();
    structureChanged();
}

// Called after anything that may have changed layers or frames.
// Canvas-sized buffers for the current document size; drops the selection.
void Editor::resetCanvas()
{
    const int w = m_doc->width(), h = m_doc->height();
    m_composite = Image(w, h);
    m_overlay = Image(w, h);
    m_onion = Image(w, h);
    m_scratch = Image(w, h);
    m_onionClear = true;
    m_onionDirty = m_doc->bounds();
    m_overlayDrawn = {};
    m_overlayDirty = m_doc->bounds();
    m_lastPainted.reset();
    if (m_symmetrySize != Point{w, h}) { // a new size (not a tab switch): center the axes
        m_symmetrySize = {w, h};
        // The file's own crossing point if it kept one (used up here: a later resize centers again).
        Point c2{w, h};
        if (m_doc->symmetryCenter2 && m_doc->symmetryCenter2->x >= 0 && m_doc->symmetryCenter2->y >= 0
            && m_doc->symmetryCenter2->x <= 2 * w && m_doc->symmetryCenter2->y <= 2 * h)
            c2 = *m_doc->symmetryCenter2;
        m_doc->symmetryCenter2.reset();
        m_symmetryCenter2 = fittedForDiagonals(c2, false, 0);
        emit symmetryCenterChanged();
    }
    m_selectionImage = Image(w, h);
    drawSelectionImage(m_doc->bounds());
    emit selectionChanged();
    emit canvasSizeChanged();
}

static Resample resampleFor(const QString& method)
{
    return method == "bilinear" ? Resample::Bilinear : method == "smooth" ? Resample::Smooth : Resample::Nearest;
}

void Editor::resizeSprite(int width, int height, const QString& method)
{
    width = std::clamp(width, 1, 4096);
    height = std::clamp(height, 1, 4096);
    endStroke();
    commitFloating();
    if (width == m_doc->width() && height == m_doc->height())
        return;
    setSelection(Mask{}); // dropped with the old size (and back on undo)
    execute(scaleSpriteCommand(*m_doc, width, height, resampleFor(method)));
    structureChanged();
}

QString Editor::spriteSizePreview(int width, int height, int maxSide, const QString& method)
{
    width = std::clamp(width, 1, 4096);
    height = std::clamp(height, 1, 4096);
    const double fit = std::min(1.0, double(std::max(1, maxSide)) / double(std::max(width, height)));
    const int w = std::max(1, int(std::lround(width * fit)));
    const int h = std::max(1, int(std::lround(height * fit)));
    const Image resized = scaled(m_composite, w, h, resampleFor(method));
    const QImage out(resized.data(), resized.width(), resized.height(), resized.stride(), QImage::Format_RGBA8888);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    out.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

void Editor::resizeCanvas(const QRect& bounds)
{
    const Rect r{bounds.x(), bounds.y(), std::clamp(bounds.width(), 1, 4096), std::clamp(bounds.height(), 1, 4096)};
    endStroke();
    commitFloating();
    if (r.x == 0 && r.y == 0 && r.w == m_doc->width() && r.h == m_doc->height())
        return;
    setSelection(Mask{});
    QColor fill = m_secondary;
    fill.setAlpha(255); // background layers stay opaque
    execute(canvasSizeCommand(*m_doc, r, encodeColor(fill)));
    structureChanged();
}

bool Editor::trimCanvas()
{
    endStroke();
    commitFloating();
    const Rect content = contentBounds(*m_doc);
    if (content.isEmpty()) {
        emit errorOccurred(tr("There is nothing to trim to: every frame is empty."));
        return false;
    }
    if (content.x == 0 && content.y == 0 && content.w == m_doc->width() && content.h == m_doc->height())
        return false;
    resizeCanvas(QRect(content.x, content.y, content.w, content.h));
    return true;
}

QString Editor::colorMode() const
{
    switch (m_doc->colorMode()) {
    case ColorMode::Grayscale: return "grayscale";
    case ColorMode::Indexed: return "indexed";
    default: return "rgba";
    }
}

// The dither named by the dialogs ("none", "bayer2", "ordered", "bayer8", "blue", "floyd", "atkinson").
static IndexedDither ditherFor(const QString& name)
{
    return name == "floyd" ? IndexedDither::Floyd
         : name == "atkinson" ? IndexedDither::Atkinson
         : name == "ordered" ? IndexedDither::Ordered
         : name == "bayer2" ? IndexedDither::Bayer2
         : name == "bayer8" ? IndexedDither::Bayer8
         : name == "sierra" ? IndexedDither::Sierra
         : name == "stucki" ? IndexedDither::Stucki
         : name == "riemersma" ? IndexedDither::Riemersma
         : name == "blue" ? IndexedDither::BlueNoise : IndexedDither::None;
}

static DitherOptions ditherOptionsFrom(const QVariantMap& map)
{
    DitherOptions options;
    options.strength = std::clamp(map.value("strength", 100).toInt(), 0, 100);
    options.serpentine = map.value("serpentine").toBool();
    options.oklab = map.value("oklab").toBool();
    return options;
}

std::unique_ptr<SetColorModeCommand> Editor::modeCommand(const QString& mode, const QString& palette,
                                                         const QString& dither, int colors,
                                                         const QVariantList& given, int onlyFrame, bool cachePalette,
                                                         bool keepAlpha, const QVariantMap& ditherOptions)
{
    const ColorMode target = mode == "grayscale" ? ColorMode::Grayscale
                           : mode == "indexed"   ? ColorMode::Indexed
                                                 : ColorMode::Rgba;
    const IndexedDither spread = ditherFor(dither);
    const int maxColors = std::clamp(colors, 1, 255) + 1; // and the transparent entry
    IndexedPalette kind = IndexedPalette::Current;
    Palette chosen;
    if (palette == "given") {
        kind = IndexedPalette::Given;
        for (const QVariant& v : given) {
            const QColor c = v.value<QColor>();
            chosen.entries.push_back({Rgba{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), uint8_t(c.alpha())}, {}});
        }
        if (chosen.entries.empty() || chosen.entries.front().color.a != 0)
            chosen.entries.insert(chosen.entries.begin(), {Rgba{}, {}});
        if (chosen.entries.size() > 256)
            chosen.entries.resize(256);
    } else if (palette == "sprite" && target == ColorMode::Indexed && cachePalette) {
        // The same palette for every dither and frame: made once per count.
        if (m_previewPalette.entries.empty() || m_previewPaletteColors != maxColors || m_previewPaletteAlpha != keepAlpha) {
            m_previewPalette = paletteFromSprite(*m_doc, maxColors, keepAlpha);
            m_previewPaletteColors = maxColors;
            m_previewPaletteAlpha = keepAlpha;
        }
        kind = IndexedPalette::Given;
        chosen = m_previewPalette;
    } else if (palette == "sprite") {
        kind = IndexedPalette::FromSprite;
    }
    return colorModeCommand(*m_doc, target, kind, spread, onlyFrame, maxColors, &chosen, keepAlpha,
                            ditherOptionsFrom(ditherOptions));
}

bool Editor::setColorMode(const QString& mode, const QString& palette, const QString& dither, int colors,
                          const QVariantList& given, bool keepAlpha, const QVariantMap& ditherOptions)
{
    endStroke();
    commitFloating();
    auto command = modeCommand(mode, palette, dither, colors, given, -1, false, keepAlpha, ditherOptions);
    if (!command)
        return false;
    execute(std::move(command));
    structureChanged();
    return true;
}

bool Editor::beginColorModePreview()
{
    endStroke();
    commitFloating();
    cancelEffect();
    m_modePreview = true;
    m_previewPalette = {};
    emit effectChanged();
    return true;
}

QVariantList Editor::previewColorMode(const QString& mode, const QString& palette, const QString& dither,
                                      int colors, const QVariantList& given, bool keepAlpha,
                                      const QVariantMap& ditherOptions)
{
    QVariantList list;
    if (!m_modePreview)
        return list;
    auto command = modeCommand(mode, palette, dither, colors, given, m_currentFrame, true, keepAlpha, ditherOptions);
    if (!command) { // the sprite's own mode: as it is
        invalidateAll();
        return list;
    }
    // Applied just long enough to render the frame, then taken back.
    command->redo(*m_doc);
    Image shown(m_doc->width(), m_doc->height());
    renderFrame(*m_doc, m_currentFrame, shown, m_doc->bounds(), true);
    if (m_doc->colorMode() == ColorMode::Indexed)
        for (const auto& e : m_doc->palette.entries)
            list.append(toQColor(e.color));
    command->undo(*m_doc);
    m_composite.blit(shown, {0, 0});
    m_dirty = m_doc->bounds();
    emit canvasChanged();
    return list;
}

void Editor::cancelColorModePreview()
{
    if (!m_modePreview)
        return;
    m_modePreview = false;
    invalidateAll();
    emit effectChanged();
}

bool Editor::canChangeColorMode() const
{
    return core::canChangeColorMode(*m_doc);
}

int Editor::spriteColorCount(int limit) const
{
    return int(spriteColors(*m_doc, size_t(std::max(0, limit)) + 1).size());
}

void Editor::structureChanged()
{
    rebuildLayerList();
    if (m_doc->colorMode() != m_shownColorMode) { // a conversion, its undo, or a tab switch
        m_shownColorMode = m_doc->colorMode();
        emit colorModeChanged();
        emit clipboardChanged(); // pasting cels depends on the color mode
    }
    if (m_composite.width() != m_doc->width() || m_composite.height() != m_doc->height())
        resetCanvas(); // sprite / canvas size, or its undo
    m_currentFrame = std::clamp(m_currentFrame, 0, m_doc->frameCount() - 1);
    if (m_doc->frameCount() < 2)
        setPlaying(false);
    emit layersChanged();
    emit celsChanged();
    emit tagsChanged();
    emit paletteChanged(); // undo / redo may have changed it
    emit gridChanged();
    if (m_currentSlice >= int(m_doc->slices.size())) // undo took it away
        m_currentSlice = -1;
    emit slicesChanged();
    bumpTileset(); // undo / redo, a tab switch
    if (m_rangeType != NoRange) {
        const Rect all{0, 0, m_doc->frameCount(), int(m_layerList.size())};
        Rect r = m_range.intersected(all);
        if (m_rangeType == FrameRange)
            r = {r.x, 0, r.w, all.h};
        setRange(m_rangeType, r);
    }
    emit currentLayerChanged();
    emit framesChanged();
    emit currentFrameChanged();
    emit frameDurationChanged();
    invalidateAll();
}

// --- layers and frames ------------------------------------------------------

void Editor::addLayer()
{
    insertLayerAboveCurrent(m_doc->makeImageLayer("Layer " + std::to_string(++m_layerCounter)));
}

// The recipe of a new layer or of applying the plain emitter: where the emitter starts, the primary
// color, or the preset placed there.
ParticleRecipe Editor::newParticleRecipe(const QString& presetId) const
{
    double x = std::floor(m_doc->width() / 2.0) + 0.5;
    double y = std::floor(m_doc->height() * 0.7) + 0.5;
    if (const ParticlePreset* preset = findAnyPreset(presetId.toStdString())) {
        x = std::floor(m_doc->width() * preset->homeX) + 0.5;
        y = std::floor(m_doc->height() * preset->homeY) + 0.5;
        return placedPreset(*preset, x, y, presetScaleFor(m_doc->width(), m_doc->height()));
    }
    ParticleRecipe recipe;
    recipe.x = x;
    recipe.y = y;
    recipe.colors = {toRgba(m_primary)};
    return recipe;
}

QVariantList Editor::quickEffectList() const
{
    QVariantList out;
    const QVariantList all = particlePresetList();
    for (const QVariant& item : all) { // the built-in ones first
        QVariantMap map = item.toMap();
        if (map.value("user").toBool())
            continue;
        map["group"] = false;
        out.push_back(map);
    }
    for (const ParticleGroupPreset& g : particleGroupPresets())
        out.push_back(QVariantMap{{"id", QString::fromStdString(g.id)}, {"name", QString::fromStdString(g.name)},
                                  {"description", QString::fromStdString(g.description)}, {"group", true}, {"user", false}});
    for (const QVariant& item : all) { // then the user's own
        QVariantMap map = item.toMap();
        if (!map.value("user").toBool())
            continue;
        map["group"] = false;
        out.push_back(map);
    }
    return out;
}

// Adds a preset, or a group of presets, above the current layer, and the frames it needs, as one step.
void Editor::addQuickEffect(const QString& presetId)
{
    const ParticlePreset* single = findAnyPreset(presetId.toStdString());
    const ParticleGroupPreset* group = findParticleGroupPreset(presetId.toStdString());
    if (!single && !group)
        return;
    endStroke();
    const double scale = presetScaleFor(m_doc->width(), m_doc->height());

    // The recipes of the live layers to add (one for a preset, one for each part of a group), bottom to top.
    struct Item {
        std::string name;
        ParticleRecipe recipe;
    };
    std::vector<Item> items;
    std::string name;
    if (single) {
        name = single->name;
        items.push_back({single->name, newParticleRecipe(presetId)});
    } else {
        name = group->name;
        const double x = std::floor(m_doc->width() * group->homeX) + 0.5;
        const double y = std::floor(m_doc->height() * group->homeY) + 0.5;
        for (const ParticleGroupPreset::Part& part : group->parts)
            if (const ParticlePreset* p = findAnyPreset(part.preset))
                items.push_back({p->name, placedPreset(*p, x + part.dx * scale, y + part.dy * scale, scale)});
    }
    if (items.empty())
        return;

    std::vector<std::unique_ptr<Command>> parts;
    // The frames first, so the layers are drawn for all of them.
    int needed = 0;
    for (const Item& item : items)
        needed = std::max(needed, particleFramesNeeded(item.recipe));
    for (int i = m_doc->frameCount(); i < std::min(needed, 10000); ++i) {
        auto insert = std::make_unique<InsertFrameCommand>(i, m_doc->frame(m_doc->frameCount() - 1));
        insert->redo(*m_doc);
        parts.push_back(std::move(insert));
    }
    const auto makeLive = [&](const Item& item) {
        auto layer = m_doc->makeImageLayer(item.name + " " + std::to_string(++m_layerCounter));
        layer->particles = item.recipe;
        renderParticleLayer(*m_doc, *layer);
        return layer;
    };
    std::unique_ptr<Layer> top;
    Layer* shown = nullptr; // the layer that becomes current: the first live one
    if (single) {
        top = makeLive(items.front());
        shown = top.get();
    } else {
        top = m_doc->makeGroupLayer(name + " " + std::to_string(++m_layerCounter));
        for (const Item& item : items) {
            auto child = makeLive(item);
            child->parent = top.get();
            if (!shown)
                shown = child.get();
            top->children.push_back(std::move(child));
        }
    }
    Layer& parent = m_current && m_current->parent ? *m_current->parent : m_doc->root();
    const int index = m_current ? indexIn(parent, m_current) + 1 : int(parent.children.size());
    auto insertLayer = std::make_unique<InsertLayerCommand>(parent, index, std::move(top));
    insertLayer->redo(*m_doc);
    parts.push_back(std::move(insertLayer));
    pushCommand(std::make_unique<CommandGroup>("Quick " + name, std::move(parts)));
    for (Layer* p = &parent; p->parent; p = p->parent)
        p->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    m_current = shown;
    emit historyChanged();
    structureChanged();
    emit liveChanged();
}

void Editor::addParticleLayer()
{
    addParticleLayerFrom({});
}

void Editor::addParticleLayerFrom(const QString& presetId)
{
    const ParticlePreset* preset = findAnyPreset(presetId.toStdString());
    auto layer = m_doc->makeImageLayer((preset ? preset->name : std::string("Particles")) + " " + std::to_string(++m_layerCounter));
    layer->particles = newParticleRecipe(presetId);
    renderParticleLayer(*m_doc, *layer);
    insertLayerAboveCurrent(std::move(layer));
    emit liveChanged();
}

QPointF Editor::liveEmitter() const
{
    return liveLayer() ? QPointF(m_current->particles->x, m_current->particles->y) : QPointF();
}

void Editor::setLiveEmitter(QPointF position)
{
    if (!liveLayer())
        return;
    ParticleRecipe recipe = *m_current->particles;
    recipe.x = position.x();
    recipe.y = position.y();
    setParticleRecipe(recipe, "Move Emitter", true);
}

ParticleRecipe Editor::particleRecipe() const
{
    return liveLayer() ? *m_current->particles : ParticleRecipe();
}

void Editor::setParticleRecipe(const ParticleRecipe& recipe, const char* label, bool continuous)
{
    Layer* layer = m_current;
    if (!layer || !layer->isLive() || recipe == *layer->particles)
        return;
    const QString key = QStringLiteral("particles:%1:%2").arg(quintptr(layer)).arg(QLatin1String(label));
    const bool merge = continuous ? continuing(key) : (m_mergeKey.clear(), false);
    execute(std::make_unique<SetParticlesCommand>(*layer, *layer->particles, recipe, label), merge);
    emit celsChanged();
    emit liveChanged();
    invalidateAll();
}

namespace {

QVariantMap settingsOf(const ParticleRecipe& r)
{
    QVariantList colors;
    for (const Rgba& c : r.colors)
        colors.push_back(toQColor(c));
    QVariantList fields;
    for (const ParticleField& f : r.fields)
        fields.push_back(QVariantMap{{"type", int(f.type)}, {"x", f.x}, {"y", f.y}, {"strength", f.strength},
                                     {"radius", f.radius}, {"falloff", f.falloff}, {"angle", f.angle},
                                     {"turbulence", f.turbulence}, {"scale", f.scale}, {"mode", f.mode},
                                     {"bounciness", f.bounciness}, {"friction", f.friction}, {"stack", f.stack ? 1 : 0},
                                     {"points", [&] {
                                          QVariantList points; // in canvas pixels
                                          for (const ParticlePoint& p : f.points)
                                              points.push_back(QPointF(f.x + p.x, f.y + p.y));
                                          return points;
                                      }()}});
    return {{"fields", fields},
            {"emission", int(r.emission)},   {"count", r.count},
            {"startFrame", r.startFrame},    {"endFrame", r.endFrame},
            {"perFrame", r.perFrame ? 1 : 0}, {"rate", r.rate}, {"untilEnd", r.untilEnd ? 1 : 0},
            {"loop", r.loop ? 1 : 0},        {"seed", qulonglong(r.seed)},
            {"lifetime", r.lifetime},        {"lifetimeRandom", r.lifetimeRandom},
            {"direction", r.direction},      {"spread", r.spread},
            {"speed", r.speed},              {"speedRandom", r.speedRandom},
            {"gravityX", r.gravityX},        {"gravityY", r.gravityY},
            {"drag", r.drag},                {"sizeStart", r.sizeStart},
            {"sizeEnd", r.sizeEnd},          {"sizeRandom", r.sizeRandom},
            {"sizeEase", int(r.sizeEase)},
            {"sizeCurve", QVariantList{r.sizeCurve.x1, r.sizeCurve.y1, r.sizeCurve.x2, r.sizeCurve.y2}},
            {"opacityCurve", QVariantList{r.opacityCurve.x1, r.opacityCurve.y1, r.opacityCurve.x2, r.opacityCurve.y2}},
            {"shape", int(r.shape)},         {"colorMode", int(r.colorMode)},
            {"brushWidth", r.brush.width}, {"brushHeight", r.brush.height},
            {"source", int(r.source)}, {"sourceWidth", r.sourceWidth}, {"sourceHeight", r.sourceHeight},
            {"sourceAngle", r.sourceAngle}, {"sourceFill", r.sourceFill ? 1 : 0},
            {"brushColors", r.brushColors ? 1 : 0}, {"angle", r.angle}, {"angleRandom", r.angleRandom},
            {"spin", r.spin}, {"spinRandom", r.spinRandom}, {"faceMotion", r.faceMotion ? 1 : 0},
            {"angleSnap", r.angleSnap},
            {"opacityStart", r.opacityStart}, {"opacityEnd", r.opacityEnd},
            {"opacityEase", int(r.opacityEase)}, {"fadePattern", r.fadePattern},
            {"colors", colors}};
}

// Sets one numeric field by name, clamped; false for an unknown name.
bool applyParticleField(ParticleRecipe& r, const QString& key, double value)
{
    const auto whole = [&](int lo, int hi) { return int(std::clamp(std::llround(value), qint64(lo), qint64(hi))); };
    const auto real = [&](double lo, double hi) { return std::clamp(value, lo, hi); };
    if (key == "emission")
        r.emission = whole(0, 1) == 0 ? ParticleRecipe::Emission::Burst : ParticleRecipe::Emission::Stream;
    else if (key == "count")
        r.count = whole(1, 5000);
    else if (key == "startFrame")
        r.startFrame = whole(-5000, 100000);
    else if (key == "perFrame")
        r.perFrame = value != 0;
    else if (key == "rate")
        r.rate = real(0.05, 500);
    else if (key == "untilEnd")
        r.untilEnd = value != 0;
    else if (key == "endFrame")
        r.endFrame = whole(0, 100000);
    else if (key == "loop")
        r.loop = value != 0;
    else if (key == "seed")
        r.seed = uint32_t(whole(0, 2147483647));
    else if (key == "lifetime")
        r.lifetime = whole(1, 1000);
    else if (key == "lifetimeRandom")
        r.lifetimeRandom = whole(0, 100);
    else if (key == "direction")
        r.direction = real(-360, 360);
    else if (key == "spread")
        r.spread = real(0, 360);
    else if (key == "speed")
        r.speed = real(0, 200);
    else if (key == "speedRandom")
        r.speedRandom = whole(0, 100);
    else if (key == "gravityX")
        r.gravityX = real(-20, 20);
    else if (key == "gravityY")
        r.gravityY = real(-20, 20);
    else if (key == "drag")
        r.drag = real(0, 1);
    else if (key == "sizeStart")
        r.sizeStart = real(0, 256);
    else if (key == "sizeEnd")
        r.sizeEnd = real(0, 256);
    else if (key == "sizeRandom")
        r.sizeRandom = whole(0, 100);
    else if (key == "sizeEase") { // Custom starts from the curve of the easing it had
        const auto ease = ParticleRecipe::Ease(whole(0, 4));
        if (ease == ParticleRecipe::Ease::Custom && r.sizeEase != ease)
            r.sizeCurve = core::namedEasing(int(r.sizeEase));
        r.sizeEase = ease;
    }
    else if (key == "opacityStart")
        r.opacityStart = whole(0, 100);
    else if (key == "opacityEnd")
        r.opacityEnd = whole(0, 100);
    else if (key == "opacityEase") {
        const auto ease = ParticleRecipe::Ease(whole(0, 4));
        if (ease == ParticleRecipe::Ease::Custom && r.opacityEase != ease)
            r.opacityCurve = core::namedEasing(int(r.opacityEase));
        r.opacityEase = ease;
    }
    else if (key == "fadePattern")
        r.fadePattern = whole(0, 4);
    else if (key == "source")
        r.source = ParticleRecipe::Source(whole(0, 3));
    else if (key == "sourceWidth")
        r.sourceWidth = whole(0, 4096);
    else if (key == "sourceHeight")
        r.sourceHeight = whole(0, 4096);
    else if (key == "sourceAngle")
        r.sourceAngle = real(-360, 360);
    else if (key == "sourceFill")
        r.sourceFill = value != 0;
    else if (key == "brushColors")
        r.brushColors = value != 0;
    else if (key == "angle")
        r.angle = real(-360, 360);
    else if (key == "angleRandom")
        r.angleRandom = whole(0, 180);
    else if (key == "spin")
        r.spin = real(-360, 360);
    else if (key == "spinRandom")
        r.spinRandom = whole(0, 100);
    else if (key == "faceMotion")
        r.faceMotion = value != 0;
    else if (key == "angleSnap")
        r.angleSnap = whole(0, 3);
    else if (key == "shape")
        r.shape = ParticleRecipe::Shape(whole(0, 4));
    else if (key == "colorMode")
        r.colorMode = whole(0, 1) == 0 ? ParticleRecipe::ColorMode::OverLife : ParticleRecipe::ColorMode::Random;
    else
        return false;
    // The last field changed wins when a pair crosses.
    if (r.endFrame < r.startFrame)
        (key == "startFrame" ? r.endFrame = r.startFrame : r.startFrame = r.endFrame);
    return true;
}

} // namespace

QVariantMap Editor::particleSettings() const
{
    if (!liveLayer())
        return {};
    const ParticleRecipe& now = *m_current->particles;
    const bool child = particleContext() == 1;
    QVariantMap out = settingsOf(child ? now.children.front() : now);
    out["context"] = child ? 1 : 0;
    out["subTrigger"] = int(now.subTrigger);
    out["subInherit"] = now.subInherit;
    out["hasSub"] = now.children.empty() ? 0 : 1;
    const ParticlePreset* preset = findAnyPreset(now.preset);
    out["preset"] = QString::fromStdString(now.preset);
    out["presetName"] = preset && !child ? QString::fromStdString(preset->name) : QString();
    out["presetEdited"] = preset && differsFromReference(now, referenceRecipe()) ? 1 : 0;
    return out;
}

QVariantMap Editor::particleDefaults() const
{
    return settingsOf(ParticleRecipe());
}

ParticleRecipe Editor::referenceRecipe() const
{
    const ParticleRecipe& now = *m_current->particles;
    ParticleRecipe base;
    if (const ParticlePreset* preset = findAnyPreset(now.preset))
        base = placedPreset(*preset, now.x, now.y, now.presetScale);
    base.x = now.x;
    base.y = now.y;
    base.seed = now.seed;
    return base;
}

namespace {

// A new sub-particle's recipe: a small burst of pale specks thrown up and all round that fall back and fade.
ParticleRecipe defaultSubRecipe()
{
    ParticleRecipe c;
    c.emission = ParticleRecipe::Emission::Burst;
    c.count = 4;
    c.lifetime = 8;
    c.lifetimeRandom = 20;
    c.direction = -90;
    c.spread = 160;
    c.speed = 1.2;
    c.speedRandom = 30;
    c.gravityY = 0.15;
    c.sizeStart = c.sizeEnd = 1;
    c.opacityStart = 100;
    c.opacityEnd = 0;
    c.opacityEase = ParticleRecipe::Ease::In;
    c.colors = {Rgba{255, 255, 255, 255}};
    return c;
}

} // namespace

int Editor::particleContext() const
{
    return liveLayer() && m_particleContext == 1 && !m_current->particles->children.empty() ? 1 : 0;
}

void Editor::setParticleContext(int context)
{
    context = context == 1 && liveLayer() && !m_current->particles->children.empty() ? 1 : 0;
    if (m_particleContext == context)
        return;
    m_particleContext = context;
    emit liveChanged();
}

QVariantMap Editor::particleReference() const
{
    if (!liveLayer())
        return settingsOf(ParticleRecipe());
    const ParticleRecipe base = referenceRecipe();
    if (particleContext() == 1)
        return settingsOf(base.children.empty() ? defaultSubRecipe() : base.children.front());
    return settingsOf(base);
}

QVariantList Editor::particlePresetList() const
{
    QVariantList out;
    for (const ParticlePreset& p : particlePresets())
        out.push_back(QVariantMap{{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)},
                                  {"description", QString::fromStdString(p.description)}, {"user", false}});
    for (const QVariant& u : userPresets()) {
        QVariantMap map = u.toMap();
        map["description"] = QString();
        map["user"] = true;
        out.push_back(map);
    }
    return out;
}

QString Editor::presetFolder() const
{
    return m_presetFolder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/presets"
                                    : m_presetFolder;
}

void Editor::setPresetFolder(const QString& folder)
{
    m_presetFolder = folder;
    m_userPresetsLoaded = false;
    m_userSmearPresetsLoaded = false;
    emit presetsChanged();
}

// A preset file: {"name", "data": the recipe as the user data property of a layer, base64, "homeX", "homeY"}.
void Editor::loadUserPresets() const
{
    if (m_userPresetsLoaded)
        return;
    m_userPresetsLoaded = true;
    m_userPresets.clear();
    const QDir dir(presetFolder());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.smaltopreset")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        const QByteArray bytes = QByteArray::fromBase64(json.value("data").toString().toLatin1());
        const auto stored = formats::storedRecipeIn(core::Bytes(bytes.begin(), bytes.end()));
        if (!stored || json.value("name").toString().isEmpty())
            continue;
        ParticlePreset p;
        p.id = "user:" + info.completeBaseName().toStdString();
        p.name = json.value("name").toString().toStdString();
        p.homeX = json.value("homeX").toDouble(0.5);
        p.homeY = json.value("homeY").toDouble(0.7);
        p.recipe = stored->recipe;
        p.recipe.preset = p.id;
        m_userPresets.push_back(std::move(p));
    }
}

const ParticlePreset* Editor::findAnyPreset(const std::string& id) const
{
    if (const ParticlePreset* p = findParticlePreset(id))
        return p;
    loadUserPresets();
    for (const ParticlePreset& p : m_userPresets)
        if (p.id == id)
            return &p;
    return nullptr;
}

QVariantList Editor::userPresets() const
{
    loadUserPresets();
    QVariantList out;
    for (const ParticlePreset& p : m_userPresets)
        out.push_back(QVariantMap{{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)}});
    return out;
}

bool Editor::saveParticlePreset(const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (!liveLayer() || name.isEmpty())
        return false;
    loadUserPresets();
    ParticleRecipe r = *m_current->particles;
    // Back to a sprite 64 pixels tall, and the places relative to the emitter.
    const double made = r.preset.empty() ? presetScaleFor(m_doc->width(), m_doc->height()) : r.presetScale;
    const double ex = r.x, ey = r.y;
    for (ParticleField& f : r.fields) {
        f.x -= ex;
        f.y -= ey;
    }
    r.x = r.y = 0;
    r = scaledRecipe(r, 1.0 / made);
    r.presetScale = 1;
    r.preset.clear();

    // The same name replaces the old one; else a file name from it that isn't taken.
    QString stem;
    for (const ParticlePreset& p : m_userPresets)
        if (QString::fromStdString(p.name).compare(name, Qt::CaseInsensitive) == 0)
            stem = QString::fromStdString(p.id).mid(5);
    if (stem.isEmpty()) {
        QString base;
        for (const QChar c : name.toLower())
            base += c.isLetterOrNumber() ? c : QLatin1Char('-');
        base = base.left(32);
        stem = base.isEmpty() ? QStringLiteral("preset") : base;
        const QDir dir(presetFolder());
        for (int n = 2; dir.exists(stem + ".smaltopreset"); ++n)
            stem = base + "-" + QString::number(n);
    }
    const core::Bytes bytes = formats::withStoredRecipe({}, r, 0);
    QJsonObject json;
    json["name"] = name;
    json["homeX"] = std::clamp(ex / m_doc->width(), 0.0, 1.0);
    json["homeY"] = std::clamp(ey / m_doc->height(), 0.0, 1.0);
    json["data"] = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toBase64());
    QDir().mkpath(presetFolder());
    QSaveFile file(presetFolder() + "/" + stem + ".smaltopreset");
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(json).toJson());
    if (!file.commit())
        return false;
    m_userPresetsLoaded = false;
    // The layer is that preset from now on (edits to it are measured against what was saved).
    ParticleRecipe now = *m_current->particles;
    now.preset = "user:" + stem.toStdString();
    now.presetScale = made;
    loadUserPresets();
    setParticleRecipe(now, "Save Preset", false);
    emit presetsChanged();
    return true;
}

void Editor::deleteUserPreset(const QString& id)
{
    if (!id.startsWith("user:"))
        return;
    QFile::remove(presetFolder() + "/" + id.mid(5) + ".smaltopreset");
    m_userPresetsLoaded = false;
    emit presetsChanged();
    emit liveChanged();
}

void Editor::applyParticlePreset(const QString& presetId)
{
    if (!liveLayer())
        return;
    const ParticleRecipe& now = *m_current->particles;
    ParticleRecipe r;
    if (const ParticlePreset* preset = findAnyPreset(presetId.toStdString())) {
        r = placedPreset(*preset, now.x, now.y, presetScaleFor(m_doc->width(), m_doc->height())); // its own seed
    } else { // the plain emitter, in the primary color
        r.x = now.x;
        r.y = now.y;
        r.seed = now.seed;
        r.colors = {toRgba(m_primary)};
    }
    setParticleRecipe(r, "Apply Preset", false);
}

void Editor::setParticleField(const QString& key, double value, bool continuous)
{
    if (!liveLayer())
        return;
    ParticleRecipe r = *m_current->particles;
    if (key == "subTrigger") { // the first time it is turned on, the sub-particles get a recipe to edit
        r.subTrigger = ParticleRecipe::Trigger(std::clamp(int(std::llround(value)), 0, 2));
        if (r.subTrigger != ParticleRecipe::Trigger::None && r.children.empty())
            r.children = {defaultSubRecipe()};
        setParticleRecipe(r, "Particle Settings", continuous);
        return;
    }
    if (key == "subInherit") {
        r.subInherit = std::clamp(int(std::llround(value)), 0, 100);
        setParticleRecipe(r, "Particle Settings", continuous);
        return;
    }
    ParticleRecipe& target = particleContext() == 1 ? r.children.front() : r;
    if (!applyParticleField(target, key, value))
        return;
    setParticleRecipe(r, key == "seed" ? "Particle Seed" : "Particle Settings", continuous);
}

void Editor::resetParticleFields(const QStringList& keys)
{
    if (!liveLayer())
        return;
    ParticleRecipe root = *m_current->particles;
    const bool child = particleContext() == 1;
    const ParticleRecipe reference = referenceRecipe(); // the preset it started from, else the built-in values
    ParticleRecipe& r = child ? root.children.front() : root;
    const ParticleRecipe base = child ? (reference.children.empty() ? defaultSubRecipe() : reference.children.front()) : reference;
    const QVariantMap defaults = settingsOf(base);
    for (const QString& key : keys) {
        if (key == "colors")
            r.colors = base.colors;
        else if (key == "fields")
            r.fields = base.fields;
        else
            applyParticleField(r, key, defaults.value(key).toDouble());
    }
    setParticleRecipe(root, "Reset Particle Settings", false);
}

void Editor::setParticleColors(const QVariantList& colors)
{
    if (!liveLayer() || colors.isEmpty())
        return;
    ParticleRecipe root = *m_current->particles;
    ParticleRecipe& r = particleContext() == 1 ? root.children.front() : root;
    r.colors.clear();
    for (const QVariant& c : colors)
        r.colors.push_back(toRgba(c.value<QColor>()));
    setParticleRecipe(root, "Particle Colors", false);
}

void Editor::setParticleRange(int start, int end, bool continuous)
{
    if (!liveLayer())
        return;
    ParticleRecipe r = *m_current->particles;
    r.startFrame = std::clamp(start, -5000, 100000);
    r.endFrame = std::clamp(end, r.startFrame, 100000);
    setParticleRecipe(r, "Particle Frames", continuous);
}

bool Editor::useCustomBrushAsParticle()
{
    if (!liveLayer() || !hasCustomBrush())
        return false;
    const Image& b = m_customBrush;
    if (b.width() > 128 || b.height() > 128)
        return false;
    ParticleBrush brush;
    brush.width = b.width();
    brush.height = b.height();
    brush.pixels.resize(size_t(brush.width) * size_t(brush.height));
    for (int y = 0; y < brush.height; ++y)
        for (int x = 0; x < brush.width; ++x)
            brush.pixels[size_t(y) * size_t(brush.width) + size_t(x)] = b.pixel(x, y);
    ParticleRecipe r = *m_current->particles;
    const ParticleRecipe untouched;
    // The size is the brush's longest side: a brush taken at the starting size of 1 px would shrink to
    // a single pixel of itself (often an empty one), so an untouched size becomes the brush's own.
    if (r.sizeStart == untouched.sizeStart && r.sizeEnd == untouched.sizeEnd)
        r.sizeStart = r.sizeEnd = std::max(brush.width, brush.height);
    r.brush = std::move(brush);
    r.shape = ParticleRecipe::Shape::Brush;
    setParticleRecipe(r, "Particle Brush", false);
    return true;
}

int Editor::addParticleField(int type)
{
    return addFieldImpl(type, false, 0, 0);
}

int Editor::addFieldImpl(int type, bool placed, double x, double y)
{
    if (!liveLayer() || m_current->particles->fields.size() >= 8 || type < 0 || type > 4)
        return -1;
    ParticleRecipe r = *m_current->particles;
    ParticleField field;
    field.type = ParticleField::Type(type);
    if (field.type == ParticleField::Type::Wind)
        field.strength = 0.1;
    field.x = r.x;
    field.y = r.y - 24; // above the emitter, where particles are going
    if (field.type == ParticleField::Type::Floor)
        field.y = r.y + 24; // a floor is below them
    if (field.type == ParticleField::Type::Collider) {
        // A small roof below the emitter, sized to the sprite: what lands on it slides off both ways.
        const double half = std::clamp(std::round(std::min(m_doc->width(), m_doc->height()) / 6.0), 4.0, 64.0);
        const double drop = std::round(half * 0.4);
        field.y = r.y + 24;
        field.points = {{-half, drop}, {0, 0}, {half, drop}};
        field.bounciness = 0;
    }
    if (placed) {
        field.x = x;
        field.y = y;
    }
    r.fields.push_back(field);
    setParticleRecipe(r, "Add Field", false);
    return int(r.fields.size()) - 1;
}

void Editor::removeParticleField(int index)
{
    if (!liveLayer() || index < 0 || index >= int(m_current->particles->fields.size()))
        return;
    ParticleRecipe r = *m_current->particles;
    r.fields.erase(r.fields.begin() + index);
    setParticleRecipe(r, "Remove Field", false);
}

void Editor::setParticleFieldValue(int index, const QString& key, double value, bool continuous)
{
    if (!liveLayer() || index < 0 || index >= int(m_current->particles->fields.size()))
        return;
    ParticleRecipe r = *m_current->particles;
    ParticleField& f = r.fields[size_t(index)];
    if (key == "x")
        f.x = std::clamp(value, -4096.0, 4096.0);
    else if (key == "y")
        f.y = std::clamp(value, -4096.0, 4096.0);
    else if (key == "strength")
        f.strength = std::clamp(value, -10.0, 10.0);
    else if (key == "radius")
        f.radius = int(std::clamp(std::llround(value), qint64(0), qint64(4096)));
    else if (key == "falloff")
        f.falloff = value != 0 ? 1 : 0;
    else if (key == "angle")
        f.angle = std::clamp(value, -360.0, 360.0);
    else if (key == "turbulence")
        f.turbulence = int(std::clamp(std::llround(value), qint64(0), qint64(100)));
    else if (key == "mode")
        f.mode = int(std::clamp(std::llround(value), qint64(0), qint64(2)));
    else if (key == "bounciness")
        f.bounciness = int(std::clamp(std::llround(value), qint64(0), qint64(100)));
    else if (key == "stack")
        f.stack = value != 0;
    else if (key == "friction")
        f.friction = int(std::clamp(std::llround(value), qint64(0), qint64(100)));
    else if (key == "scale")
        f.scale = int(std::clamp(std::llround(value), qint64(1), qint64(512)));
    else
        return;
    setParticleRecipe(r, "Particle Field", continuous);
}

void Editor::setColliderPoint(int index, int point, double x, double y, bool continuous)
{
    if (!liveLayer() || index < 0 || index >= int(m_current->particles->fields.size()))
        return;
    ParticleRecipe r = *m_current->particles;
    ParticleField& f = r.fields[size_t(index)];
    if (f.type != ParticleField::Type::Collider || point < 0 || point >= int(f.points.size()))
        return;
    f.points[size_t(point)] = {std::clamp(x, -4096.0, 4096.0) - f.x, std::clamp(y, -4096.0, 4096.0) - f.y};
    setParticleRecipe(r, "Collider Point", continuous);
}

int Editor::insertColliderPoint(int index, int at, double x, double y)
{
    if (!liveLayer() || index < 0 || index >= int(m_current->particles->fields.size()))
        return -1;
    ParticleRecipe r = *m_current->particles;
    ParticleField& f = r.fields[size_t(index)];
    if (f.type != ParticleField::Type::Collider || at < 0 || at > int(f.points.size()) || f.points.size() >= 32)
        return -1;
    f.points.insert(f.points.begin() + at, ParticlePoint{std::clamp(x, -4096.0, 4096.0) - f.x,
                                                         std::clamp(y, -4096.0, 4096.0) - f.y});
    setParticleRecipe(r, "Collider Point", true); // the drag that follows is the same step
    return at;
}

void Editor::removeColliderPoint(int index, int point)
{
    if (!liveLayer() || index < 0 || index >= int(m_current->particles->fields.size()))
        return;
    ParticleRecipe r = *m_current->particles;
    ParticleField& f = r.fields[size_t(index)];
    if (f.type != ParticleField::Type::Collider || point < 0 || point >= int(f.points.size()) || f.points.size() <= 2)
        return;
    f.points.erase(f.points.begin() + point);
    setParticleRecipe(r, "Remove Collider Point", false);
}

void Editor::setParticleFieldPosition(int index, double x, double y, bool continuous)
{
    if (!liveLayer() || index < 0 || index >= int(m_current->particles->fields.size()))
        return;
    ParticleRecipe r = *m_current->particles;
    r.fields[size_t(index)].x = std::clamp(x, -4096.0, 4096.0);
    r.fields[size_t(index)].y = std::clamp(y, -4096.0, 4096.0);
    setParticleRecipe(r, "Particle Field", continuous);
}

void Editor::setLiveSourceKind(int kind)
{
    kind = std::clamp(kind, 1, 3);
    if (m_liveSourceKind == kind)
        return;
    m_liveSourceKind = kind;
    emit toolOptionsChanged();
}

void Editor::setLiveFieldKind(int kind)
{
    kind = std::clamp(kind, 0, 4);
    if (m_liveFieldKind == kind)
        return;
    m_liveFieldKind = kind;
    emit toolOptionsChanged();
}

// The source tool: the drag's two corners are a rectangle or an oval, its two ends a line. The emitter
// goes to the middle. Shift makes a square / circle (or snaps a line to 15 degrees), Alt draws from the
// middle outward. Every step replaces the one before it, and the whole drag is one undo step.
void Editor::beginSourceDraw(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    if (!liveLayer() || !effectivelyVisible(*m_current))
        return;
    m_stroke = {};
    m_stroke.kind = Stroke::Source;
    m_stroke.active = true;
    m_stroke.modifiers = modifiers;
    m_stroke.start = m_stroke.last = toPoint(pixel);
}

void Editor::dragSourceTo(QPoint pixel)
{
    if (!liveLayer())
        return;
    const Point a = m_stroke.start;
    const Point b = toPoint(pixel);
    const bool fromMiddle = m_stroke.modifiers & Qt::AltModifier;
    const bool even = m_stroke.modifiers & Qt::ShiftModifier;
    ParticleRecipe r = *m_current->particles;
    r.source = ParticleRecipe::Source(m_liveSourceKind);
    double dx = b.x - a.x, dy = b.y - a.y;
    if (r.source == ParticleRecipe::Source::Line) {
        double angle = std::atan2(dy, dx) * 180.0 / M_PI;
        const double length = std::hypot(dx, dy);
        if (even) {
            angle = std::round(angle / 15.0) * 15.0;
            dx = std::cos(angle * M_PI / 180.0) * length;
            dy = std::sin(angle * M_PI / 180.0) * length;
        }
        r.sourceAngle = std::round(angle);
        r.sourceWidth = std::max(1, int(std::lround(fromMiddle ? 2 * length + 1 : length + 1)));
        r.x = fromMiddle ? a.x + 0.5 : a.x + dx / 2 + 0.5;
        r.y = fromMiddle ? a.y + 0.5 : a.y + dy / 2 + 0.5;
    } else {
        int w = fromMiddle ? 2 * std::abs(b.x - a.x) + 1 : std::abs(b.x - a.x) + 1;
        int h = fromMiddle ? 2 * std::abs(b.y - a.y) + 1 : std::abs(b.y - a.y) + 1;
        if (even)
            w = h = std::max(w, h);
        const int x0 = fromMiddle ? a.x - w / 2 : (b.x >= a.x ? a.x : a.x - w + 1);
        const int y0 = fromMiddle ? a.y - h / 2 : (b.y >= a.y ? a.y : a.y - h + 1);
        r.sourceWidth = w;
        r.sourceHeight = h;
        r.sourceAngle = 0;
        r.x = x0 + w / 2.0;
        r.y = y0 + h / 2.0;
    }
    setParticleRecipe(r, "Draw Source", true);
}

// The field tool: a click puts a field of the chosen kind there (and picks it); dragging moves it.
void Editor::placeFieldAt(QPoint pixel)
{
    if (!liveLayer() || !effectivelyVisible(*m_current))
        return;
    const int index = addFieldImpl(m_liveFieldKind, true, pixel.x() + 0.5, pixel.y() + 0.5);
    if (index < 0)
        return;
    m_stroke = {};
    m_stroke.kind = Stroke::Field;
    m_stroke.active = true;
    m_stroke.start = m_stroke.last = toPoint(pixel);
    m_stroke.celStart = {index, 0};
    emit liveFieldPlaced(index);
}

void Editor::reseedParticles()
{
    if (!liveLayer())
        return;
    ParticleRecipe root = *m_current->particles;
    ParticleRecipe& r = particleContext() == 1 ? root.children.front() : root;
    r.seed = uint32_t(QRandomGenerator::global()->bounded(1, 2147483647));
    setParticleRecipe(root, "Reseed Particles", false);
}

QVariantList Editor::staleLiveLayers() const
{
    QVariantList out;
    int index = 0;
    m_doc->forEachLayer([&](const Layer& layer) {
        if (layer.staleRecipe)
            out.push_back(QVariantMap{{"index", index}, {"name", QString::fromStdString(layer.name)}});
        ++index;
    });
    return out;
}

void Editor::resolveStaleLive(int index, bool renderAgain)
{
    Layer* target = nullptr;
    int at = 0;
    m_doc->forEachLayer([&](Layer& layer) {
        if (at++ == index)
            target = &layer;
    });
    if (!target || !target->staleRecipe)
        return;
    execute(std::make_unique<ResolveStaleCommand>(*target, renderAgain, formats::withoutStoredRecipe(target->userData)));
    structureChanged();
    emit celsChanged();
    emit liveChanged();
    invalidateAll();
}

void Editor::bakeLayer()
{
    if (m_current && m_current->isSmear()) { // the group becomes an image layer with what it showed
        Layer& parent = *m_current->parent;
        const int index = indexIn(parent, m_current);
        auto baked = core::bakeSmearGroup(*m_doc, *m_current);
        baked->userData = formats::withoutStoredRecipe(m_current->userData);
        Layer* made = baked.get();
        std::vector<std::unique_ptr<Command>> parts;
        parts.push_back(std::make_unique<RemoveLayerCommand>(parent, index));
        parts.push_back(std::make_unique<InsertLayerCommand>(parent, index, std::move(baked)));
        for (auto& part : parts)
            part->redo(*m_doc);
        pushCommand(std::make_unique<CommandGroup>("Bake Layer", std::move(parts)));
        emit historyChanged();
        m_current = made;
        structureChanged();
        return;
    }
    if (!liveLayer())
        return;
    execute(std::make_unique<BakeLayerCommand>(*m_current, formats::withoutStoredRecipe(m_current->userData)));
    structureChanged();
    emit liveChanged();
}

void Editor::bakeToNewLayer()
{
    if (m_current && m_current->isSmear()) {
        auto baked = core::bakeSmearGroup(*m_doc, *m_current);
        baked->name += " baked";
        insertLayerAboveCurrent(std::move(baked));
        return;
    }
    if (!liveLayer())
        return;
    auto copy = cloneLayer(*m_current);
    copy->particles.reset();
    copy->userData = formats::withoutStoredRecipe(copy->userData);
    copy->name += " baked";
    insertLayerAboveCurrent(std::move(copy));
    emit liveChanged();
}

// New layers go right above the current one, in the same group, and become
// current. Undoable.
Layer* Editor::insertLayerAboveCurrent(std::unique_ptr<Layer> layer)
{
    Layer& parent = m_current && m_current->parent ? *m_current->parent : m_doc->root();
    const int index = m_current ? indexIn(parent, m_current) + 1 : int(parent.children.size());
    Layer* added = layer.get();
    execute(std::make_unique<InsertLayerCommand>(parent, index, std::move(layer)));
    for (Layer* p = &parent; p->parent; p = p->parent)
        p->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    m_current = added;
    structureChanged();
    return added;
}

// Removes the current layer (a group goes with its children) and selects
// the one below it.
// Several layers at once (the selected block), one undo step. The document keeps
// at least one top-level layer: if every one of them is selected, the one that
// holds the current layer stays.
void Editor::removeLayers(std::vector<Layer*> layers)
{
    Layer& root = m_doc->root();
    Layer* keep = nullptr;
    const auto selectedCount = std::count_if(layers.begin(), layers.end(), [&](const Layer* l) { return l->parent == &root; });
    if (size_t(selectedCount) >= root.children.size()) {
        keep = m_current;
        while (keep && keep->parent != &root)
            keep = keep->parent;
        std::erase(layers, keep);
    }
    if (layers.empty())
        return;
    commitFloating();
    // Where the current layer goes when it is among the removed.
    Layer& anchorParent = *layers.front()->parent;
    const int anchorIndex = indexIn(anchorParent, layers.front());
    std::vector<std::unique_ptr<Command>> parts;
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) { // top first: lower indices stay valid
        auto c = std::make_unique<RemoveLayerCommand>(*(*it)->parent, indexIn(*(*it)->parent, *it));
        c->redo(*m_doc);
        parts.push_back(std::move(c));
    }
    pushCommand(std::make_unique<CommandGroup>("Delete Layers", std::move(parts)));
    emit historyChanged();
    if (keep) {
        m_current = keep;
    } else if (!anchorParent.children.empty()) {
        const int below = std::max(0, anchorIndex - 1);
        m_current = anchorParent.children[size_t(std::min(below, int(anchorParent.children.size()) - 1))].get();
    } else {
        m_current = &anchorParent;
    }
    setRange(NoRange, {});
    structureChanged();
}

void Editor::removeLayer()
{
    if (!canRemoveLayer())
        return;
    // A block of layers taken in the layers list or the timeline goes together.
    if (std::vector<Layer*> layers = selectedLayers(); layers.size() > 1) {
        removeLayers(std::move(layers));
        return;
    }
    Layer& parent = *m_current->parent;
    const int index = indexIn(parent, m_current);
    Layer* next = index > 0                       ? parent.children[size_t(index) - 1].get()
                : parent.children.size() > 1      ? parent.children[1].get()
                : &parent != &m_doc->root()       ? &parent
                                                  : nullptr;
    execute(std::make_unique<RemoveLayerCommand>(parent, index));
    m_current = next;
    structureChanged();
}

void Editor::setLayerVisible(int index, bool visible)
{
    setLayerProps(layerAt(index), [&](LayerProps& p) { p.visible = visible; }, visible ? "Show Layer" : "Hide Layer");
}

void Editor::setLayerName(int index, const QString& name)
{
    setLayerProps(layerAt(index), [&](LayerProps& p) { p.name = name.toStdString(); }, "Rename Layer");
}

void Editor::setLayerLocked(int index, bool locked)
{
    setLayerProps(layerAt(index), [&](LayerProps& p) { p.locked = locked; }, locked ? "Lock Layer" : "Unlock Layer");
}

const Layer* Editor::topLayerAt(QPoint pixel) const
{
    // forEachLayer goes bottom to top; the last hit is the topmost.
    const Layer* hit = nullptr;
    m_doc->forEachLayer([&](const Layer& layer) {
        if (layer.kind != LayerKind::Image || !effectivelyVisible(layer) || layer.opacity == 0)
            return;
        const CelRef cel = layer.cel(m_currentFrame);
        if (!cel || !cel->image)
            return;
        const int x = pixel.x() - cel->position.x, y = pixel.y() - cel->position.y;
        if (cel->image->bounds().contains(x, y) && core::toRgba(*m_doc, *cel->image, x, y).a > 0
            && std::find(m_layerList.begin(), m_layerList.end(), &layer) != m_layerList.end())
            hit = &layer;
    });
    return hit;
}

QString Editor::layerNameAt(QPoint pixel) const
{
    const Layer* layer = topLayerAt(pixel);
    return layer ? QString::fromStdString(layer->name) : QString();
}

bool Editor::selectLayerAt(QPoint pixel)
{
    const Layer* hit = topLayerAt(pixel);
    if (!hit)
        return false;
    setCurrentLayer(int(std::find(m_layerList.begin(), m_layerList.end(), hit) - m_layerList.begin()));
    return true;
}

void Editor::setLayerAlphaLock(int index, bool on)
{
    setLayerProps(layerAt(index), [&](LayerProps& p) { p.alphaLock = on; }, on ? "Alpha Lock" : "Alpha Unlock");
}

void Editor::setLayerOpacity(int index, int percent)
{
    const int value = (std::clamp(percent, 0, 100) * 255 + 50) / 100;
    setLayerProps(layerAt(index), [&](LayerProps& p) { p.opacity = value; }, "Layer Opacity", true);
}

void Editor::setLayerBlend(int index, int mode)
{
    if (mode < 0 || mode >= kBlendModeCount)
        return;
    setLayerProps(layerAt(index), [&](LayerProps& p) { p.blend = BlendMode(mode); }, "Blend Mode");
}

QStringList Editor::blendModes() const
{
    QStringList names;
    for (int i = 0; i < kBlendModeCount; ++i)
        names.append(QString::fromLatin1(blendModeName(BlendMode(i))));
    return names;
}

void Editor::addGroup()
{
    int groups = 0;
    m_doc->forEachLayer([&](const Layer& l) { groups += l.kind == LayerKind::Group; });
    insertLayerAboveCurrent(m_doc->makeGroupLayer("Group " + std::to_string(groups + 1)));
}

void Editor::duplicateLayer()
{
    if (!m_current)
        return;
    auto copy = cloneLayer(*m_current);
    copy->name += " copy";
    insertLayerAboveCurrent(std::move(copy));
}

bool Editor::picksActive() const
{
    return m_picks.size() > 1 && std::find(m_picks.begin(), m_picks.end(), m_current) != m_picks.end();
}

bool Editor::isPicked(int layerIndex) const
{
    const Layer* layer = layerAt(layerIndex);
    return layer && picksActive() && std::find(m_picks.begin(), m_picks.end(), layer) != m_picks.end();
}

void Editor::togglePick(int layerIndex)
{
    Layer* layer = layerAt(layerIndex);
    if (!layer)
        return;
    commitFloating();
    // What is selected now, unfiltered: the picks, a block, or the current layer.
    std::vector<Layer*> set;
    if (picksActive()) {
        set = m_picks;
    } else if (m_rangeType == CelRange) {
        for (int row = m_range.y; row < m_range.bottom() && row < int(m_layerList.size()); ++row)
            set.push_back(m_layerList[size_t(row)]);
    } else if (m_current) {
        set.push_back(m_current);
    }
    const auto at = std::find(set.begin(), set.end(), layer);
    const bool added = at == set.end();
    if (added)
        set.push_back(layer);
    else if (set.size() > 1)
        set.erase(at);
    else
        return; // the last one stays
    const auto position = [&](const Layer* l) { return std::find(m_layerList.begin(), m_layerList.end(), l) - m_layerList.begin(); };
    std::sort(set.begin(), set.end(), [&](const Layer* a, const Layer* b) { return position(a) < position(b); });
    setRange(NoRange, {}); // a new selection: the block and old picks go
    if (set.size() == 1) {
        m_picks.clear();
        setCurrentLayer(int(position(set[0])));
        emit rangeChanged();
        return;
    }
    // The current layer is the one just added, or stays unless it was the one taken out.
    Layer* current = added ? layer : (std::find(set.begin(), set.end(), m_current) != set.end() ? m_current : set.back());
    m_picks = std::move(set);
    m_current = current;
    m_currentRow = int(position(current));
    emit currentLayerChanged();
    bumpTileset();
    emit rangeChanged();
}

std::vector<Layer*> Editor::selectedLayers() const
{
    std::vector<Layer*> out;
    if (picksActive()) {
        out = m_picks;
    } else if (m_rangeType == CelRange) {
        for (int row = m_range.y; row < m_range.bottom() && row < int(m_layerList.size()); ++row)
            out.push_back(m_layerList[size_t(row)]);
    } else if (m_current) {
        out.push_back(m_current);
    }
    // A layer whose group is selected too goes with the group.
    std::erase_if(out, [&](const Layer* l) {
        for (const Layer* p = l->parent; p; p = p->parent)
            if (std::find(out.begin(), out.end(), p) != out.end())
                return true;
        return false;
    });
    return out; // m_layerList order: bottom to top
}

// Several layers next to / into `onto` (zones as moveLayer), keeping their
// order; one undo step.
bool Editor::moveLayers(const std::vector<Layer*>& layers, Layer* onto, int zone)
{
    for (const Layer* l : layers)
        if (l == onto || isWithin(*onto, *l))
            return false; // onto one of them, or into one of them
    Layer* to;
    if (zone == 2 || (zone == 1 && onto->kind == LayerKind::Group && !onto->isCollapsed())) {
        if (onto->kind != LayerKind::Group)
            return false;
        to = onto;
    } else {
        to = onto->parent;
    }
    std::vector<std::unique_ptr<Command>> parts;
    // Bottom first: each goes right above the one before (the first, where
    // a single layer would go). Applied as made, so indices stay right.
    Layer* previous = nullptr;
    for (Layer* l : layers) {
        Layer& from = *l->parent;
        const int fromIndex = indexIn(from, l);
        int toIndex = previous ? indexIn(*to, previous) + 1
                    : to == onto ? int(onto->children.size())
                                 : indexIn(*to, onto) + (zone == 0 ? 1 : 0);
        if (&from == to && fromIndex < toIndex)
            --toIndex; // counted after the layer leaves
        auto c = std::make_unique<MoveLayerCommand>(from, fromIndex, *to, toIndex);
        c->redo(*m_doc);
        parts.push_back(std::move(c));
        previous = l;
    }
    to->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    pushCommand(std::make_unique<CommandGroup>("Move Layers", std::move(parts)));
    emit historyChanged();
    m_current = layers.back();
    setRange(NoRange, {});
    structureChanged();
    return true;
}

std::unique_ptr<Layer> Editor::layerLike(const Layer& layer, LayerKind kind) const
{
    auto out = std::make_unique<Layer>(kind, layer.name);
    out->visible = layer.visible;
    out->locked = layer.locked;
    out->opacity = layer.opacity;
    out->blend = layer.blend;
    out->extraFlags = layer.extraFlags & uint16_t(~Layer::kBackgroundFlag);
    out->uuid = layer.uuid;
    out->userData = layer.userData;
    out->cels.resize(size_t(m_doc->frameCount()));
    return out;
}

void Editor::replaceCurrentLayer(std::unique_ptr<Layer> layer, std::vector<Tileset> tilesets, const char* label)
{
    Layer& parent = *m_current->parent;
    const int index = indexIn(parent, m_current);
    Layer* added = layer.get();
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<SetTilesetsCommand>(m_doc->tilesets, std::move(tilesets), label));
    parts.push_back(std::make_unique<RemoveLayerCommand>(parent, index));
    parts.push_back(std::make_unique<InsertLayerCommand>(parent, index, std::move(layer)));
    execute(std::make_unique<CommandGroup>(label, std::move(parts)));
    m_current = added;
    structureChanged();
}

QVariantList Editor::tilesetList() const
{
    QVariantList out;
    for (const Tileset& set : m_doc->tilesets)
        out.push_back(QVariantMap{{"name", QString::fromStdString(set.name)},
                                  {"tileWidth", set.tileWidth},
                                  {"tileHeight", set.tileHeight},
                                  {"baseIndex", set.baseIndex},
                                  {"tiles", std::max(0, int(set.tiles.size()) - 1)},
                                  {"usable", !set.tiles.empty()}});
    return out;
}

uint32_t Editor::tilesetFor(const QVariantMap& options, std::vector<Tileset>& tilesets,
                            const std::string& defaultName) const
{
    const int existing = options.value("tileset", -1).toInt();
    if (existing >= 0 && existing < int(tilesets.size()) && !tilesets[size_t(existing)].tiles.empty())
        return uint32_t(existing);
    const QRect g = grid();
    Tileset set;
    const QString name = options.value("name").toString().trimmed();
    set.name = name.isEmpty() ? defaultName : name.toStdString();
    set.tileWidth = std::clamp(options.value("tileWidth", g.width()).toInt(), 1, 1024);
    set.tileHeight = std::clamp(options.value("tileHeight", g.height()).toInt(), 1, 1024);
    set.baseIndex = std::clamp(options.value("baseIndex", 1).toInt(), 0, 1 << 20);
    auto empty = std::make_shared<Image>(set.tileWidth, set.tileHeight, m_doc->pixelFormat());
    empty->clear(transparentValue());
    set.tiles.push_back(std::move(empty));
    tilesets.push_back(std::move(set));
    return uint32_t(tilesets.size() - 1);
}

bool Editor::addTilemapLayer(const QVariantMap& options)
{
    commitFloating();
    int count = 0;
    m_doc->forEachLayer([&](const Layer& l) { count += l.kind == LayerKind::Tilemap; });
    const std::string name = "Tilemap " + std::to_string(count + 1);
    std::vector<Tileset> tilesets = m_doc->tilesets;
    auto layer = std::make_unique<Layer>(LayerKind::Tilemap, name);
    layer->tilesetIndex = tilesetFor(options, tilesets, name);
    layer->cels.resize(size_t(m_doc->frameCount()));
    Layer* added = layer.get();
    Layer& parent = m_current && m_current->parent ? *m_current->parent : m_doc->root();
    const int index = m_current ? indexIn(parent, m_current) + 1 : int(parent.children.size());
    std::vector<std::unique_ptr<Command>> parts;
    if (tilesets.size() != m_doc->tilesets.size())
        parts.push_back(std::make_unique<SetTilesetsCommand>(m_doc->tilesets, std::move(tilesets), "New Tilemap Layer"));
    parts.push_back(std::make_unique<InsertLayerCommand>(parent, index, std::move(layer)));
    execute(std::make_unique<CommandGroup>("New Tilemap Layer", std::move(parts)));
    m_current = added;
    structureChanged();
    // A tileset that already has tiles is there to be placed: show it. An empty
    // one is filled by painting, so the pixel mode is the one to be in.
    if (const Tileset* set = currentTileset())
        setEditTiles(set->tiles.size() > 1);
    return true;
}

bool Editor::convertToTilemap(const QVariantMap& options)
{
    commitFloating();
    if (!m_current || m_current->kind != LayerKind::Image || m_current->isBackground())
        return false;
    const QRect g = grid();
    std::vector<Tileset> tilesets = m_doc->tilesets;
    const uint32_t setIndex = tilesetFor(options, tilesets, m_current->name);
    Tileset& set = tilesets[setIndex];
    auto layer = layerLike(*m_current, LayerKind::Tilemap);
    layer->tilesetIndex = setIndex;
    // Each cel once: linked cels stay linked. Tiles already in an existing
    // tileset are reused.
    std::unordered_map<const CelData*, CelRef> made;
    for (size_t f = 0; f < m_current->cels.size(); ++f) {
        const CelRef& cel = m_current->cels[f];
        if (!cel || !cel->image)
            continue;
        CelRef& map = made[cel.get()];
        if (!map)
            map = tilemapFromCel(*m_doc, *cel, set, {g.x(), g.y()});
        layer->cels[f] = map;
    }
    replaceCurrentLayer(std::move(layer), std::move(tilesets), "Convert to Tilemap");
    return true;
}

bool Editor::loadTilesetSheet(const QUrl& url)
{
    const QString path = toLocalPath(url);
    std::string error;
    auto image = formats::loadImage(toFsPath(path), &error);
    if (!image) {
        emit errorOccurred(tr("Couldn't import %1: %2.")
                               .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return false;
    }
    m_sheet = std::move(*image);
    m_sheetName = QFileInfo(path).completeBaseName();
    return true;
}

QImage Editor::tilesetSheet() const
{
    QImage out(m_sheet.width(), m_sheet.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < m_sheet.height(); ++y)
        for (int x = 0; x < m_sheet.width(); ++x) {
            const Rgba c = unpack(m_sheet.pixel(x, y));
            out.setPixelColor(x, y, QColor(c.r, c.g, c.b, c.a));
        }
    return out;
}

QSize Editor::tilesetSheetGrid(int tileWidth, int tileHeight, int margin, int spacing) const
{
    const SheetGrid g = sheetGrid(m_sheet.width(), m_sheet.height(), tileWidth, tileHeight, margin, spacing);
    return {g.columns, g.rows};
}

QVariantList Editor::tilesetSheetCells(int tileWidth, int tileHeight, int margin, int spacing) const
{
    QVariantList out;
    const SheetGrid g = sheetGrid(m_sheet.width(), m_sheet.height(), tileWidth, tileHeight, margin, spacing);
    for (int row = 0; row < g.rows; ++row)
        for (int col = 0; col < g.columns; ++col)
            out.append(QRectF(margin + col * (tileWidth + spacing), margin + row * (tileHeight + spacing),
                              tileWidth, tileHeight));
    return out;
}

QSize Editor::detectTilesetSize() const
{
    const TileSizeGuess guess = detectTileSize(m_sheet);
    return guess.found() ? QSize(guess.width, guess.height) : QSize();
}

bool Editor::importTileset(const QVariantMap& options)
{
    if (m_sheet.width() == 0)
        return false;
    commitFloating();
    int tw = std::clamp(options.value("tileWidth", 16).toInt(), 1, 1024);
    int th = std::clamp(options.value("tileHeight", 16).toInt(), 1, 1024);
    const bool skipEmpty = options.value("skipEmpty").toBool();
    const bool merge = options.value("merge").toBool();
    const bool layout = options.value("layout").toBool(); // the tiles on the new layer as the image shows them
    std::vector<Tileset> tilesets = m_doc->tilesets;
    // Into a tileset the sprite has: its tile size is the one the sheet is cut with.
    const int existing = options.value("tileset", -1).toInt();
    const bool append = existing >= 0 && existing < int(tilesets.size()) && !tilesets[size_t(existing)].tiles.empty();
    if (append) {
        tw = tilesets[size_t(existing)].tileWidth;
        th = tilesets[size_t(existing)].tileHeight;
    }
    const int margin = options.value("margin").toInt(), spacing = options.value("spacing").toInt();
    const std::vector<Image> slices = sliceSheet(m_sheet, tw, th, margin, spacing);
    const SheetGrid grid = sheetGrid(m_sheet.width(), m_sheet.height(), tw, th, margin, spacing);
    if (slices.empty()) {
        emit errorOccurred(tr("The tile size doesn't fit the image."));
        return false;
    }
    QVariantMap make;
    make["name"] = options.value("name", m_sheetName);
    make["tileWidth"] = tw;
    make["tileHeight"] = th;
    make["baseIndex"] = options.value("baseIndex", 1);
    make["tileset"] = append ? existing : -1;
    const uint32_t id = tilesetFor(make, tilesets, m_sheetName.toStdString());
    std::vector<Image> tiles;
    tiles.reserve(slices.size());
    for (const Image& slice : slices)
        tiles.push_back(convertToDocument(*m_doc, slice));
    const SheetImport imported = importSheetTiles(tilesets[id], tiles, grid.columns, grid.rows, transparentValue(),
                                                  skipEmpty, merge);
    for (const auto& tile : imported.added)
        tilesets[id].tiles.push_back(tile);
    const int firstNew = int(tilesets[id].tiles.size() - imported.added.size());
    auto layer = std::make_unique<Layer>(LayerKind::Tilemap, m_sheetName.toStdString());
    layer->tilesetIndex = id;
    layer->cels.resize(size_t(m_doc->frameCount()));
    if (layout) {
        auto cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(imported.map);
        cel->position = {0, 0};
        layer->cels[size_t(m_currentFrame)] = cel;
    }
    Layer* added = layer.get();
    Layer& parent = m_current && m_current->parent ? *m_current->parent : m_doc->root();
    const int index = m_current ? indexIn(parent, m_current) + 1 : int(parent.children.size());
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<SetTilesetsCommand>(m_doc->tilesets, std::move(tilesets), "Import Tileset"));
    parts.push_back(std::make_unique<InsertLayerCommand>(parent, index, std::move(layer)));
    execute(std::make_unique<CommandGroup>("Import Tileset", std::move(parts)));
    m_current = added;
    structureChanged();
    setEditTiles(true);
    setCurrentTile(imported.added.empty() ? 1 : firstNew);
    return true;
}

bool Editor::exportTileset(const QUrl& url, const QVariantMap& map)
{
    const Tileset* set = currentTileset();
    if (!set)
        return false;
    endStroke();
    commitFloating();
    const QString path = toLocalPath(url);
    formats::TilesetExportOptions options;
    options.columns = map.value("columns", 8).toInt();
    options.margin = map.value("margin").toInt();
    options.spacing = map.value("spacing").toInt();
    options.tsx = map.value("tsx", true).toBool();
    options.tmx = map.value("tmx").toBool();
    options.tsj = map.value("tsj").toBool();
    options.tmj = map.value("tmj").toBool();
    options.tres = map.value("tres").toBool();
    options.godotFolder = map.value("godotFolder").toString().toStdString();
    options.csv = map.value("csv").toBool();
    std::string error;
    const bool ok = formats::exportTileset(*m_doc, m_current->tilesetIndex, m_currentFrame, toFsPath(path), options, &error);
    if (!ok)
        emit errorOccurred(tr("Couldn't export %1: %2.")
                               .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
    return ok;
}

QVariantMap Editor::tilesetExportInfo(int columns, int margin, int spacing) const
{
    QVariantMap out;
    const Tileset* set = currentTileset();
    if (!set)
        return out;
    const auto s = formats::tilesetSheetLayout(int(set->tiles.size()) - 1, set->tileWidth, set->tileHeight, columns, margin, spacing);
    out["width"] = s.width;
    out["height"] = s.height;
    out["columns"] = s.columns;
    out["rows"] = s.rows;
    out["tiles"] = s.tiles;
    int layers = 0;
    m_doc->forEachLayer([&](const Layer& l) {
        layers += l.kind == LayerKind::Tilemap && l.tilesetIndex == m_current->tilesetIndex
                  && m_currentFrame < int(l.cels.size()) && l.cels[size_t(m_currentFrame)];
    });
    out["layers"] = layers;
    // What a map would hold: every tilemap layer on this frame whose tileset has this tile size
    // (those of other sizes are left out), and how many tilesets that is.
    int mapLayers = 0, skipped = 0;
    std::vector<size_t> used;
    m_doc->forEachLayer([&](const Layer& l) {
        if (l.kind != LayerKind::Tilemap || l.tilesetIndex >= m_doc->tilesets.size() || m_currentFrame >= int(l.cels.size())
            || !l.cels[size_t(m_currentFrame)])
            return;
        const Tileset& t = m_doc->tilesets[l.tilesetIndex];
        if (t.tiles.size() < 2)
            return;
        if (t.tileWidth != set->tileWidth || t.tileHeight != set->tileHeight) {
            ++skipped;
            return;
        }
        ++mapLayers;
        if (std::find(used.begin(), used.end(), l.tilesetIndex) == used.end())
            used.push_back(l.tilesetIndex);
    });
    out["mapLayers"] = mapLayers;
    out["mapTilesets"] = int(used.size());
    out["skippedLayers"] = skipped;
    return out;
}

QImage Editor::tilesetExportImage(int columns, int margin, int spacing) const
{
    const Tileset* set = currentTileset();
    if (!set || set->tiles.size() < 2)
        return {};
    const Image sheet = formats::tilesetSheetImage(*m_doc, *set, columns, margin, spacing);
    QImage out(sheet.width(), sheet.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < sheet.height(); ++y)
        for (int x = 0; x < sheet.width(); ++x) {
            const Rgba c = unpack(sheet.pixel(x, y));
            out.setPixelColor(x, y, QColor(c.r, c.g, c.b, c.a));
        }
    return out;
}

bool Editor::convertToImageLayer()
{
    commitFloating();
    const Tileset* set = currentTileset();
    if (!set)
        return false;
    auto layer = layerLike(*m_current, LayerKind::Image);
    std::unordered_map<const CelData*, CelRef> made;
    for (size_t f = 0; f < m_current->cels.size(); ++f) {
        const CelRef& cel = m_current->cels[f];
        if (!cel || !cel->image)
            continue;
        CelRef& image = made[cel.get()];
        if (!image)
            image = celFromTilemap(*m_doc, *cel, *set);
        layer->cels[f] = image;
    }
    // The tileset stays (other layers may use it; Aseprite keeps it too).
    replaceCurrentLayer(std::move(layer), m_doc->tilesets, "Convert to Normal Layer");
    return true;
}

bool Editor::groupLayers()
{
    commitFloating();
    const std::vector<Layer*> layers = selectedLayers();
    if (layers.empty())
        return false;
    // The group goes right above the topmost selected layer, in its parent.
    Layer& parent = *layers.back()->parent;
    int groups = 0;
    m_doc->forEachLayer([&](const Layer& l) { groups += l.kind == LayerKind::Group; });
    auto made = m_doc->makeGroupLayer("Group " + std::to_string(groups + 1));
    Layer* group = made.get();
    std::vector<std::unique_ptr<Command>> parts;
    // Each step is applied as it's made, so the next one's indices are right.
    const auto apply = [&](std::unique_ptr<Command> c) {
        c->redo(*m_doc);
        parts.push_back(std::move(c));
    };
    apply(std::make_unique<InsertLayerCommand>(parent, indexIn(parent, layers.back()) + 1, std::move(made)));
    for (Layer* l : layers) // bottom first, each on top of the last
        apply(std::make_unique<MoveLayerCommand>(*l->parent, indexIn(*l->parent, l), *group,
                                                 int(group->children.size())));
    pushCommand(std::make_unique<CommandGroup>("Group Layers", std::move(parts)));
    emit historyChanged();
    m_current = group;
    setRange(NoRange, {});
    structureChanged();
    return true;
}

bool Editor::smearLayers()
{
    commitFloating();
    const std::vector<Layer*> layers = selectedLayers();
    if (layers.empty())
        return false;
    // The pivot starts in the middle of what the layers show, over every frame.
    Rect art;
    for (const Layer* layer : layers) {
        std::function<void(const Layer&)> add = [&](const Layer& l) {
            for (const auto& child : l.children)
                add(*child);
            if (l.kind != LayerKind::Image)
                return;
            for (const CelRef& cel : l.cels) {
                if (!cel || !cel->image)
                    continue;
                const Image& image = *cel->image;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        if (core::toRgba(*m_doc, image, x, y, l.isBackground()).a)
                            art = art.united({cel->position.x + x, cel->position.y + y, 1, 1});
            }
        };
        add(*layer);
    }
    if (art.isEmpty())
        art = m_doc->bounds();
    Layer& parent = *layers.back()->parent;
    int smears = 0;
    m_doc->forEachLayer([&](const Layer& l) { smears += l.isSmear(); });
    auto made = m_doc->makeGroupLayer("Smear " + std::to_string(smears + 1));
    Layer* group = made.get();
    core::SmearRecipe recipe;
    recipe.pivotX = art.x + art.w / 2.0;
    recipe.pivotY = art.y + art.h / 2.0;
    group->smear = recipe;
    std::vector<std::unique_ptr<Command>> parts;
    const auto apply = [&](std::unique_ptr<Command> c) {
        c->redo(*m_doc);
        parts.push_back(std::move(c));
    };
    apply(std::make_unique<InsertLayerCommand>(parent, indexIn(parent, layers.back()) + 1, std::move(made)));
    for (Layer* l : layers)
        apply(std::make_unique<MoveLayerCommand>(*l->parent, indexIn(*l->parent, l), *group,
                                                 int(group->children.size())));
    pushCommand(std::make_unique<CommandGroup>("Smear", std::move(parts)));
    emit historyChanged();
    m_current = group;
    setRange(NoRange, {});
    structureChanged();
    return true;
}

Layer* Editor::smearGroup() const
{
    for (Layer* l = m_current; l; l = l->parent)
        if (l->isSmear())
            return l;
    return nullptr;
}

std::optional<core::SmearRecipe> Editor::smearRecipe() const
{
    const Layer* group = smearGroup();
    return group ? group->smear : std::nullopt;
}

void Editor::setSmearRecipe(const core::SmearRecipe& recipe, const char* label, bool continuous)
{
    Layer* group = smearGroup();
    if (!group || recipe == *group->smear)
        return;
    const QString key = QStringLiteral("smear:%1:%2").arg(quintptr(group)).arg(QLatin1String(label));
    const bool merge = continuous ? continuing(key) : (m_mergeKey.clear(), false);
    execute(std::make_unique<SetSmearCommand>(*group, *group->smear, recipe, label), merge);
    invalidateAll();
}

void Editor::refreshSmearState()
{
    QVariantMap state;
    Layer* group = m_current && m_current->isSmear() ? m_current : nullptr;
    state["current"] = group != nullptr;
    if (group) {
        const core::SmearRecipe& r = *group->smear;
        const core::SmearPose pose = core::smearPoseAt(r, m_currentFrame);
        QVariantList keys;
        bool here = false;
        for (const core::SmearKey& k : r.keys) {
            keys.push_back(k.frame);
            here = here || k.frame == m_currentFrame;
        }
        Image art(m_doc->width(), m_doc->height());
        core::renderSmearArt(*m_doc, *group, m_currentFrame, art);
        Rect box = opaqueBounds(art);
        if (box.isEmpty()) // nothing drawn: a small box around the pivot, so there is something to grab
            box = {int(std::floor(r.pivotX)) - 4, int(std::floor(r.pivotY)) - 4, 8, 8};
        const auto mapped = [&](double x, double y) {
            double qx, qy;
            core::smearMapPoint(r, pose, x, y, qx, qy);
            return QPointF(qx, qy);
        };
        const QPointF origin = mapped(r.pivotX, r.pivotY);
        const QPointF ex = mapped(r.pivotX + 1, r.pivotY) - origin, ey = mapped(r.pivotX, r.pivotY + 1) - origin;
        state["frame"] = m_currentFrame;
        state["key"] = here;
        state["keys"] = keys;
        state["pose"] = QVariantMap{{"x", pose.x}, {"y", pose.y}, {"rotation", pose.rotation}, {"scaleX", pose.scaleX},
                                    {"scaleY", pose.scaleY}, {"skewX", pose.skewX}, {"skewY", pose.skewY}};
        state["pivot"] = QPointF(r.pivotX, r.pivotY);
        state["pivotPosed"] = origin;
        state["box"] = QRectF(box.x, box.y, box.w, box.h);
        state["corners"] = QVariantList{mapped(box.x, box.y), mapped(box.x + box.w, box.y),
                                        mapped(box.x + box.w, box.y + box.h), mapped(box.x, box.y + box.h)};
        state["matrix"] = QVariantList{ex.x(), ey.x(), ex.y(), ey.y()};
        QVariantList keyList; // with their easing, for the panel
        for (const core::SmearKey& k : r.keys)
            keyList.push_back(QVariantMap{{"frame", k.frame}, {"ease", int(k.ease)},
                                          {"curve", QVariantList{k.curve.x1, k.curve.y1, k.curve.x2, k.curve.y2}}});
        state["keyList"] = keyList;
        state["trail"] = r.trail;
        state["behind"] = r.behind ? 1 : 0;
        state["style"] = int(r.style);
        state["color"] = toQColor(r.color);
        state["taper"] = r.taper;
        state["fade"] = r.fade;
        state["fadePattern"] = r.fadePattern;
        state["outline"] = r.outline ? 1 : 0;
        state["outlineColor"] = toQColor(r.outlineColor);
        state["smoothPath"] = r.smoothPath ? 1 : 0;
        state["facePath"] = r.facePath ? 1 : 0;
        // The way the pivot goes, from the first key to the last: a line (8 points a frame) and a dot a
        // frame.
        QVariantList path, dots;
        if (r.keys.size() > 1) {
            const int first = r.keys.front().frame, last = r.keys.back().frame;
            for (int i = first * 8; i <= last * 8; ++i) {
                const core::SmearPose p = core::smearPoseAt(r, i / 8.0);
                const QPointF at(r.pivotX + p.x, r.pivotY + p.y);
                path.push_back(at);
                if (i % 8 == 0)
                    dots.push_back(at);
            }
        }
        state["path"] = path;
        state["pathDots"] = dots;
    }
    if (state != m_smearState) {
        m_smearState = state;
        emit smearChanged();
    }
}

void Editor::setSmearPose(const QVariantMap& pose, bool continuous)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    core::SmearPose p = core::smearPoseAt(r, m_currentFrame);
    const auto number = [&](const char* key, double& target, double lo, double hi) {
        if (pose.contains(key))
            target = std::clamp(pose.value(key).toDouble(), lo, hi);
    };
    number("x", p.x, -8192, 8192);
    number("y", p.y, -8192, 8192);
    number("rotation", p.rotation, -36000, 36000);
    number("scaleX", p.scaleX, -100, 100);
    number("scaleY", p.scaleY, -100, 100);
    number("skewX", p.skewX, -85, 85);
    number("skewY", p.skewY, -85, 85);
    // Never quite flat: a scale of 0 would make it vanish and leave nothing to grab.
    for (double* s : {&p.scaleX, &p.scaleY})
        if (std::abs(*s) < 0.05)
            *s = *s < 0 ? -0.05 : 0.05;
    const auto at = std::find_if(r.keys.begin(), r.keys.end(), [&](const core::SmearKey& k) { return k.frame >= m_currentFrame; });
    if (at != r.keys.end() && at->frame == m_currentFrame) {
        at->pose = p;
    } else {
        core::SmearKey k;
        k.frame = m_currentFrame;
        k.pose = p;
        // A new key between two eases on like the one before it.
        if (at != r.keys.begin())
            k.ease = std::prev(at)->ease;
        r.keys.insert(at, k);
    }
    setSmearRecipe(r, "Pose", continuous);
}

void Editor::setSmearPivot(double x, double y, bool continuous)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    const double dx = std::clamp(x, -8192.0, 8192.0) - r.pivotX, dy = std::clamp(y, -8192.0, 8192.0) - r.pivotY;
    // The art stays where it is posed on every key: each key's move takes (M - I) d, M its turn, skew and
    // scale (the art at p shows at pivot + move + M (p - pivot)).
    const core::SmearRecipe was = r;
    for (core::SmearKey& k : r.keys) {
        // As it shows there (facing the path, too).
        const core::SmearPose shown = core::smearPoseAt(was, k.frame);
        double ox, oy, qx, qy;
        core::smearMapPoint(was, shown, was.pivotX, was.pivotY, ox, oy);
        core::smearMapPoint(was, shown, was.pivotX + dx, was.pivotY + dy, qx, qy);
        k.pose.x += (qx - ox) - dx;
        k.pose.y += (qy - oy) - dy;
    }
    r.pivotX += dx;
    r.pivotY += dy;
    setSmearRecipe(r, "Pivot", continuous);
}

void Editor::removeSmearKey(int frame)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    if (std::erase_if(r.keys, [&](const core::SmearKey& k) { return k.frame == frame; }) == 0)
        return;
    setSmearRecipe(r, "Remove Key", false);
}

void Editor::setSmearKeyEase(int frame, int ease)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    for (core::SmearKey& k : r.keys)
        if (k.frame == frame) {
            const auto next = core::SmearKey::Ease(std::clamp(ease, 0, 4));
            if (next == core::SmearKey::Ease::Custom && k.ease != next) // starts from the curve it had
                k.curve = core::namedEasing(int(k.ease));
            k.ease = next;
        }
    setSmearRecipe(r, "Key Easing", false);
}

void Editor::setSmearKeyCurve(int frame, double x1, double y1, double x2, double y2, bool continuous)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    for (core::SmearKey& k : r.keys)
        if (k.frame == frame) {
            k.ease = core::SmearKey::Ease::Custom;
            k.curve = {std::clamp(x1, 0.0, 1.0), std::clamp(y1, -1.0, 2.0), std::clamp(x2, 0.0, 1.0), std::clamp(y2, -1.0, 2.0)};
        }
    setSmearRecipe(r, "Key Curve", continuous);
}

void Editor::setParticleCurve(const QString& key, double x1, double y1, double x2, double y2, bool continuous)
{
    if (!liveLayer())
        return;
    ParticleRecipe root = *m_current->particles;
    ParticleRecipe& r = particleContext() == 1 && !root.children.empty() ? root.children.front() : root;
    const core::EasingCurve curve{std::clamp(x1, 0.0, 1.0), std::clamp(y1, -1.0, 2.0), std::clamp(x2, 0.0, 1.0),
                                  std::clamp(y2, -1.0, 2.0)};
    if (key == "sizeCurve") {
        r.sizeEase = ParticleRecipe::Ease::Custom;
        r.sizeCurve = curve;
    } else if (key == "opacityCurve") {
        r.opacityEase = ParticleRecipe::Ease::Custom;
        r.opacityCurve = curve;
    } else {
        return;
    }
    setParticleRecipe(root, "Particle Curve", continuous);
}

void Editor::resetSmearSettings(const QStringList& keys)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    const core::SmearRecipe built;
    const core::SmearPose plain;
    bool pose = false;
    core::SmearPose p = core::smearPoseAt(r, m_currentFrame);
    for (const QString& key : keys) {
        if (key == "trail") r.trail = built.trail;
        else if (key == "behind") r.behind = built.behind;
        else if (key == "style") r.style = built.style;
        else if (key == "taper") r.taper = built.taper;
        else if (key == "fade") r.fade = built.fade;
        else if (key == "fadePattern") r.fadePattern = built.fadePattern;
        else if (key == "outline") r.outline = built.outline;
        else if (key == "smoothPath") r.smoothPath = built.smoothPath;
        else if (key == "facePath") r.facePath = built.facePath;
        else if (key == "x") { p.x = plain.x; pose = true; }
        else if (key == "y") { p.y = plain.y; pose = true; }
        else if (key == "rotation") { p.rotation = plain.rotation; pose = true; }
        else if (key == "scaleX") { p.scaleX = plain.scaleX; pose = true; }
        else if (key == "scaleY") { p.scaleY = plain.scaleY; pose = true; }
        else if (key == "skewX") { p.skewX = plain.skewX; pose = true; }
        else if (key == "skewY") { p.skewY = plain.skewY; pose = true; }
    }
    if (pose) {
        const auto at = std::find_if(r.keys.begin(), r.keys.end(), [&](const core::SmearKey& k) { return k.frame >= m_currentFrame; });
        if (at != r.keys.end() && at->frame == m_currentFrame) {
            at->pose = p;
        } else {
            core::SmearKey k;
            k.frame = m_currentFrame;
            k.pose = p;
            if (at != r.keys.begin())
                k.ease = std::prev(at)->ease;
            r.keys.insert(at, k);
        }
    }
    setSmearRecipe(r, "Reset", false);
}

void Editor::setSmearSetting(const QString& key, const QVariant& value, bool continuous)
{
    if (!m_current || !m_current->isSmear())
        return;
    core::SmearRecipe r = *m_current->smear;
    const double v = value.toDouble();
    if (key == "trail")
        r.trail = std::clamp(v, 0.0, 1.0);
    else if (key == "behind")
        r.behind = v != 0;
    else if (key == "style")
        r.style = core::SmearRecipe::Style(std::clamp(int(v), 0, 1));
    else if (key == "color")
        r.color = toRgba(value.value<QColor>());
    else if (key == "taper")
        r.taper = std::clamp(v, 0.0, 1.0);
    else if (key == "fade")
        r.fade = std::clamp(v, 0.0, 1.0);
    else if (key == "fadePattern")
        r.fadePattern = std::clamp(int(v), 0, 4);
    else if (key == "outline")
        r.outline = v != 0;
    else if (key == "outlineColor")
        r.outlineColor = toRgba(value.value<QColor>());
    else if (key == "smoothPath")
        r.smoothPath = v != 0;
    else if (key == "facePath")
        r.facePath = v != 0;
    else
        return;
    setSmearRecipe(r, "Smear", continuous);
}

void Editor::moveSmearKey(int from, int to, bool copy)
{
    if (!m_current || !m_current->isSmear() || from == to || to < 0 || to >= m_doc->frameCount())
        return;
    core::SmearRecipe r = *m_current->smear;
    const auto at = std::find_if(r.keys.begin(), r.keys.end(), [&](const core::SmearKey& k) { return k.frame == from; });
    if (at == r.keys.end())
        return;
    core::SmearKey moved = *at;
    moved.frame = to;
    if (!copy)
        r.keys.erase(at);
    std::erase_if(r.keys, [&](const core::SmearKey& k) { return k.frame == to; }); // replaced
    r.keys.insert(std::find_if(r.keys.begin(), r.keys.end(), [&](const core::SmearKey& k) { return k.frame > to; }), moved);
    setSmearRecipe(r, copy ? "Copy Key" : "Move Key", false);
    setCurrentFrame(to);
}

void Editor::copySmearPose(int frame)
{
    if (!m_current || !m_current->isSmear())
        return;
    m_smearPoseClipboard = core::smearPoseAt(*m_current->smear, frame);
    emit smearChanged();
}

void Editor::pasteSmearPose(int frame)
{
    if (!m_smearPoseClipboard || !m_current || !m_current->isSmear())
        return;
    const core::SmearPose& p = *m_smearPoseClipboard;
    setCurrentFrame(frame);
    setSmearPose({{"x", p.x}, {"y", p.y}, {"rotation", p.rotation}, {"scaleX", p.scaleX}, {"scaleY", p.scaleY},
                  {"skewX", p.skewX}, {"skewY", p.skewY}},
                 false);
}

// --- Smear presets: the built-in ones (core::smearPresets) and the ones saved as files ---------------

// A saved Smear preset: {"name", "data": the recipe made independent of the art (core::relativeSmear) as
// a group's user data property, base64}.
void Editor::loadUserSmearPresets() const
{
    if (m_userSmearPresetsLoaded)
        return;
    m_userSmearPresetsLoaded = true;
    m_userSmearPresets.clear();
    const QDir dir(presetFolder());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.smearpreset")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        const QByteArray bytes = QByteArray::fromBase64(json.value("data").toString().toLatin1());
        const auto recipe = formats::storedSmearIn(core::Bytes(bytes.begin(), bytes.end()));
        const QString name = json.value("name").toString();
        if (!recipe || recipe->keys.empty() || name.isEmpty())
            continue;
        m_userSmearPresets.push_back({"user:" + info.completeBaseName(), name, *recipe});
    }
}

QVariantList Editor::smearPresetList() const
{
    QVariantList list;
    for (const core::SmearPreset& p : core::smearPresets())
        list.push_back(QVariantMap{{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)},
                                   {"description", QString::fromStdString(p.description)}, {"user", false}});
    loadUserSmearPresets();
    for (const UserSmearPreset& p : m_userSmearPresets)
        list.push_back(QVariantMap{{"id", p.id}, {"name", p.name}, {"description", QString()}, {"user", true}});
    return list;
}

// The current Smear group's art as drawn: on this frame, else the first frame that has any (art drawn
// once and posed on later frames).
Image Editor::smearArt() const
{
    Image art(m_doc->width(), m_doc->height());
    if (!m_current || !m_current->isSmear())
        return art;
    core::renderSmearArt(*m_doc, *m_current, m_currentFrame, art);
    for (int f = 0; f < m_doc->frameCount() && opaqueBounds(art).isEmpty(); ++f)
        core::renderSmearArt(*m_doc, *m_current, f, art);
    return art;
}

// Its box (the whole canvas when it shows nothing).
Rect Editor::smearArtBox() const
{
    const Rect box = opaqueBounds(smearArt());
    return box.isEmpty() ? m_doc->bounds() : box;
}

std::optional<core::SmearRecipe> Editor::smearPresetFor(const QString& id, Rect art, int start) const
{
    if (!m_current || !m_current->isSmear())
        return std::nullopt;
    if (const core::SmearPreset* preset = core::findSmearPreset(id.toStdString()))
        return core::smearPresetRecipe(preset->id, art, start, *m_current->smear);
    loadUserSmearPresets();
    for (const UserSmearPreset& p : m_userSmearPresets)
        if (p.id == id)
            return core::placedSmear(p.relative, art, start, *m_current->smear);
    return std::nullopt;
}

void Editor::applySmearPreset(const QString& id)
{
    const auto recipe = smearPresetFor(id, smearArtBox(), m_currentFrame);
    if (!recipe || recipe->keys.empty())
        return;
    endStroke();
    Layer* group = m_current;
    std::vector<std::unique_ptr<Command>> parts;
    const auto apply = [&](std::unique_ptr<Command> c) {
        c->redo(*m_doc);
        parts.push_back(std::move(c));
    };
    // The frames it needs, after the last one.
    const int need = recipe->keys.back().frame + 1;
    for (int i = m_doc->frameCount(); i < need && i < 10000; ++i)
        apply(std::make_unique<InsertFrameCommand>(i, m_doc->frame(m_doc->frameCount() - 1)));
    apply(std::make_unique<SetSmearCommand>(*group, *group->smear, *recipe, "Smear Preset"));
    QString name = id;
    for (const QVariant& p : smearPresetList())
        if (p.toMap()["id"].toString() == id)
            name = p.toMap()["name"].toString();
    pushCommand(std::make_unique<CommandGroup>("Smear Preset: " + name.toStdString(), std::move(parts)));
    emit historyChanged();
    structureChanged();
    invalidateAll();
}

QImage Editor::smearPresetFrame(const QString& id, int frame) const
{
    const auto it = m_smearPreviews.find(id);
    if (it == m_smearPreviews.end() || it->second.empty())
        return {};
    return it->second[size_t(std::clamp(frame, 0, int(it->second.size()) - 1))];
}

QVariantMap Editor::smearPresetPreview(const QString& id) const
{
    m_smearPreviews.erase(id);
    const Rect box = smearArtBox();
    const auto recipe = smearPresetFor(id, box, 0);
    if (!recipe || recipe->keys.empty())
        return {};
    const Image art = smearArt();
    // Every frame of it on the art as drawn here, cropped to where anything shows (and room around it).
    std::vector<Image> frames;
    Rect shown;
    for (int f = 0; f <= recipe->keys.back().frame; ++f) {
        frames.push_back(core::renderSmear(*recipe, art, f));
        shown = shown.united(opaqueBounds(frames.back()));
    }
    if (shown.isEmpty())
        return {};
    shown = Rect{shown.x - 1, shown.y - 1, shown.w + 2, shown.h + 2}.intersected(m_doc->bounds());
    std::vector<QImage> out;
    int still = 0, most = -1;
    for (size_t f = 0; f < frames.size(); ++f) {
        const Image part = frames[f].copy(shown);
        int covered = 0;
        for (int y = 0; y < part.height(); ++y)
            for (int x = 0; x < part.width(); ++x)
                covered += (part.pixel(x, y) >> 24) != 0;
        if (covered > most) {
            most = covered;
            still = int(f);
        }
        out.push_back(QImage(part.data(), part.width(), part.height(), part.stride(), QImage::Format_RGBA8888).copy());
    }
    const int count = int(out.size());
    m_smearPreviews[id] = std::move(out);
    return {{"count", count}, {"still", still}};
}

bool Editor::saveSmearPreset(const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (!m_current || !m_current->isSmear() || m_current->smear->keys.empty() || name.isEmpty())
        return false;
    loadUserSmearPresets();
    const core::SmearRecipe relative = core::relativeSmear(*m_current->smear, smearArtBox());
    // The same name replaces the old one; else a file name from it that isn't taken.
    QString stem;
    for (const UserSmearPreset& p : m_userSmearPresets)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            stem = p.id.mid(5);
    if (stem.isEmpty()) {
        QString base;
        for (const QChar c : name.toLower())
            base += c.isLetterOrNumber() ? c : QLatin1Char('-');
        base = base.left(32);
        stem = base.isEmpty() ? QStringLiteral("smear") : base;
        const QDir dir(presetFolder());
        for (int n = 2; dir.exists(stem + ".smearpreset"); ++n)
            stem = base + "-" + QString::number(n);
    }
    const core::Bytes bytes = formats::withStoredSmear({}, relative);
    QJsonObject json;
    json["name"] = name;
    json["data"] = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toBase64());
    QDir().mkpath(presetFolder());
    QFile file(presetFolder() + "/" + stem + ".smearpreset");
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(json).toJson());
    m_userSmearPresetsLoaded = false;
    emit presetsChanged();
    return true;
}

void Editor::deleteSmearPreset(const QString& id)
{
    if (!id.startsWith("user:"))
        return;
    QFile::remove(presetFolder() + "/" + id.mid(5) + ".smearpreset");
    m_userSmearPresetsLoaded = false;
    emit presetsChanged();
}

bool Editor::renameSmearPreset(const QString& id, const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (!id.startsWith("user:") || name.isEmpty())
        return false;
    QFile file(presetFolder() + "/" + id.mid(5) + ".smearpreset");
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    json["name"] = name;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(json).toJson());
    m_userSmearPresetsLoaded = false;
    emit presetsChanged();
    return true;
}

bool Editor::syncSmearAsDrawn()
{
    bool changed = false;
    m_doc->forEachLayer([&](Layer& layer) {
        if (!layer.isSmear())
            return;
        bool inside = false;
        for (const Layer* l = m_current; l && !inside; l = l->parent)
            inside = l->parent == &layer;
        if (layer.smearAsDrawn != inside) {
            layer.smearAsDrawn = inside;
            changed = true;
        }
    });
    return changed;
}

bool Editor::anyPosedSmear() const
{
    bool posed = false;
    m_doc->forEachLayer([&](const Layer& layer) { posed = posed || (layer.isSmear() && !layer.smearAsDrawn && layer.visible); });
    return posed;
}

bool Editor::ungroupLayer()
{
    commitFloating();
    Layer* group = m_current;
    if (!group || group->kind != LayerKind::Group || !group->parent)
        return false;
    Layer& parent = *group->parent;
    Layer* top = group->children.empty() ? nullptr : group->children.back().get();
    std::vector<std::unique_ptr<Command>> parts;
    const auto apply = [&](std::unique_ptr<Command> c) {
        c->redo(*m_doc);
        parts.push_back(std::move(c));
    };
    // Children go just below the group, bottom first, keeping their order.
    while (!group->children.empty())
        apply(std::make_unique<MoveLayerCommand>(*group, 0, parent, indexIn(parent, group)));
    apply(std::make_unique<RemoveLayerCommand>(parent, indexIn(parent, group)));
    pushCommand(std::make_unique<CommandGroup>("Ungroup", std::move(parts)));
    emit historyChanged();
    m_current = top ? top : (parent.children.empty() ? nullptr : parent.children.back().get());
    setRange(NoRange, {});
    structureChanged();
    return true;
}

bool Editor::moveLayer(int index, int target, int zone)
{
    Layer* layer = layerAt(index);
    Layer* onto = layerAt(target);
    if (!layer || !onto || layer == onto)
        return false;
    commitFloating();
    // Dragging one of several selected layers moves them all.
    std::vector<Layer*> moving = selectedLayers();
    if (moving.size() < 2 || std::find(moving.begin(), moving.end(), layer) == moving.end())
        moving = {layer};
    if (moving.size() > 1)
        return moveLayers(moving, onto, zone);

    Layer* to;
    int toIndex;
    // Just under an open group's row is the top of that group.
    if (zone == 2 || (zone == 1 && onto->kind == LayerKind::Group && !onto->isCollapsed())) {
        if (onto->kind != LayerKind::Group)
            return false;
        to = onto;
        toIndex = int(onto->children.size());
    } else {
        to = onto->parent;
        toIndex = indexIn(*to, onto) + (zone == 0 ? 1 : 0);
    }
    if (isWithin(*to, *layer))
        return false; // a group into itself
    Layer& from = *layer->parent;
    const int fromIndex = indexIn(from, layer);
    if (&from == to && fromIndex < toIndex)
        --toIndex; // counted after the layer leaves
    if (&from == to && fromIndex == toIndex)
        return false;
    // Opening the target group so the moved layer stays visible.
    to->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    execute(std::make_unique<MoveLayerCommand>(from, fromIndex, *to, toIndex));
    m_current = layer;
    setRange(NoRange, {});
    structureChanged();
    return true;
}

// Panel state only; stored in the file but doesn't count as an edit.
void Editor::setLayerCollapsed(int index, bool collapsed)
{
    Layer* layer = layerAt(index);
    if (!layer || layer->kind != LayerKind::Group || layer->isCollapsed() == collapsed)
        return;
    if (collapsed)
        layer->extraFlags |= Layer::kCollapsedFlag;
    else
        layer->extraFlags &= uint16_t(~Layer::kCollapsedFlag);
    rebuildLayerList();
    emit layersChanged();
    emit currentLayerChanged();
}

void Editor::addFramesUpTo(int total)
{
    const int have = m_doc->frameCount();
    if (total <= have || total > 10000)
        return;
    endStroke();
    std::vector<std::unique_ptr<Command>> parts;
    for (int i = have; i < total; ++i) {
        auto insert = std::make_unique<InsertFrameCommand>(i, m_doc->frame(have - 1));
        insert->redo(*m_doc);
        parts.push_back(std::move(insert));
    }
    pushCommand(std::make_unique<CommandGroup>(total - have > 1 ? "Add Frames" : "Add Frame", std::move(parts)));
    emit historyChanged();
    structureChanged();
}

void Editor::addFrame()
{
    const int index = m_currentFrame + 1;
    execute(std::make_unique<InsertFrameCommand>(index, m_doc->frame(m_currentFrame)));
    m_currentFrame = index;
    structureChanged();
}

// Copies go right after the frames; links between the copied frames stay.
void Editor::duplicateFrame()
{
    const auto [first, last] = frameSpan();
    const int count = last - first + 1;
    std::unordered_map<const CelData*, CelRef> clones;
    std::vector<std::unique_ptr<Command>> parts;
    for (int i = 0; i < count; ++i) {
        std::vector<CelRef> cels = m_doc->celsAt(first + i);
        for (auto& cel : cels) {
            if (!cel)
                continue;
            auto& clone = clones[cel.get()];
            if (!clone)
                clone = cloneCel(cel);
            cel = clone;
        }
        auto insert = std::make_unique<InsertFrameCommand>(last + 1 + i, m_doc->frame(first + i), std::move(cels));
        insert->redo(*m_doc);
        parts.push_back(std::move(insert));
    }
    pushCommand(std::make_unique<CommandGroup>(count > 1 ? "Duplicate Frames" : "Duplicate Frame", std::move(parts)));
    emit historyChanged();
    m_currentFrame = std::clamp(m_currentFrame, first, last) + count;
    if (m_rangeType == FrameRange)
        m_range = m_range.translated(count, 0);
    else
        setRange(NoRange, {});
    structureChanged();
}

void Editor::removeFrame()
{
    const auto [first, last] = frameSpan();
    const int count = last - first + 1;
    if (count >= m_doc->frameCount())
        return; // keep at least one frame
    std::vector<std::unique_ptr<Command>> parts;
    for (int i = 0; i < count; ++i) {
        auto remove = std::make_unique<RemoveFrameCommand>(first);
        remove->redo(*m_doc);
        parts.push_back(std::move(remove));
    }
    pushCommand(std::make_unique<CommandGroup>(count > 1 ? "Delete Frames" : "Delete Frame", std::move(parts)));
    emit historyChanged();
    m_currentFrame = std::min(first, m_doc->frameCount() - 1);
    setRange(NoRange, {});
    structureChanged();
}

void Editor::setFramesDuration(int ms)
{
    const auto [first, last] = frameSpan();
    setDurations(first, last, ms);
}

// One undo step; scrubbing / scrolling the same frames merges into it.
void Editor::setDurations(int first, int last, int ms)
{
    ms = std::clamp(ms, 1, 65535);
    SetFrameDurationsCommand::Durations before, after;
    for (int f = first; f <= last; ++f) {
        before.push_back({f, m_doc->frame(f).durationMs});
        after.push_back({f, ms});
    }
    if (before == after)
        return;
    execute(std::make_unique<SetFrameDurationsCommand>(std::move(before), std::move(after)),
            continuing(QStringLiteral("durations:%1-%2").arg(first).arg(last)));
    emit frameDurationChanged();
    emit framesChanged();
}

// True when an edit of `key` follows one of the same kind within a moment,
// so it merges into one undo step (a scrub, a run of scroll ticks).
bool Editor::continuing(const QString& key)
{
    const bool merge = key == m_mergeKey && m_mergeTimer.isValid() && m_mergeTimer.elapsed() < 1000;
    m_mergeKey = key;
    m_mergeTimer.restart();
    return merge;
}

void Editor::setLayerProps(Layer* layer, const std::function<void(LayerProps&)>& change, const char* label,
                           bool continuous)
{
    if (!layer)
        return;
    const LayerProps before = LayerProps::of(*layer);
    LayerProps after = before;
    change(after);
    if (after == before)
        return;
    const QString key = QStringLiteral("%1:%2").arg(quintptr(layer)).arg(QLatin1String(label));
    const bool merge = continuous ? continuing(key) : (m_mergeKey.clear(), false);
    execute(std::make_unique<SetLayerPropsCommand>(*layer, before, after, label), merge);
    emit layersChanged();
    invalidateAll();
    refreshOverlay(); // e.g. a lock changes what can be drawn
}

void Editor::reverseFrames()
{
    const auto [first, last] = frameSpan();
    if (first == last)
        return;
    execute(std::make_unique<ReverseFramesCommand>(first, last));
    if (m_currentFrame >= first && m_currentFrame <= last)
        m_currentFrame = first + last - m_currentFrame;
    structureChanged();
}

// --- timeline range ---------------------------------------------------------

QRect Editor::range() const { return {m_range.x, m_range.y, m_range.w, m_range.h}; }

void Editor::setRange(RangeType type, Rect range)
{
    m_layerBlock = false; // selectLayerBlock marks it again
    // Any new range is a new selection: picked layers go. (togglePick sets its picks after this.)
    if (!m_picks.empty()) {
        m_picks.clear();
        emit rangeChanged();
    }
    if (type == NoRange || range.isEmpty()) {
        type = NoRange;
        range = {};
    }
    if (type == m_rangeType && range == m_range)
        return;
    m_rangeType = type;
    m_range = range;
    emit rangeChanged();
}

void Editor::selectFrames(int from, int to)
{
    const int last = m_doc->frameCount() - 1;
    from = std::clamp(from, 0, last);
    to = std::clamp(to, 0, last);
    if (from > to)
        std::swap(from, to);
    setRange(FrameRange, {from, 0, to - from + 1, int(m_layerList.size())});
}

void Editor::selectCels(int layerA, int frameA, int layerB, int frameB)
{
    const int lastLayer = int(m_layerList.size()) - 1;
    const int lastFrame = m_doc->frameCount() - 1;
    const int l0 = std::clamp(std::min(layerA, layerB), 0, lastLayer);
    const int l1 = std::clamp(std::max(layerA, layerB), 0, lastLayer);
    const int f0 = std::clamp(std::min(frameA, frameB), 0, lastFrame);
    const int f1 = std::clamp(std::max(frameA, frameB), 0, lastFrame);
    setRange(CelRange, {f0, l0, f1 - f0 + 1, l1 - l0 + 1});
}

void Editor::selectLayerBlock(int layerA, int layerB)
{
    selectCels(layerA, m_currentFrame, layerB, m_currentFrame);
    m_layerBlock = m_rangeType == CelRange;
}

void Editor::clearRange() { setRange(NoRange, {}); }

bool Editor::inRange(int layerIndex, int frame) const
{
    return m_rangeType != NoRange && m_range.contains(frame, layerIndex);
}

std::pair<int, int> Editor::frameSpan() const
{
    if (m_rangeType == NoRange)
        return {m_currentFrame, m_currentFrame};
    return {m_range.x, m_range.right() - 1};
}

std::pair<int, int> Editor::layerSpan() const
{
    if (m_rangeType == FrameRange)
        return {0, int(m_layerList.size()) - 1};
    if (m_rangeType == NoRange) {
        const int i = currentLayer();
        return {i, i};
    }
    return {m_range.y, m_range.bottom() - 1};
}

bool Editor::moveRange(int layerDelta, int frameDelta, bool copy)
{
    if (m_rangeType == NoRange || (layerDelta == 0 && frameDelta == 0))
        return false;
    endStroke();
    const auto [f0, f1] = frameSpan();

    if (m_rangeType == FrameRange) {
        const int count = f1 - f0 + 1;
        const int newFirst = std::clamp(f0 + frameDelta, 0, m_doc->frameCount() - count);
        if (newFirst == f0)
            return false;
        execute(std::make_unique<MoveFramesCommand>(f0, count, newFirst));
        if (m_currentFrame >= f0 && m_currentFrame <= f1)
            m_currentFrame += newFirst - f0;
        m_range = m_range.translated(newFirst - f0, 0);
        emit rangeChanged();
        structureChanged();
        return true;
    }

    // Cels: the block lands as a whole (empty cels included), so every
    // source row must land on a layer that holds cels and inside the frames.
    const auto [l0, l1] = layerSpan();
    const bool currentInside = inRange(currentLayer(), m_currentFrame);
    const int layers = int(m_layerList.size());
    if (f0 + frameDelta < 0 || f1 + frameDelta >= m_doc->frameCount())
        return false;
    std::map<std::pair<Layer*, int>, CelRef> target; // final cel per (layer, frame)
    std::unordered_map<const CelData*, CelRef> clones;
    // A live layer's cels are rendered from its recipe, so they neither move nor take cels.
    for (int l = l0; l <= l1; ++l) {
        Layer* src = m_layerList[size_t(l)];
        if (!src->hasCels() || src->isLive())
            continue;
        const int tl = l + layerDelta;
        if (tl < 0 || tl >= layers || !m_layerList[size_t(tl)]->hasCels() || m_layerList[size_t(tl)]->isLive())
            return false;
        if (!copy)
            for (int f = f0; f <= f1; ++f)
                target.try_emplace({src, f}, nullptr);
    }
    for (int l = l0; l <= l1; ++l) {
        Layer* src = m_layerList[size_t(l)];
        if (!src->hasCels() || src->isLive())
            continue;
        Layer* dst = m_layerList[size_t(l + layerDelta)];
        for (int f = f0; f <= f1; ++f) {
            CelRef cel = src->cel(f);
            if (copy && cel) {
                auto& clone = clones[cel.get()];
                if (!clone)
                    clone = cloneCel(cel);
                cel = clone;
            }
            target[{dst, f + frameDelta}] = cel;
        }
    }
    std::vector<std::unique_ptr<Command>> parts;
    for (const auto& [where, cel] : target) {
        auto [layer, frame] = where;
        if (layer->cel(frame) == cel)
            continue;
        auto set = std::make_unique<SetCelCommand>(*layer, frame, layer->cel(frame), cel);
        set->redo(*m_doc);
        parts.push_back(std::move(set));
    }
    if (!parts.empty()) {
        pushCommand(std::make_unique<CommandGroup>(copy ? "Copy Cels" : "Move Cels", std::move(parts)));
        emit historyChanged();
    }
    m_range = m_range.translated(frameDelta, layerDelta);
    emit rangeChanged();
    if (currentInside) { // the current cel moves with the block
        m_currentFrame += frameDelta;
        m_current = m_layerList[size_t(currentLayer() + layerDelta)];
    }
    structureChanged();
    return true;
}

namespace {

// Runs `edit(layer, frame)` for each cel slot of the span, collecting
// SetCelCommands for the slots it changes.
template <class F>
std::vector<std::unique_ptr<Command>> editCels(Document& doc, const std::vector<Layer*>& rows, std::pair<int, int> layers,
                                               std::pair<int, int> frames, F&& edit)
{
    std::vector<std::unique_ptr<Command>> parts;
    for (int l = layers.first; l <= layers.second; ++l) {
        Layer* layer = rows[size_t(l)];
        if (!layer->hasCels() || layer->isLive())
            continue;
        for (int f = frames.first; f <= frames.second; ++f) {
            const CelRef before = layer->cel(f);
            const CelRef after = edit(*layer, f);
            if (after == before)
                continue;
            auto set = std::make_unique<SetCelCommand>(*layer, f, before, after);
            set->redo(doc);
            parts.push_back(std::move(set));
        }
    }
    return parts;
}

} // namespace

void Editor::clearCels()
{
    endStroke();
    auto parts = editCels(*m_doc, m_layerList, layerSpan(), frameSpan(), [](Layer&, int) { return CelRef{}; });
    if (parts.empty())
        return;
    pushCommand(std::make_unique<CommandGroup>("Clear Cels", std::move(parts)));
    emit historyChanged();
    structureChanged();
}

void Editor::clearLayer()
{
    endStroke();
    if (!m_current || !m_current->hasCels() || effectivelyLocked(*m_current))
        return;
    const int row = currentLayer();
    auto parts = editCels(*m_doc, m_layerList, {row, row}, {0, m_doc->frameCount() - 1}, [](Layer&, int) { return CelRef{}; });
    if (parts.empty())
        return;
    pushCommand(std::make_unique<CommandGroup>("Clear Layer", std::move(parts)));
    emit historyChanged();
    structureChanged();
}

// Per layer, every frame of the span shows the first cel in it.
void Editor::linkCels()
{
    endStroke();
    const auto frames = frameSpan();
    auto parts = editCels(*m_doc, m_layerList, layerSpan(), frames, [&](Layer& layer, int f) {
        for (int g = frames.first; g <= frames.second; ++g)
            if (CelRef cel = layer.cel(g))
                return cel;
        return layer.cel(f);
    });
    if (parts.empty())
        return;
    pushCommand(std::make_unique<CommandGroup>("Link Cels", std::move(parts)));
    emit historyChanged();
    structureChanged();
}

// Cels of the span that share pixels with another frame get their own copy.
void Editor::unlinkCels()
{
    endStroke();
    auto parts = editCels(*m_doc, m_layerList, layerSpan(), frameSpan(), [](Layer& layer, int f) {
        const CelRef cel = layer.cel(f);
        if (!cel)
            return cel;
        const auto uses = std::count(layer.cels.begin(), layer.cels.end(), cel);
        return uses > 1 ? cloneCel(cel) : cel;
    });
    if (parts.empty())
        return;
    pushCommand(std::make_unique<CommandGroup>("Unlink Cels", std::move(parts)));
    emit historyChanged();
    structureChanged();
}

// --- tags -------------------------------------------------------------------

void Editor::setTags(std::vector<Tag> tags, const char* label)
{
    execute(std::make_unique<SetTagsCommand>(m_doc->tags, std::move(tags), label));
    emit tagsChanged();
}

void Editor::addTag()
{
    static constexpr uint32_t colors[] = {0x6e8bff, 0xff6b6b, 0x51cf66, 0xfcc419, 0xcc5de8, 0x22b8cf};
    const auto [from, to] = frameSpan();
    std::vector<Tag> tags = m_doc->tags;
    Tag tag;
    tag.name = "Tag " + std::to_string(tags.size() + 1);
    tag.from = from;
    tag.to = to;
    tag.color = Rgba::fromHex(colors[tags.size() % std::size(colors)]);
    tags.push_back(tag);
    setTags(std::move(tags), "New Tag");
}

void Editor::setTag(int index, const QVariantMap& values)
{
    if (index < 0 || index >= int(m_doc->tags.size()))
        return;
    std::vector<Tag> tags = m_doc->tags;
    Tag& tag = tags[size_t(index)];
    const int last = m_doc->frameCount() - 1;
    if (values.contains("name"))
        tag.name = values.value("name").toString().toStdString();
    if (values.contains("color"))
        tag.color = toRgba(values.value("color").value<QColor>());
    if (values.contains("direction"))
        tag.direction = AniDirection(std::clamp(values.value("direction").toInt(), 0, 3));
    if (values.contains("repeat"))
        tag.repeat = std::clamp(values.value("repeat").toInt(), 0, 65535);
    if (values.contains("from"))
        tag.from = std::clamp(values.value("from").toInt(), 0, last);
    if (values.contains("to"))
        tag.to = std::clamp(values.value("to").toInt(), 0, last);
    if (tag.from > tag.to)
        std::swap(tag.from, tag.to);
    const Tag& old = m_doc->tags[size_t(index)];
    if (tag.name == old.name && tag.color == old.color && tag.direction == old.direction && tag.repeat == old.repeat
        && tag.from == old.from && tag.to == old.to)
        return;
    setTags(std::move(tags), "Edit Tag");
}

void Editor::removeTag(int index)
{
    if (index < 0 || index >= int(m_doc->tags.size()))
        return;
    std::vector<Tag> tags = m_doc->tags;
    tags.erase(tags.begin() + index);
    setTags(std::move(tags), "Delete Tag");
}

void Editor::nextFrame() { setCurrentFrame((m_currentFrame + 1) % m_doc->frameCount()); }

void Editor::previousFrame()
{
    setCurrentFrame((m_currentFrame + m_doc->frameCount() - 1) % m_doc->frameCount());
}

// --- drawing ----------------------------------------------------------------

// Pixel value of a color in the document's format.
uint32_t Editor::encodeColor(const QColor& color) const
{
    return fromRgba(*m_doc, toRgba(color));
}

uint32_t Editor::transparentValue() const
{
    return m_doc->colorMode() == ColorMode::Indexed ? uint32_t(m_doc->transparentIndex) : 0;
}

std::vector<uint32_t> Editor::shadeRamp() const
{
    const Palette& palette = m_doc->palette;
    int first = 0, last = palette.size() - 1;
    if (m_shadeRange.x() >= 0) {
        first = std::min(m_shadeRange.x(), last);
        last = std::min(m_shadeRange.y(), last);
    }
    std::vector<uint32_t> ramp;
    for (int i = first; i <= last; ++i) {
        // Indexed pixels are the entries themselves (a color listed twice is
        // two steps); other modes match colors.
        const uint32_t value = m_doc->colorMode() == ColorMode::Indexed ? uint32_t(i) : fromRgba(*m_doc, palette.color(i));
        if (!isTransparent(value) && palette.color(i).a != 0)
            ramp.push_back(value);
    }
    return ramp;
}

// Image of the current cel, created or grown to cover the canvas if needed.
// Null when the current layer can't be drawn on (group, tilemap, hidden or
// locked, including through an enclosing group).
std::shared_ptr<Image> Editor::editableImage(Point& origin, bool* created, bool create)
{
    if (created)
        *created = false;
    Layer* layer = m_current;
    if (!layer || layer->kind != LayerKind::Image || !effectivelyVisible(*layer) || effectivelyLocked(*layer))
        return nullptr;

    // The canvas, and in the tiled view a canvas more on each tiled side:
    // strokes paint past the edge there before wrapStroke moves it over.
    const int w = m_doc->width(), h = m_doc->height();
    const Rect area{tileX() ? -w : 0, tileY() ? -h : 0, tileX() ? 3 * w : w, tileY() ? 3 * h : h};
    CelRef& cel = layer->cels[size_t(m_currentFrame)];
    if (!cel) {
        if (!create)
            return nullptr;
        cel = std::make_shared<CelData>();
        cel->image = std::make_shared<Image>(area.w, area.h, m_doc->pixelFormat());
        cel->image->clear(transparentValue());
        cel->position = {area.x, area.y};
        if (created)
            *created = true;
        emit celsChanged();
    } else {
        const Rect celRect{cel->position.x, cel->position.y, cel->image->width(), cel->image->height()};
        const Rect needed = celRect.united(area);
        if (needed != celRect) {
            // Grow without cropping anything outside the canvas.
            auto grown = std::make_shared<Image>(needed.w, needed.h, cel->image->format());
            grown->clear(transparentValue());
            grown->blit(*cel->image, {celRect.x - needed.x, celRect.y - needed.y});
            cel->image = grown;
            cel->position = {needed.x, needed.y};
        }
    }
    origin = cel->position;
    return cel->image;
}

void Editor::pushPixelEdit(const std::shared_ptr<Image>& image, Rect r, const Image& before, const std::string& label,
                           bool createdCel)
{
    auto patch = std::make_unique<ImagePatchCommand>(image, Point{r.x, r.y}, before.copy(r), image->copy(r), label);
    if (createdCel) {
        std::vector<std::unique_ptr<Command>> parts;
        parts.push_back(std::make_unique<SetCelCommand>(*m_current, m_currentFrame, nullptr,
                                                        m_current->cels[size_t(m_currentFrame)]));
        parts.push_back(std::move(patch));
        pushCommand(std::make_unique<CommandGroup>(label, std::move(parts)));
    } else {
        pushCommand(std::move(patch));
    }
    emit historyChanged();
}

// A cel created for an edit that changed nothing goes away again.
void Editor::dropCreatedCel()
{
    m_current->cels[size_t(m_currentFrame)] = nullptr;
    emit celsChanged();
}

bool Editor::isTransparent(uint32_t value) const
{
    switch (m_doc->colorMode()) {
    case ColorMode::Rgba: return (value >> 24) == 0;
    case ColorMode::Grayscale: return (value >> 8) == 0;
    case ColorMode::Indexed: return int(value) == m_doc->transparentIndex;
    }
    return false;
}

// The selection's bounds in the coordinates of the stroke's image (empty = none).
Rect Editor::selectionInStrokeImage() const
{
    return m_doc->selection.isEmpty() ? Rect{}
                                      : m_doc->selection.bounds().translated(-m_stroke.origin.x, -m_stroke.origin.y);
}

// Drawing stays inside the selection: puts back what changed outside it.
void Editor::restoreOutsideSelection(Rect area)
{
    if (m_doc->selection.isEmpty())
        return;
    const Point o = m_stroke.origin;
    Image& img = *m_stroke.image;
    area = area.intersected(img.bounds());
    for (int y = area.y; y < area.bottom(); ++y)
        for (int x = area.x; x < area.right(); ++x)
            if (!m_doc->selection.contains(x + o.x, y + o.y))
                img.setPixel(x, y, m_stroke.before.pixel(x, y));
}

void Editor::keepAlphaInStroke(Rect area)
{
    if (m_current && m_current->isAlphaLocked())
        keepAlpha(*m_stroke.image, m_stroke.before, area, transparentValue());
}

void Editor::beginStroke(QPoint pixel, bool secondary, Qt::KeyboardModifiers modifiers)
{
    if (m_stroke.active)
        endStroke();
    if (!m_gradientEdit.redrawing)
        closeGradient(); // a click elsewhere leaves the gradient as it is
    // A click on the canvas drops a multi-layer selection (the current layer stays).
    if (((m_rangeType == CelRange && m_range.h > 1) || picksActive()) && m_tool != Hand && m_tool != Move)
        setRange(NoRange, {});
    // Tiled view: from the copy it starts on into the sprite; moveStroke
    // shifts the same way, so the stroke stays continuous across edges.
    {
        const QPoint w = wrapPixel(pixel);
        m_tileShift = {pixel.x() - w.x(), pixel.y() - w.y()};
        pixel = w;
    }
    if (!(m_playing && liveLayer() && (m_tool == Move || m_tool == Eyedropper)))
        setPlaying(false);
    // The paste keeps floating only while it's being moved.
    const bool onFloat = m_float.active && m_float.rect().contains(toPoint(pixel));
    if (m_float.active && !(m_tool == Move || (m_tool == Select && m_doc->selection.contains(toPoint(pixel)))
                            || (m_tool == Text && onFloat)))
        commitFloating();

    // Cmd / Ctrl + click picks the layer under the cursor.
    if ((modifiers & Qt::ControlModifier) && m_tool != Hand) {
        selectLayerAt(pixel);
        return;
    }
    // Alt with the pencil, eraser, fill or gradient picks colors (dragging keeps
    // picking), as the eyedropper does. The shapes use Alt for "from the
    // center".
    if ((modifiers & Qt::AltModifier) && (m_tool == Pencil || m_tool == Eraser || m_tool == Fill || m_tool == Gradient)) {
        m_stroke = {};
        m_stroke.active = true;
        m_stroke.kind = Stroke::Pick;
        m_stroke.modifiers = modifiers;
        m_stroke.last = toPoint(pixel);
        m_pickSecondary = secondary;
        pickColor(pixel, secondary);
        return;
    }

    // Tile mode: the pencil places the picked tile (or stamp), the eraser
    // clears tiles, the eyedropper picks one (or drags out a stamp), fill,
    // line and rectangle work on cells (other tools work as usual).
    if (tilesActive() && (m_tool == Pencil || m_tool == Eraser || m_tool == Eyedropper || m_tool == Fill
                          || m_tool == Line || m_tool == Rectangle || m_tool == Ellipse)) {
        const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        const Tileset* set = currentTileset();
        if (m_tool == Eyedropper) {
            pickTile(toPoint(pixel));
            const CelRef cel = m_current->cel(m_currentFrame);
            if (!cel || !cel->image)
                return;
            m_stroke = {};
            m_stroke.active = true;
            m_stroke.kind = Stroke::TilePick;
            m_stroke.image = cel->image;
            m_stroke.origin = cel->position;
            m_stroke.start = {floorDiv(pixel.x() - cel->position.x, set->tileWidth),
                              floorDiv(pixel.y() - cel->position.y, set->tileHeight)};
            m_stroke.last = {pixel.x() - cel->position.x, pixel.y() - cel->position.y};
            return;
        }
        Point origin;
        bool created = false;
        auto map = editableTilemap(origin, &created);
        if (!map)
            return;
        m_stroke = {};
        m_stroke.active = true;
        m_stroke.kind = (m_tool == Line || m_tool == Rectangle || m_tool == Ellipse) ? Stroke::TileShape : Stroke::Tiles;
        m_stroke.image = map;
        m_stroke.before = *map;
        m_stroke.origin = origin;
        m_stroke.createdCel = created;
        // The secondary button erases too, as with tiles there's no second color.
        m_stroke.value = m_tool == Eraser || secondary ? 0u : (uint32_t(m_currentTile) | m_tileFlips);
        m_stroke.useStamp = m_tool == Pencil && !secondary && m_stampW * m_stampH > 1;
        m_stroke.label = m_tool == Eraser || secondary ? QStringLiteral("Erase Tiles")
                       : m_tool == Fill ? QStringLiteral("Fill Tiles")
                       : m_tool == Line ? QStringLiteral("Tile Line")
                       : m_tool == Rectangle ? QStringLiteral("Tile Rectangle")
                       : m_tool == Ellipse ? QStringLiteral("Tile Ellipse") : QStringLiteral("Place Tiles");
        m_stroke.last = {pixel.x() - origin.x, pixel.y() - origin.y}; // relative to the cel, as moveStroke keeps it
        m_stroke.start = {floorDiv(m_stroke.last.x, set->tileWidth), floorDiv(m_stroke.last.y, set->tileHeight)};
        if (m_tool == Fill) {
            tileFill(m_stroke.start);
            endStroke();
        } else if (m_stroke.kind == Stroke::TileShape) {
            drawTileShape();
        } else {
            stampTile(toPoint(pixel));
        }
        return;
    }

    switch (m_tool) {
    case Eyedropper:
        pickColor(pixel, secondary);
        return;
    case Fill:
        fill(pixel, secondary);
        return;
    case Hand:
        return;
    case Select:
        // Without Shift / Alt, dragging inside the selection moves its
        // pixels, as with the move tool; a click without dragging still
        // deselects.
        if (selectModeFor(modifiers) == Stroke::Replace && m_doc->selection.contains(toPoint(pixel))) {
            if (tilesActive())
                beginMoveCells(pixel, modifiers);
            else
                beginMove(pixel);
            if (m_stroke.active)
                m_stroke.fromSelectTool = true;
            else
                deselect(); // nothing to move on this layer
            return;
        }
        beginSelectStroke(pixel, modifiers);
        return;
    case Lasso:
        if (m_lassoPolygon)
            polygonClick(pixel, modifiers);
        else
            beginSelectStroke(pixel, modifiers);
        return;
    case MagicWand:
        magicWand(pixel, modifiers);
        return;
    case Move:
        if (tilesActive()) { // the selected cells, not pixels
            beginMoveCells(pixel, modifiers);
            return;
        }
        beginMove(pixel);
        return;
    case SliceTool: // drawn and edited by SliceOverlay.qml
        return;
    case SourceTool:
        beginSourceDraw(pixel, modifiers);
        return;
    case FieldTool:
        placeFieldAt(pixel);
        return;
    case PoseTool: // its handles do the work (SmearHandles.qml); a press elsewhere does nothing
        return;
    case Text:
        // On the text being typed: drag it. Elsewhere: a new text there.
        if (m_float.active && onFloat) {
            beginMove(pixel);
            emit textStarted(); // typing goes on after the drag
        } else if (!reopenLastText(toPoint(pixel))) {
            m_text = {true, toPoint(pixel), {}};
            m_textCursor = 0;
            emit textChanged();
            emit textStarted();
        }
        return;
    case Pencil:
    case Eraser:
    case Blur:
    case Jumble:
    case Line:
    case Rectangle:
    case Ellipse:
    case Gradient:
        break;
    }

    // Shading works with the pencil and the outlines of the line, rectangle and ellipse tools.
    const bool shading = m_shading && (m_tool == Pencil || m_tool == Line || ((m_tool == Rectangle || m_tool == Ellipse) && !m_fillShapes));
    const bool softens = m_tool == Blur || m_tool == Jumble;
    Point origin;
    bool created = false;
    // Pixel mode on a tilemap layer paints the map laid out as pixels.
    const bool tilePixels = !m_editTiles && tilemapLayer();
    Stroke tileState;
    std::shared_ptr<Image> image;
    if (tilePixels) {
        image = beginTilePixels(origin, created);
        tileState = std::move(m_stroke); // what beginTilePixels kept
    } else {
        image = editableImage(origin, &created, !shading && !softens); // nothing to shade or blur on an empty cel
    }
    if (!image)
        return;
    m_stroke = {};
    if (tilePixels) {
        m_stroke.tiles = true;
        m_stroke.map = std::move(tileState.map);
        m_stroke.mapBefore = std::move(tileState.mapBefore);
        m_stroke.tilesetId = tileState.tilesetId;
        m_stroke.tilesetBefore = std::move(tileState.tilesetBefore);
        m_stroke.tilesAtStart = tileState.tilesAtStart;
    }
    m_stroke.active = true;
    m_stroke.createdCel = created;
    m_stroke.kind = (m_tool == Pencil || m_tool == Eraser || softens) ? Stroke::Freehand : Stroke::Shape;
    m_stroke.softens = softens;
    m_stroke.jumble = m_tool == Jumble;
    m_stroke.seed = uint32_t(QRandomGenerator::global()->generate());
    m_stroke.modifiers = modifiers;
    m_stroke.image = image;
    m_stroke.before = *image;
    m_stroke.origin = origin;
    m_stroke.value = m_tool == Eraser ? transparentValue() : encodeColor(secondary ? m_secondary : m_primary);
    m_stroke.label = QString::fromLatin1(QMetaEnum::fromType<Tool>().valueToKey(m_tool));
    if (m_brushShape == CustomBrush && hasCustomBrush() && !softens && m_tool != Gradient) {
        m_stroke.brushes = customBrushes(true, true);
        m_stroke.brushOwnColors = m_brushColors && m_tool != Eraser && !shading;
    } else if (m_tool != Gradient && usesBrush(m_tool) && brushIsImage()) {
        m_stroke.brushes = customBrushes(true, false); // the built-in shape, turned
    }
    if (m_dither && !softens && m_tool != Gradient) {
        m_stroke.covered = Image(image->width(), image->height(), PixelFormat::Indexed8);
        // Two-color dither: the other mouse button's color (not for erasing or shading).
        if (m_ditherOtherColor && m_tool != Eraser && !shading)
            m_stroke.ditherOff = encodeColor(secondary ? m_primary : m_secondary);
    }
    if (shading) {
        m_stroke.ramp = shadeRamp();
        m_stroke.shadeStep = secondary ? -1 : 1;
        m_stroke.label = QStringLiteral("Shading");
    }
    const Point p{pixel.x() - origin.x, pixel.y() - origin.y};
    m_stroke.start = m_stroke.last = p;
    refreshOverlay(); // hides the Shift-line preview
    if (m_stroke.kind == Stroke::Freehand) {
        // Shift-click: a straight line from the last painted pixel.
        if ((modifiers & Qt::ShiftModifier) && m_lastPainted)
            plotSegment({m_lastPainted->x - origin.x, m_lastPainted->y - origin.y}, p);
        else
            plotSegment(p, p);
    } else {
        drawShape();
    }
}

void Editor::moveStroke(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    if (!m_stroke.active)
        return;
    pixel -= QPoint(m_tileShift.x, m_tileShift.y);
    const bool modifiersChanged = modifiers != m_stroke.modifiers;
    m_stroke.modifiers = modifiers;
    const Point p{pixel.x() - m_stroke.origin.x, pixel.y() - m_stroke.origin.y};
    if (p == m_stroke.last && !modifiersChanged)
        return;
    const Point from = m_stroke.last;
    m_stroke.last = p;
    m_stroke.moved = true;
    switch (m_stroke.kind) {
    case Stroke::Freehand: plotSegment(from, p); break;
    case Stroke::Pick: pickColor(wrapPixel(pixel), m_pickSecondary); break;
    case Stroke::Tiles: {
        // Every pixel along the way, so a fast drag doesn't skip cells.
        const Point o = m_stroke.origin;
        const int steps = std::max(std::abs(p.x - from.x), std::abs(p.y - from.y));
        for (int i = 1; i <= steps; ++i)
            stampTile({o.x + from.x + (p.x - from.x) * i / steps, o.y + from.y + (p.y - from.y) * i / steps});
        break;
    }
    case Stroke::Source:
        dragSourceTo(pixel);
        break;
    case Stroke::Field:
        if (liveLayer() && m_stroke.celStart.x < int(m_current->particles->fields.size())
            && m_current->particles->fields[size_t(m_stroke.celStart.x)].type == ParticleField::Type::Collider) {
            // A collider is drawn: a straight line from where the press was to here.
            if (p == m_stroke.start)
                break; // a click keeps the roof it made
            ParticleRecipe r = *m_current->particles;
            ParticleField& f = r.fields[size_t(m_stroke.celStart.x)];
            f.x = m_stroke.start.x + 0.5;
            f.y = m_stroke.start.y + 0.5;
            f.points = {{0, 0}, {double(p.x - m_stroke.start.x), double(p.y - m_stroke.start.y)}};
            setParticleRecipe(r, "Particle Field", true);
        } else {
            setParticleFieldPosition(m_stroke.celStart.x, pixel.x() + 0.5, pixel.y() + 0.5, true);
        }
        break;
    case Stroke::MoveEmitter:
        setLiveEmitter({m_stroke.celStart.x / 2.0 + (p.x - m_stroke.start.x), m_stroke.celStart.y / 2.0 + (p.y - m_stroke.start.y)});
        break;
    case Stroke::MoveSmear: {
        const int dx = p.x - m_stroke.start.x, dy = p.y - m_stroke.start.y;
        const int stepX = dx - m_stroke.celStart.x, stepY = dy - m_stroke.celStart.y;
        if ((stepX || stepY) && m_current && m_current->isSmear()) {
            // The first step is a new undo step, the rest of the drag goes into it.
            execute(std::make_unique<MoveSmearGroupCommand>(*m_current, stepX, stepY), m_stroke.celStart != Point{0, 0});
            m_stroke.celStart = {dx, dy};
            emit celsChanged();
            invalidateAll();
        }
        break;
    }
    case Stroke::TileShape: drawTileShape(); break;
    case Stroke::TilePick: {
        const Tileset* set = currentTileset();
        if (!set)
            break;
        const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        const Point c{floorDiv(p.x, set->tileWidth), floorDiv(p.y, set->tileHeight)};
        Rect r{std::min(c.x, m_stroke.start.x), std::min(c.y, m_stroke.start.y), std::abs(c.x - m_stroke.start.x) + 1,
               std::abs(c.y - m_stroke.start.y) + 1};
        r = r.intersected(m_stroke.image->bounds());
        m_stroke.drawn = r;
        const QRect canvas = r.isEmpty() || r.w * r.h < 2
            ? QRect()
            : QRect(m_stroke.origin.x + r.x * set->tileWidth, m_stroke.origin.y + r.y * set->tileHeight,
                    r.w * set->tileWidth, r.h * set->tileHeight);
        if (canvas != m_tilePickRect) {
            m_tilePickRect = canvas;
            emit tileCursorChanged();
        }
        break;
    }
    case Stroke::Shape: drawShape(); break;
    case Stroke::Select: updateSelectionStroke(); break;
    case Stroke::MoveSelection:
    case Stroke::MoveCel:
    case Stroke::MoveMany:
    case Stroke::MoveCells:
    case Stroke::MoveFloat: updateMove(); break;
    }
}

// Shift/Alt pressed or released mid-drag: redraw with the new constraints.
void Editor::setStrokeModifiers(Qt::KeyboardModifiers modifiers)
{
    if (!m_stroke.active || m_stroke.kind == Stroke::Freehand || modifiers == m_stroke.modifiers)
        return;
    m_stroke.modifiers = modifiers;
    if (m_stroke.kind == Stroke::Shape)
        drawShape();
    else if (m_stroke.kind == Stroke::Select && m_stroke.moved)
        updateSelectionStroke();
}

void Editor::plotSegment(Point from, Point to)
{
    Image& img = *m_stroke.image;
    const bool perfect = m_pixelPerfect && m_brushSize == 1 && !m_stroke.brushes && !m_stroke.softens;
    auto key = [](Point p) { return uint64_t(uint32_t(p.x)) << 32 | uint32_t(p.y); };
    const bool symmetric = perfect && (symmetryElements().size() > 1 || m_symmetryRadial >= 2);
    Rect changed;
    bresenham(from, to, [&](Point p) {
        if (perfect && p != m_stroke.lastPushed) {
            const int step = ++m_stroke.step;
            const int cornerStep = m_stroke.lastStep;
            m_stroke.lastPushed = p;
            m_stroke.lastStep = step;
            const auto corner = m_stroke.filter.push(p);
            if (corner && img.bounds().contains(*corner) && m_stroke.firstPainted[key(*corner)] == cornerStep) {
                const Point o = m_stroke.origin;
                for (const Point m : mirrored({corner->x + o.x, corner->y + o.y}, 1)) {
                    const Point c{m.x - o.x, m.y - o.y};
                    // Another kept point (or its mirror image) paints here too: the pixel stays.
                    if (symmetric && --m_stroke.owners[key(c)] > 0)
                        continue;
                    if (!img.bounds().contains(c))
                        continue;
                    img.setPixel(c.x, c.y, m_stroke.before.pixel(c.x, c.y));
                    if (!m_stroke.covered.isNull())
                        m_stroke.covered.setPixel(c.x, c.y, 0); // not part of the stroke any more
                    changed = changed.united({c.x, c.y, 1, 1});
                    // A pixel painted past a tiled edge was moved to the other
                    // side already: its copy goes too.
                    if (m_tileMode && !m_doc->bounds().contains(m.x, m.y)) {
                        const QPoint wrapped = wrapPixel(QPoint(m.x, m.y));
                        const Point wc{wrapped.x() - o.x, wrapped.y() - o.y};
                        if (img.bounds().contains(wc)) {
                            img.setPixel(wc.x, wc.y, m_stroke.before.pixel(wc.x, wc.y));
                            if (!m_stroke.covered.isNull())
                                m_stroke.covered.setPixel(wc.x, wc.y, 0);
                            changed = changed.united({wc.x, wc.y, 1, 1});
                        }
                    }
                }
                m_stroke.firstPainted.erase(key(*corner)); // may be painted again later
            }
            if (m_stroke.firstPainted.emplace(key(p), step).second && symmetric)
                for (const Point m : mirrored({p.x + m_stroke.origin.x, p.y + m_stroke.origin.y}, 1))
                    ++m_stroke.owners[key({m.x - m_stroke.origin.x, m.y - m_stroke.origin.y})];
        }
        changed = changed.united(m_stroke.softens ? softenMirrored(p)
                                 : m_stroke.ramp.empty() ? stampStroke(img, p) : shadeMirrored(p));
        if (!m_stroke.covered.isNull())
            stampStroke(m_stroke.covered, p, 1);
    });
    ditherStroke(changed);
    changed = wrapStroke(changed);
    restoreOutsideSelection(changed);
    keepAlphaInStroke(changed);
    if (m_stroke.tiles)
        retile(changed);
    if (!m_doc->selection.isEmpty())
        changed = changed.intersected(selectionInStrokeImage()); // nothing changed outside it
    m_stroke.bounds = m_stroke.bounds.united(changed);
    invalidate(changed.translated(m_stroke.origin.x, m_stroke.origin.y));
}

// Redraws the line/rectangle/ellipse from start to the current point over the
// image as it was before the stroke.
void Editor::drawShape()
{
    Image& img = *m_stroke.image;
    const Rect previous = m_stroke.drawn;
    const bool dithering = !m_stroke.covered.isNull();
    if (!previous.isEmpty()) {
        img.blit(m_stroke.before.copy(previous), {previous.x, previous.y});
        if (dithering)
            m_stroke.covered.fillRect(previous, 0);
    }

    const bool constrain = m_stroke.modifiers & Qt::ShiftModifier;
    const bool fromCenter = m_stroke.modifiers & Qt::AltModifier;
    const Point a = m_stroke.start;
    const Point b = m_stroke.last;
    Rect drawn;
    const Point o = m_stroke.origin;
    auto stamp = [&](Point p) {
        drawn = drawn.united(m_stroke.ramp.empty() ? stampStroke(img, p) : shadeMirrored(p));
        if (dithering)
            stampStroke(m_stroke.covered, p, 1);
    };
    auto fillRow = [&](int y, int x0, int x1) {
        if (m_symmetryRadial >= 2) { // turned copies don't make rects: pixel by pixel
            for (int x = x0; x <= x1; ++x)
                for (const Point q : mirrored({x + o.x, y + o.y}, 1)) {
                    const Point c{q.x - o.x, q.y - o.y};
                    if (!img.bounds().contains(c))
                        continue;
                    img.setPixel(c.x, c.y, m_stroke.value);
                    if (dithering)
                        m_stroke.covered.setPixel(c.x, c.y, 1);
                    drawn = drawn.united({c.x, c.y, 1, 1});
                }
            return;
        }
        // The row mirrors as a rect (across a diagonal it becomes a column).
        for (const int e : symmetryElements()) {
            const Rect m = mirrorRect({x0 + o.x, y + o.y, x1 - x0 + 1, 1}, e, m_symmetryCenter2);
            const Rect row = m.translated(-o.x, -o.y).intersected(img.bounds());
            img.fillRect(row, m_stroke.value);
            if (dithering)
                m_stroke.covered.fillRect(row, 1);
            drawn = drawn.united(row);
        }
    };

    if (m_tool == Gradient) {
        // Across the whole cel; what's outside the selection is put back below.
        const Rect all = img.bounds();
        const double dx = double(b.x - a.x), dy = double(b.y - a.y);
        const double length2 = dx * dx + dy * dy;
        setGradientLine({a.x + o.x, a.y + o.y}, {b.x + o.x, b.y + o.y});
        if (length2 > 0) {
            const std::vector<GradientStop> stops = gradientStops();
            std::vector<double> positions;
            for (const GradientStop& stop : stops)
                positions.push_back(stop.position);
            // The stops in the document's pixels, and 256 steps through them (a smooth gradient).
            std::vector<uint32_t> values;
            for (const GradientStop& stop : stops)
                values.push_back(encodeColor(stop.color));
            std::array<uint32_t, 256> steps;
            for (int i = 0; i < 256; ++i) {
                const RampStep s = rampStepAt(i / 255.0, positions);
                const QColor& c0 = stops[size_t(s.lower)].color;
                const QColor& c1 = stops[size_t(s.lower) + 1].color;
                const double t = s.frac;
                steps[size_t(i)] = encodeColor(QColor::fromRgbF(
                    float(c0.redF() + (c1.redF() - c0.redF()) * t), float(c0.greenF() + (c1.greenF() - c0.greenF()) * t),
                    float(c0.blueF() + (c1.blueF() - c0.blueF()) * t), float(c0.alphaF() + (c1.alphaF() - c0.alphaF()) * t)));
            }
            const GradientShape shape = GradientShape(m_gradientShape);
            const GradientRepeat repeat = GradientRepeat(m_gradientRepeat);
            const IndexedDither pattern = m_gradientDither == 1 ? IndexedDither::Bayer2
                                        : m_gradientDither == 2 ? IndexedDither::Ordered
                                        : m_gradientDither == 3 ? IndexedDither::Bayer8
                                        : m_gradientDither == 4 ? IndexedDither::BlueNoise : IndexedDither::None;
            // A position in 0..1 maps to the stops' own range: the colors before the first stop
            // and after the last stay flat.
            for (int y = all.y; y < all.bottom(); ++y)
                for (int x = all.x; x < all.right(); ++x) {
                    const double t = gradientPosition(shape, a, b, x, y, repeat);
                    uint32_t v;
                    if (pattern == IndexedDither::None) {
                        v = steps[size_t(std::lround(t * 255))];
                    } else { // two neighboring stops, the pattern (in document coordinates) picks
                        const RampStep s = rampStepAt(t, positions);
                        v = s.frac > double(ditherThreshold(pattern, x + o.x, y + o.y)) ? values[size_t(s.lower) + 1]
                                                                                      : values[size_t(s.lower)];
                    }
                    img.setPixel(x, y, v);
                }
            drawn = all;
        }
    } else if (m_tool == Line) {
        // On a grid, both ends are cell corners.
        const Point a0 = snapVertex(a, o);
        const Point end = constrain ? snapLine(a0, snapVertex(b, o)) : snapVertex(b, o);
        const Point begin = fromCenter ? Point{2 * a0.x - end.x, 2 * a0.y - end.y} : a0;
        bresenham(begin, end, stamp);
    } else {
        const Rect r = snapCells(shapeRect(a, b, constrain, fromCenter), o);
        if (m_tool == Rectangle) {
            if (m_fillShapes)
                for (int y = r.y; y < r.bottom(); ++y)
                    fillRow(y, r.x, r.right() - 1);
            else
                rectOutline(r, stamp);
        } else if (m_fillShapes) {
            const auto spans = ellipseSpans(r);
            for (int i = 0; i < r.h; ++i)
                fillRow(r.y + i, spans[size_t(i)].first, spans[size_t(i)].second);
        } else {
            ellipseOutline(r, stamp);
        }
    }

    ditherStroke(drawn);
    drawn = wrapStroke(drawn);
    restoreOutsideSelection(drawn);
    keepAlphaInStroke(drawn);
    if (m_stroke.tiles)
        retile(previous.united(drawn)); // a shape's last place goes back too
    if (!m_doc->selection.isEmpty())
        drawn = drawn.intersected(selectionInStrokeImage());
    m_stroke.drawn = drawn;
    m_stroke.bounds = m_stroke.bounds.united(drawn).united(previous);
    invalidate(previous.united(drawn).translated(m_stroke.origin.x, m_stroke.origin.y));
}

// Shift / Alt held at the start: add to / subtract from the selection; both:
// keep only what's in both.
Editor::Stroke::SelectMode Editor::selectModeFor(Qt::KeyboardModifiers modifiers)
{
    const bool shift = modifiers & Qt::ShiftModifier;
    const bool alt = modifiers & Qt::AltModifier;
    return shift && alt ? Stroke::Intersect : shift ? Stroke::Add : alt ? Stroke::Subtract : Stroke::Replace;
}

const char* Editor::selectLabel(Stroke::SelectMode mode)
{
    switch (mode) {
    case Stroke::Add: return "Add to Selection";
    case Stroke::Subtract: return "Subtract from Selection";
    case Stroke::Intersect: return "Intersect Selection";
    case Stroke::Replace: break;
    }
    return "Select";
}

Mask Editor::combinedSelection(const Mask& base, const Mask& shape, Stroke::SelectMode mode) const
{
    switch (mode) {
    case Stroke::Add: return base.united(shape);
    case Stroke::Subtract: return base.subtracted(shape);
    case Stroke::Intersect: return base.intersected(shape);
    case Stroke::Replace: break;
    }
    return shape;
}

// A drag of the select or lasso tool; a click without dragging deselects
// (see endStroke).
void Editor::beginSelectStroke(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    m_stroke = {};
    m_stroke.active = true;
    m_stroke.kind = Stroke::Select;
    m_stroke.modifiers = modifiers;
    m_stroke.selectMode = selectModeFor(modifiers);
    m_stroke.modeModifiers = modifiers & (Qt::ShiftModifier | Qt::AltModifier);
    m_stroke.selectionStart = m_doc->selection;
    m_stroke.start = m_stroke.last = toPoint(pixel);
    if (m_tool == Lasso)
        m_stroke.lasso = {m_stroke.start};
}

// With symmetry on, a selection takes the mirrored areas too (cut to the canvas).
Mask Editor::symmetricSelection(const Mask& shape) const
{
    return symmetricMask(shape).intersected(m_doc->bounds());
}

Mask Editor::symmetricMask(const Mask& shape) const
{
    std::vector<Mask> parts; // the shape under each axis element
    for (const int element : symmetryElements())
        parts.push_back(mirroredMask(shape, element, m_symmetryCenter2));
    Mask all;
    for (const Mask& part : parts)
        all = all.united(part);
    if (m_symmetryRadial >= 2) {
        constexpr double kTwoPi = 6.283185307179586;
        for (const Mask& part : parts)
            for (int mirror = 0; mirror < (m_symmetryRadialMirror ? 2 : 1); ++mirror)
                for (int k = 0; k < m_symmetryRadial; ++k)
                    if (mirror || k)
                        all = all.united(turnedMask(part, kTwoPi * k / m_symmetryRadial, mirror != 0, m_symmetryCenter2));
    }
    return all;
}

void Editor::updateSelectionStroke()
{
    Mask shape;
    if (!m_stroke.lasso.empty()) {
        // Only the outline while dragging (LassoOverlay.qml draws lassoPath);
        // the shape is filled once, when let go (endStroke).
        // Alt held (other than the key that picked the mode): a straight segment from the last
        // point to the pointer; the point it ends on only stays when Alt is let go.
        const bool straight = (m_stroke.modifiers & ~m_stroke.modeModifiers) & Qt::AltModifier;
        if (straight) {
            if (!m_stroke.lassoTail) {
                m_stroke.lasso.push_back(m_stroke.last);
                m_stroke.lassoTail = true;
            } else {
                m_stroke.lasso.back() = m_stroke.last;
            }
            emit lassoPathChanged();
        } else if (m_stroke.lassoTail) {
            m_stroke.lasso.back() = m_stroke.last;
            m_stroke.lassoTail = false;
            emit lassoPathChanged();
        } else if (m_stroke.lasso.back() != m_stroke.last) {
            m_stroke.lasso.push_back(m_stroke.last);
            emit lassoPathChanged();
        }
        return;
    } else {
        // Keys that picked the mode at the start don't also constrain the shape.
        const Qt::KeyboardModifiers mods = m_stroke.modifiers & ~m_stroke.modeModifiers;
        const bool constrain = mods & Qt::ShiftModifier;
        const bool fromCenter = mods & Qt::AltModifier;
        const Rect drawn = shapeRect(m_stroke.start, m_stroke.last, constrain, fromCenter);
        // On a tilemap layer in tile mode a selection is whole cells (the tileset's grid).
        const Rect cells = tilesActive() ? snapToTileCells(drawn) : snapCells(drawn);
        shape = symmetricSelection(Mask(cells.intersected(m_doc->bounds())));
    }
    setSelection(combinedSelection(m_stroke.selectionStart, shape, m_stroke.selectMode));
}

// Selects the pixels like the one clicked in the current cel, or in what
// every visible layer shows (sampleMerged); contiguous or all of them,
// within the wand's tolerance; outside the cel counts as transparent. One
// undo step.
void Editor::magicWand(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    const auto mode = selectModeFor(modifiers);
    const Point p = toPoint(pixel);
    if (!m_doc->bounds().contains(p)) {
        if (mode == Stroke::Replace)
            deselect(); // as a click off the canvas with the select tool
        return;
    }
    Mask shape;
    Image canvas;
    if (!m_sampleMerged) {
        canvas = Image(m_doc->width(), m_doc->height(), m_doc->pixelFormat());
        canvas.clear(transparentValue());
        const CelRef cel = m_current && m_current->kind == LayerKind::Image ? m_current->cel(m_currentFrame) : nullptr;
        if (cel && cel->image)
            canvas.blit(*cel->image, cel->position);
    }
    // With symmetry, the mirrored points select too (a no-op where the first already reached).
    for (const Point q : mirrored(p, 1))
        if (m_doc->bounds().contains(q))
            shape = shape.united(similarMaskWrapped(m_sampleMerged ? m_composite : canvas, q, m_wandContiguous,
                                                    m_wandTolerance, tileX(), tileY()));
    commitFloating();
    setSelection(combinedSelection(m_doc->selection, shape, mode));
    recordSelection(selectLabel(mode));
}

// With a selection, moves its pixels within the current cel; without one,
// moves the whole cel (losslessly, through its position).
// Several layers picked together, or a range of cels or frames in the timeline (a single cel is the
// plain move or transform of the current one); with nothing floating and no pixels selected.
bool Editor::multiSelectionActive() const
{
    if (m_float.active || !m_doc->selection.isEmpty())
        return false;
    return picksActive() || m_rangeType == FrameRange || (m_rangeType == CelRange && (m_range.w > 1 || m_range.h > 1));
}

// Whether the Move tool drags several things at once.
bool Editor::multiMoveActive() const
{
    return m_tool == Move && multiSelectionActive();
}

// What a move or a flip of the selection takes along: every cel in the selected frames of the selected
// layers (groups count with what is in them; a linked cel once), the picked layers' cel on the current
// frame, and each live layer (its emitter). Hidden and locked layers stay where they are.
void Editor::gatherMoveTargets(MoveGroup& group) const
{
    std::vector<Layer*> layers;
    if (m_rangeType != NoRange) {
        const auto [l0, l1] = layerSpan();
        for (int l = l0; l <= l1; ++l)
            layers.push_back(m_layerList[size_t(l)]);
    } else {
        layers = m_picks;
    }
    const auto [f0, f1] = m_rangeType != NoRange ? frameSpan() : std::pair<int, int>{m_currentFrame, m_currentFrame};
    std::unordered_set<const CelData*> seenCels;
    std::unordered_set<const Layer*> seenLayers;
    const auto take = [&](auto&& self, Layer* layer) -> void {
        if (!seenLayers.insert(layer).second)
            return;
        if (layer->kind == LayerKind::Group) {
            for (const auto& child : layer->children)
                self(self, child.get());
            return;
        }
        // (A live layer counts as locked for pixel work, so its own lock is looked at by hand.)
        bool locked = false;
        for (const Layer* l = layer; l && l->parent; l = l->parent)
            locked = locked || l->locked;
        if (layer->kind != LayerKind::Image || !effectivelyVisible(*layer) || locked)
            return;
        if (layer->isLive()) {
            group.live.push_back({layer, *layer->particles});
            return;
        }
        for (int f = f0; f <= f1; ++f)
            if (const CelRef cel = layer->cel(f); cel && cel->image && seenCels.insert(cel.get()).second)
                group.cels.push_back({cel, cel->position}), group.celLayers.push_back(layer);
    };
    for (Layer* layer : layers)
        take(take, layer);
}

// Puts the cels at where they started plus `offset`, and each live layer's emitter too (drawing it
// again): live, while a selection is dragged or nudged.
void Editor::shiftGroup(const std::vector<std::pair<CelRef, Point>>& cels, std::vector<LiveStart>& live, Point offset)
{
    Rect touched;
    for (const auto& [cel, start] : cels) {
        const Rect old{cel->position.x, cel->position.y, cel->image->width(), cel->image->height()};
        cel->position = {start.x + offset.x, start.y + offset.y};
        touched = touched.united(old).united(old.translated(cel->position.x - old.x, cel->position.y - old.y));
    }
    for (LiveStart& l : live) {
        l.layer->particles->x = l.before.x + offset.x;
        l.layer->particles->y = l.before.y + offset.y;
        renderParticleLayer(*m_doc, *l.layer);
    }
    if (!live.empty()) {
        emit celsChanged();
        emit liveChanged();
        invalidateAll();
    } else {
        invalidate(touched);
    }
}

// Flips every selected cel about the middle of all of them together, so what was side by side stays so
// (frames of an animation keep their place relative to each other).
void Editor::flipMany(bool horizontal)
{
    commitFloating();
    MoveGroup group;
    gatherMoveTargets(group);
    if (group.cels.empty())
        return;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& [cel, start] : group.cels) {
        x0 = std::min(x0, cel->position.x);
        y0 = std::min(y0, cel->position.y);
        x1 = std::max(x1, cel->position.x + cel->image->width());
        y1 = std::max(y1, cel->position.y + cel->image->height());
    }
    std::vector<ReplaceCelsCommand::Change> changes;
    for (const auto& [cel, start] : group.cels) {
        const Point at = cel->position;
        ReplaceCelsCommand::Change c;
        c.cel = cel;
        c.imageBefore = cel->image;
        c.positionBefore = at;
        c.imageAfter = std::make_shared<Image>(horizontal ? flippedHorizontally(*cel->image) : flippedVertically(*cel->image));
        c.positionAfter = horizontal ? Point{x0 + x1 - (at.x + cel->image->width()), at.y}
                                     : Point{at.x, y0 + y1 - (at.y + cel->image->height())};
        changes.push_back(std::move(c));
    }
    execute(std::make_unique<ReplaceCelsCommand>(std::move(changes), horizontal ? "Flip Horizontal" : "Flip Vertical"));
    emit celsChanged();
    invalidateAll();
}

void Editor::beginMoveMany(QPoint pixel)
{
    MoveGroup group;
    gatherMoveTargets(group);
    m_stroke = {};
    if (group.cels.empty() && group.live.empty())
        return;
    m_stroke.kind = Stroke::MoveMany;
    m_stroke.manyCels = std::move(group.cels);
    m_stroke.manyLive = std::move(group.live);
    m_stroke.active = true;
    m_stroke.start = m_stroke.last = toPoint(pixel);
}

void Editor::beginMove(QPoint pixel)
{
    if (multiMoveActive()) {
        beginMoveMany(pixel);
        return;
    }
    // The move tool on a Smear group moves all of it: its art on every frame and its pivot.
    if (m_tool == Move && !m_float.active && m_current && m_current->isSmear() && effectivelyVisible(*m_current)) {
        m_stroke = {};
        m_stroke.kind = Stroke::MoveSmear;
        m_stroke.active = true;
        m_stroke.start = m_stroke.last = toPoint(pixel);
        m_stroke.celStart = {0, 0}; // how far it has been moved so far
        return;
    }
    // The move tool on a live layer moves its emitter (the crosshair), wherever it is dragged.
    if (m_tool == Move && !m_float.active && m_current && m_current->isLive() && effectivelyVisible(*m_current)) {
        m_stroke = {};
        m_stroke.kind = Stroke::MoveEmitter;
        m_stroke.active = true;
        m_stroke.start = m_stroke.last = toPoint(pixel);
        const QPointF at = liveEmitter();
        m_stroke.celStart = {int(std::lround(at.x() * 2)), int(std::lround(at.y() * 2))}; // half pixels
        return;
    }
    if (m_float.active) {
        m_stroke = {};
        m_stroke.kind = Stroke::MoveFloat;
        m_stroke.active = true;
        m_stroke.celStart = {m_float.box.x, m_float.box.y};
        m_stroke.start = m_stroke.last = toPoint(pixel);
        return;
    }
    Layer* layer = m_current;
    if (!layer || layer->kind != LayerKind::Image || !effectivelyVisible(*layer) || effectivelyLocked(*layer))
        return;

    if (m_doc->selection.isEmpty()) {
        if (m_tool != Move)
            return;
        CelRef cel = layer->cel(m_currentFrame);
        if (!cel || !cel->image)
            return;
        m_stroke = {};
        m_stroke.kind = Stroke::MoveCel;
        m_stroke.cel = cel;
        m_stroke.celStart = cel->position;
    } else {
        Point origin;
        auto image = editableImage(origin, nullptr, false);
        if (!image)
            return; // nothing on this frame to move
        m_stroke = {};
        m_stroke.kind = Stroke::MoveSelection;
        m_stroke.image = image;
        m_stroke.before = *image;
        m_stroke.origin = origin;
        m_stroke.selectionStart = m_doc->selection;
        // The selected pixels; the rest of the bounds is transparent.
        const Rect area = selectionInStrokeImage();
        m_stroke.floating = Image(area.w, area.h, image->format());
        m_stroke.floating.clear(transparentValue());
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x)
                if (image->bounds().contains(x, y) && m_doc->selection.contains(x + origin.x, y + origin.y))
                    m_stroke.floating.setPixel(x - area.x, y - area.y, image->pixel(x, y));
        splitMoveByPress(toPoint(pixel));
    }
    m_stroke.active = true;
    m_stroke.label = QStringLiteral("Move");
    // Relative to the stroke's image (origin is zero when moving the cel);
    // only the offset from start matters.
    m_stroke.start = m_stroke.last = {pixel.x() - m_stroke.origin.x, pixel.y() - m_stroke.origin.y};
}

// With symmetry on, a selection made of mirrored areas moves as the pointer drags it: the piece nearest
// the press follows the drag, the others move as its mirror images (or turned copies) would. `press` is in
// document coordinates. Leaves m_stroke.parts empty (one piece, the plain move) when the selection has
// one piece only.
void Editor::splitMoveByPress(Point press)
{
    m_stroke.parts.clear();
    const std::vector<Copy> copies = mirrored(press, 1, 1);
    if (copies.size() < 2)
        return;
    const Mask& selection = m_stroke.selectionStart;
    const Rect b = selection.bounds();
    std::vector<Image> maps(copies.size()); // per copy, the pixels nearest it (made when it gets any)
    for (int y = b.y; y < b.bottom(); ++y)
        for (int x = b.x; x < b.right(); ++x) {
            if (!selection.contains(x, y))
                continue;
            size_t best = 0;
            int64_t bestDistance = INT64_MAX;
            for (size_t i = 0; i < copies.size(); ++i) {
                const int64_t dx = x - copies[i].p.x, dy = y - copies[i].p.y;
                if (dx * dx + dy * dy < bestDistance) {
                    bestDistance = dx * dx + dy * dy;
                    best = i;
                }
            }
            if (maps[best].isNull()) {
                maps[best] = Image(b.w, b.h, PixelFormat::Indexed8);
                maps[best].clear(0);
            }
            maps[best].setPixel(x - b.x, y - b.y, 1);
        }
    const Point o = m_stroke.origin;
    const Image& image = *m_stroke.image;
    for (size_t i = 0; i < copies.size(); ++i) {
        if (maps[i].isNull())
            continue;
        MovePart part;
        part.mask = Mask::fromImage(maps[i], {b.x, b.y});
        part.element = copies[i].element;
        part.step = copies[i].step;
        const Rect pb = part.mask.bounds();
        part.block = Image(pb.w, pb.h, image.format());
        part.block.clear(transparentValue());
        for (int y = 0; y < pb.h; ++y)
            for (int x = 0; x < pb.w; ++x)
                if (part.mask.contains(pb.x + x, pb.y + y) && image.bounds().contains(pb.x + x - o.x, pb.y + y - o.y))
                    part.block.setPixel(x, y, image.pixel(pb.x + x - o.x, pb.y + y - o.y));
        m_stroke.parts.push_back(std::move(part));
    }
    if (m_stroke.parts.size() < 2)
        m_stroke.parts.clear();
}

// A drag of `offset` as piece `part` moves: mirrored by its element, then turned by its radial share.
Point Editor::partOffset(const MovePart& part, Point offset) const
{
    const Point v = applySymmetry(part.element, offset);
    if (part.step == 0 || m_symmetryRadial < 2)
        return v;
    const double angle = 6.283185307179586 * part.step / m_symmetryRadial;
    return {int(std::lround(v.x * std::cos(angle) - v.y * std::sin(angle))),
            int(std::lround(v.x * std::sin(angle) + v.y * std::cos(angle)))};
}

// The pieces of a symmetric move, each by its own offset.
void Editor::updateMoveParts(Point offset)
{
    Image& img = *m_stroke.image;
    const Rect previous = m_stroke.drawn;
    if (!previous.isEmpty())
        img.blit(m_stroke.before.copy(previous), {previous.x, previous.y});

    const Point o = m_stroke.origin;
    const Rect start = m_stroke.selectionStart.bounds();
    Rect drawn = start.translated(-o.x, -o.y).intersected(img.bounds());
    for (int y = drawn.y; y < drawn.bottom(); ++y)
        for (int x = drawn.x; x < drawn.right(); ++x)
            if (m_stroke.selectionStart.contains(x + o.x, y + o.y))
                img.setPixel(x, y, transparentValue());
    Mask moved;
    for (const MovePart& part : m_stroke.parts) {
        const Point d = partOffset(part, offset);
        const Rect pb = part.mask.bounds();
        for (int y = 0; y < part.block.height(); ++y)
            for (int x = 0; x < part.block.width(); ++x) {
                int tx = pb.x + x + d.x - o.x, ty = pb.y + y + d.y - o.y;
                const uint32_t v = part.block.pixel(x, y);
                if (m_tileMode) {
                    const QPoint w = wrapPixel(QPoint(tx + o.x, ty + o.y));
                    tx = w.x() - o.x;
                    ty = w.y() - o.y;
                }
                if (img.bounds().contains(tx, ty) && !isTransparent(v))
                    img.setPixel(tx, ty, v);
            }
        drawn = drawn.united(pb.translated(d.x - o.x, d.y - o.y).intersected(img.bounds()));
        moved = moved.united(part.mask.translated(d.x, d.y));
    }
    if (m_tileMode)
        drawn = drawn.united(Rect{-o.x, -o.y, m_doc->width(), m_doc->height()}.intersected(img.bounds()));
    m_stroke.drawn = drawn;
    m_stroke.bounds = m_stroke.bounds.united(drawn).united(previous);
    invalidate(previous.united(drawn).translated(o.x, o.y));
    setSelection(moved);
}

void Editor::updateMove()
{
    if (m_stroke.kind == Stroke::MoveCells) {
        updateMoveCells();
        return;
    }
    Point offset{m_stroke.last.x - m_stroke.start.x, m_stroke.last.y - m_stroke.start.y};
    // On a grid, what moves lands with its top-left on a cell corner.
    if (m_snapToGrid && m_stroke.kind != Stroke::MoveMany) {
        const Rect from = m_stroke.selectionStart.bounds();
        const bool whole = m_stroke.kind == Stroke::MoveFloat || m_stroke.kind == Stroke::MoveCel;
        offset = snapMoveBy(whole ? m_stroke.celStart : Point{from.x, from.y}, offset);
    }

    if (m_stroke.kind == Stroke::MoveFloat) {
        const Point shift{m_stroke.celStart.x + offset.x - m_float.box.x, m_stroke.celStart.y + offset.y - m_float.box.y};
        m_float.box = m_float.box.translated(shift.x, shift.y);
        m_float.pivot.reset(); // moved: the pivot goes with the box
        m_float.position = {m_float.position.x + shift.x, m_float.position.y + shift.y};
        m_float.mask = m_float.mask.translated(shift.x, shift.y);
        setSelection(m_float.mask);
        refreshOverlay();
        emit floatingChanged();
        return;
    }
    if (m_stroke.kind == Stroke::MoveMany) {
        shiftGroup(m_stroke.manyCels, m_stroke.manyLive, offset);
        return;
    }
    if (m_stroke.kind == Stroke::MoveCel) {
        CelData& cel = *m_stroke.cel;
        const Rect old{cel.position.x, cel.position.y, cel.image->width(), cel.image->height()};
        cel.position = {m_stroke.celStart.x + offset.x, m_stroke.celStart.y + offset.y};
        invalidate(old.united(old.translated(cel.position.x - old.x, cel.position.y - old.y)));
        return;
    }

    if (!m_stroke.parts.empty()) {
        updateMoveParts(offset);
        return;
    }
    Image& img = *m_stroke.image;
    const Rect previous = m_stroke.drawn;
    if (!previous.isEmpty())
        img.blit(m_stroke.before.copy(previous), {previous.x, previous.y});

    const Point o = m_stroke.origin;
    const Rect start = m_stroke.selectionStart.bounds();
    const Rect src = start.translated(-o.x, -o.y).intersected(img.bounds());
    const Rect dst{start.x - o.x + offset.x, start.y - o.y + offset.y, m_stroke.floating.width(),
                   m_stroke.floating.height()};
    for (int y = src.y; y < src.bottom(); ++y)
        for (int x = src.x; x < src.right(); ++x)
            if (m_stroke.selectionStart.contains(x + o.x, y + o.y))
                img.setPixel(x, y, transparentValue());
    // Transparent pixels of the moved block don't cover what's below.
    for (int y = 0; y < m_stroke.floating.height(); ++y)
        for (int x = 0; x < m_stroke.floating.width(); ++x) {
            int tx = dst.x + x, ty = dst.y + y;
            const uint32_t v = m_stroke.floating.pixel(x, y);
            if (m_tileMode) { // over the edge of a tiled axis it comes in at the other side
                const QPoint w = wrapPixel(QPoint(tx + o.x, ty + o.y));
                tx = w.x() - o.x;
                ty = w.y() - o.y;
            }
            if (img.bounds().contains(tx, ty) && !isTransparent(v))
                img.setPixel(tx, ty, v);
        }
    Rect drawn = src.united(dst).intersected(img.bounds());
    if (m_tileMode) // pixels may have wrapped anywhere on the canvas
        drawn = drawn.united(Rect{-o.x, -o.y, m_doc->width(), m_doc->height()}.intersected(img.bounds()));
    m_stroke.drawn = drawn;
    m_stroke.bounds = m_stroke.bounds.united(drawn).united(previous);
    invalidate(previous.united(drawn).translated(o.x, o.y));
    setSelection(m_stroke.selectionStart.translated(offset.x, offset.y));
}

void Editor::endStroke()
{
    if (!m_stroke.active)
        return;
    const size_t historyBefore = m_undo.position();
    switch (m_stroke.kind) {
    case Stroke::Pick:
    case Stroke::MoveEmitter:
    case Stroke::Source:
    case Stroke::MoveSmear: // each step was its own (merged) command
    case Stroke::Field:
        break;
    case Stroke::TilePick:
        finishTilePick();
        break;
    case Stroke::Tiles:
    case Stroke::TileShape:
        if (!m_stroke.bounds.isEmpty())
            pushPixelEdit(m_stroke.image, m_stroke.bounds, m_stroke.before, m_stroke.label.toStdString(),
                          m_stroke.createdCel);
        else if (m_stroke.createdCel)
            dropCreatedCel();
        break;
    case Stroke::Select:
        if (!m_stroke.moved) {
            if (m_stroke.selectMode == Stroke::Replace)
                clickDeselect(); // a plain click deselects; Shift / Alt clicks do nothing
        } else {
            if (!m_stroke.lasso.empty()) // closed by a straight line back to the start
                setSelection(combinedSelection(m_stroke.selectionStart, symmetricSelection(polygonMask(m_stroke.lasso, m_doc->bounds())),
                                               m_stroke.selectMode));
            recordSelection(selectLabel(m_stroke.selectMode));
        }
        break;
    case Stroke::MoveFloat:
        if (m_stroke.fromSelectTool && !m_stroke.moved)
            commitFloating(); // a click on the paste places it
        break;
    case Stroke::MoveCells:
        if (m_stroke.cellOffset == Point{0, 0}) {
            if (m_stroke.fromSelectTool && !m_stroke.moved)
                clickDeselect(); // a click inside the selection without dragging deselects
        } else if (!m_stroke.bounds.isEmpty()) {
            pushPixelEdit(m_stroke.image, m_stroke.bounds, m_stroke.before, m_stroke.label.toStdString(), false);
        }
        break;
    case Stroke::MoveMany: {
        std::vector<std::unique_ptr<Command>> parts;
        for (const auto& [cel, start] : m_stroke.manyCels)
            if (cel->position != start)
                parts.push_back(std::make_unique<MoveCelCommand>(cel, start, cel->position));
        for (const LiveStart& live : m_stroke.manyLive)
            if (!(*live.layer->particles == live.before))
                parts.push_back(std::make_unique<SetParticlesCommand>(*live.layer, live.before, *live.layer->particles, "Move Emitter"));
        if (!parts.empty()) {
            pushCommand(std::make_unique<CommandGroup>("Move", std::move(parts)));
            emit historyChanged();
        }
        break;
    }
    case Stroke::MoveCel:
        if (m_stroke.cel->position != m_stroke.celStart) {
            pushCommand(std::make_unique<MoveCelCommand>(m_stroke.cel, m_stroke.celStart, m_stroke.cel->position));
            emit historyChanged();
        }
        break;
    case Stroke::MoveSelection:
        if (m_stroke.last == m_stroke.start) {
            if (m_stroke.fromSelectTool && !m_stroke.moved)
                clickDeselect();
            break;
        }
        [[fallthrough]];
    case Stroke::Freehand:
        m_lastPainted = Point{m_stroke.last.x + m_stroke.origin.x, m_stroke.last.y + m_stroke.origin.y};
        [[fallthrough]];
    case Stroke::Shape: {
        if (m_stroke.tiles) {
            finishTileStroke();
            break;
        }
        const Rect r = m_stroke.bounds;
        // A shading stroke over pixels outside the ramp, or one over an alpha
        // locked layer's empty pixels, changes nothing.
        const bool mayBeUnchanged = !m_stroke.ramp.empty() || m_stroke.softens
                                    || (m_current && m_current->isAlphaLocked());
        const bool unchanged = mayBeUnchanged && !r.isEmpty() && m_stroke.image->copy(r) == m_stroke.before.copy(r);
        if (!r.isEmpty() && !unchanged)
            pushPixelEdit(m_stroke.image, r, m_stroke.before, m_stroke.label.toStdString(), m_stroke.createdCel);
        else if (m_stroke.createdCel)
            dropCreatedCel();
        break;
    }
    }
    const bool lasso = !m_stroke.lasso.empty();
    if (m_stroke.kind == Stroke::Shape && m_tool == Gradient && m_undo.position() > historyBefore && !m_stroke.tiles) {
        // The gradient stays open: its line and handles show, and it can be moved or changed.
        m_gradientEdit = {true, {m_stroke.start.x + m_stroke.origin.x, m_stroke.start.y + m_stroke.origin.y},
                          {m_stroke.last.x + m_stroke.origin.x, m_stroke.last.y + m_stroke.origin.y},
                          m_undo.position(), m_current, m_currentFrame, m_gradientEdit.redrawing};
        emit gradientLineChanged();
    } else if (!m_gradientLine.isEmpty()) {
        m_gradientLine.clear();
        emit gradientLineChanged();
    }
    m_stroke = {};
    refreshOverlay();
    if (lasso)
        emit lassoPathChanged();
}

void Editor::fill(QPoint pixel, bool secondary)
{
    Point origin;
    bool created = false;
    auto image = editableImage(origin, &created);
    if (!image)
        return;
    const Image before = *image;
    const uint32_t value = encodeColor(secondary ? m_secondary : m_primary);
    const Mask clip = m_doc->selection.translated(-origin.x, -origin.y);
    Rect r;
    // With symmetry the mirrored points fill too (a no-op where the first
    // fill already reached).
    if (m_sampleMerged || m_tileMode) {
        // The region comes from what every visible layer shows (or, in the
        // tiled view, from this cel as it is on the canvas, so an area goes on
        // across the edges); the fill goes into the current cel.
        Image celOnCanvas;
        if (!m_sampleMerged) {
            celOnCanvas = Image(m_doc->width(), m_doc->height(), m_doc->pixelFormat());
            celOnCanvas.clear(transparentValue());
            celOnCanvas.blit(*image, origin);
        }
        const Image& source = m_sampleMerged ? m_composite : celOnCanvas;
        Mask region;
        for (const Point p : mirrored(toPoint(pixel), 1))
            if (m_doc->bounds().contains(p))
                region = region.united(similarMaskWrapped(source, p, m_fillContiguous, m_fillTolerance, tileX(), tileY()));
        if (!m_doc->selection.isEmpty())
            region = region.intersected(m_doc->selection);
        const Rect b = region.bounds();
        for (int y = b.y; y < b.bottom(); ++y)
            for (int x = b.x; x < b.right(); ++x) {
                const int ix = x - origin.x, iy = y - origin.y;
                if (region.contains(x, y) && image->bounds().contains(ix, iy) && image->pixel(ix, iy) != value) {
                    image->setPixel(ix, iy, value);
                    r = r.united({ix, iy, 1, 1});
                }
            }
    } else {
        for (const Point p : mirrored(toPoint(pixel), 1))
            r = r.united(floodFill(*image, {p.x - origin.x, p.y - origin.y}, value, m_fillContiguous, m_fillTolerance,
                                   {}, clip.isEmpty() ? nullptr : &clip));
    }
    if (r.isEmpty()) {
        if (created)
            dropCreatedCel();
        return;
    }
    if (m_dither) {
        // Covered: what the fill changed (a fill region is one color, so exact).
        Image covered(image->width(), image->height(), PixelFormat::Indexed8);
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x)
                if (image->pixel(x, y) != before.pixel(x, y))
                    covered.setPixel(x, y, 1);
        const std::optional<uint32_t> off =
            m_ditherOtherColor ? std::optional(encodeColor(secondary ? m_primary : m_secondary)) : std::nullopt;
        applyDither(*image, before, covered, r, origin, coreDither(), off);
    }
    if (m_current->isAlphaLocked()) {
        keepAlpha(*image, before, r, transparentValue());
        if (image->copy(r) == before.copy(r)) { // only empty pixels were reached
            if (created)
                dropCreatedCel();
            return;
        }
    }
    pushPixelEdit(image, r, before, "Fill", created);
    invalidate(r.translated(origin.x, origin.y));
}

// --- clipboard: pixels -----------------------------------------------------

bool Editor::canPaste() const
{
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    return data && data->hasImage();
}

// Pixels of the current cel only (not the whole composite) in `area`
// (document coordinates), as RGBA; with a selection only the selected ones.
Image Editor::currentCelRgba(Rect area) const
{
    const Mask& sel = m_doc->selection;
    Image rgba(std::max(0, area.w), std::max(0, area.h));
    const CelRef cel = m_current && m_current->hasCels() ? m_current->cel(m_currentFrame) : nullptr;
    if (cel && cel->image && cel->image->format() != PixelFormat::Tilemap32) {
        const Rect celRect{cel->position.x, cel->position.y, cel->image->width(), cel->image->height()};
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x)
                if (celRect.contains(x, y) && (sel.isEmpty() || sel.contains(x, y)))
                    rgba.setPixel(x - area.x, y - area.y,
                                  pack(toRgba(*m_doc, *cel->image, x - celRect.x, y - celRect.y,
                                              m_current->isBackground())));
    }
    return rgba;
}

void Editor::copy()
{
    endStroke();
    commitFloating();
    const Mask& sel = m_doc->selection;
    const Rect area = sel.isEmpty() ? m_doc->bounds() : sel.bounds().intersected(m_doc->bounds());
    if (area.isEmpty())
        return;
    const Image rgba = currentCelRgba(area);
    m_copiedImage = QImage(rgba.data(), rgba.width(), rgba.height(), rgba.stride(), QImage::Format_RGBA8888).copy();
    m_copiedOrigin = {area.x, area.y};
    QGuiApplication::clipboard()->setImage(m_copiedImage);
    emit clipboardChanged();
}

void Editor::cut()
{
    if (m_float.active) {
        // Cut what's floating: onto the clipboard, out of the layer.
        const Image rgba = m_float.rgba;
        const Point at = m_float.position;
        m_copiedImage = QImage(rgba.data(), rgba.width(), rgba.height(), rgba.stride(), QImage::Format_RGBA8888).copy();
        m_copiedOrigin = at;
        QGuiApplication::clipboard()->setImage(m_copiedImage);
        emit clipboardChanged();
        if (m_float.lifted) {
            m_float.rendered = Image(); // land nothing: the lifted pixels stay cut
            m_float.label = "Cut";
            m_float.keepSelection = false;
            commitFloating();
        } else {
            cancelFloating();
        }
        return;
    }
    copy();
    deleteArea(m_doc->selection.isEmpty() ? Mask(m_doc->bounds()) : m_doc->selection, "Cut");
}

QSize Editor::clipboardImageSize() const
{
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull() || image.width() > 4096 || image.height() > 4096)
        return {};
    return image.size();
}

bool Editor::clipboardRgba(Image& out) const
{
    QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull())
        return false;
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.width() > 4096 || image.height() > 4096)
        return false;
    out = Image(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(out.row(y), image.constScanLine(y), size_t(out.stride()));
    return true;
}

void Editor::copyMerged()
{
    endStroke();
    commitFloating();
    const Mask& sel = m_doc->selection;
    const Rect area = sel.isEmpty() ? m_doc->bounds() : sel.bounds().intersected(m_doc->bounds());
    if (area.isEmpty())
        return;
    Image rgba = m_composite.copy(area);
    if (!sel.isEmpty())
        for (int y = 0; y < area.h; ++y)
            for (int x = 0; x < area.w; ++x)
                if (!sel.contains(area.x + x, area.y + y))
                    rgba.setPixel(x, y, 0);
    m_copiedImage = QImage(rgba.data(), rgba.width(), rgba.height(), rgba.stride(), QImage::Format_RGBA8888).copy();
    m_copiedOrigin = {area.x, area.y};
    QGuiApplication::clipboard()->setImage(m_copiedImage);
    emit clipboardChanged();
}

bool Editor::pasteAsNewLayer()
{
    endStroke();
    commitFloating();
    Image rgba;
    if (!clipboardRgba(rgba))
        return false;
    insertLayerAboveCurrent(m_doc->makeImageLayer("Pasted"));
    paste();
    return true;
}

bool Editor::pasteAsNewSprite()
{
    endStroke();
    commitFloating();
    Image rgba;
    if (!clipboardRgba(rgba))
        return false;
    auto doc = std::make_unique<Document>(rgba.width(), rgba.height(), ColorMode::Rgba);
    doc->palette = Palette::defaultPalette();
    auto layer = doc->makeImageLayer("Layer 1");
    auto cel = std::make_shared<CelData>();
    cel->image = std::make_shared<Image>(std::move(rgba));
    layer->cels[0] = cel;
    doc->insertLayer(doc->root(), 0, std::move(layer));
    setDocument(std::move(doc), {}, nextUntitledName());
    return true;
}

void Editor::paste()
{
    endStroke();
    commitFloating();
    Image rgba;
    if (!canDrawOnCurrentLayer() || !clipboardRgba(rgba))
        return;
    // Our own copy goes back where it came from; anything else is centered.
    const QImage image = QGuiApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
    const Point at = image == m_copiedImage ? m_copiedOrigin
                                            : Point{(m_doc->width() - rgba.width()) / 2, (m_doc->height() - rgba.height()) / 2};
    if (m_tool != Move && m_tool != Select)
        setTool(Move);
    startFloat(convertToDocument(*m_doc, rgba), {at.x, at.y, rgba.width(), rgba.height()}, "Paste");
}

// --- floating pixels --------------------------------------------------------

void Editor::startFloat(const Image& source, Rect box, const char* label)
{
    m_float.active = true;
    m_float.source = source;
    m_float.box = box;
    m_float.angle = 0;
    m_float.pivot.reset();
    m_float.label = label;
    renderFloat();
}

// Scales the source into the box, rotates it, and shows it.
void Editor::renderFloat()
{
    Image img = scaledNearest(m_float.source, m_float.box.w, m_float.box.h);
    if (m_float.angle != 0)
        img = rotated(img, m_float.angle, transparentValue());
    // Centered on the box (exactly the box when not rotated).
    const auto floorHalf = [](int v) { return v >= 0 ? v / 2 : (v - 1) / 2; };
    m_float.position = {m_float.box.x + floorHalf(m_float.box.w - img.width()),
                        m_float.box.y + floorHalf(m_float.box.h - img.height())};
    m_float.rgba = Image(img.width(), img.height());
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (const uint32_t v = img.pixel(x, y); !isTransparent(v))
                m_float.rgba.setPixel(x, y, pack(toRgba(*m_doc, img, x, y)));
    m_float.rendered = std::move(img);
    // The selected shape, transformed the same way (0 / 1 values).
    Image mask = m_float.sourceMask;
    if (mask.isNull()) {
        mask = Image(m_float.source.width(), m_float.source.height(), PixelFormat::Indexed8);
        mask.clear(1);
    }
    mask = scaledNearest(mask, m_float.box.w, m_float.box.h);
    if (m_float.angle != 0)
        mask = rotated(mask, m_float.angle, 0);
    m_float.mask = Mask::fromImage(mask, m_float.position);
    setSelection(m_float.mask);
    refreshOverlay();
    if (m_float.multi)
        updateMultiLive();
    emit floatingChanged();
}

QRect Editor::transformBox() const
{
    const Rect& b = m_float.box;
    return m_float.active ? QRect(b.x, b.y, b.w, b.h) : QRect();
}

QSize Editor::transformSourceSize() const
{
    return m_float.active ? QSize(m_float.source.width(), m_float.source.height()) : QSize();
}

void Editor::setTransformBox(const QRect& box)
{
    if (!m_float.active)
        return;
    const Rect b{box.x(), box.y(), std::clamp(box.width(), 1, 4096), std::clamp(box.height(), 1, 4096)};
    if (b == m_float.box)
        return;
    m_float.box = b;
    m_float.pivot.reset(); // moved or sized: the pivot follows the box
    renderFloat();
}

void Editor::setTransformAngle(double degrees)
{
    if (!m_float.active)
        return;
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180)
        degrees -= 360;
    if (degrees <= -180)
        degrees += 360;
    if (std::abs(degrees - m_float.angle) < 1e-9)
        return;
    // The float turns around its center; moving the center turns it around
    // the pivot instead.
    if (!m_float.pivot) {
        const Rect b = boxKeepingPivot(m_float.box.w, m_float.box.h, m_float.angle); // the same box
        const double fx = (m_transformPivot % 3) / 2.0 - 0.5, fy = (m_transformPivot / 3) / 2.0 - 0.5;
        const double r = m_float.angle * M_PI / 180;
        const double ox = fx * b.w, oy = fy * b.h;
        m_float.pivot = std::pair{b.x + b.w / 2.0 + ox * std::cos(r) - oy * std::sin(r),
                                  b.y + b.h / 2.0 + ox * std::sin(r) + oy * std::cos(r)};
    }
    m_float.box = boxKeepingPivot(m_float.box.w, m_float.box.h, degrees);
    m_float.angle = degrees;
    renderFloat();
}

void Editor::setTransformPivot(int pivot)
{
    pivot = std::clamp(pivot, 0, 8);
    if (m_transformPivot == pivot)
        return;
    m_transformPivot = pivot;
    m_float.pivot.reset();
    emit toolOptionsChanged();
}

Rect Editor::boxKeepingPivot(int width, int height, double angle) const
{
    const Rect& b = m_float.box;
    // The pivot as a share of the box, from its center: -0.5, 0 or 0.5 each way.
    const double fx = (m_transformPivot % 3) / 2.0 - 0.5, fy = (m_transformPivot / 3) / 2.0 - 0.5;
    const auto turn = [](double x, double y, double degrees) {
        const double r = degrees * M_PI / 180, c = std::cos(r), s = std::sin(r);
        return std::pair{x * c - y * s, x * s + y * c};
    };
    const auto [ox, oy] = turn(fx * b.w, fy * b.h, m_float.angle);
    // The pivot, in canvas pixels (kept exact while only turning).
    const auto [px, py] = m_float.pivot ? *m_float.pivot : std::pair{b.x + b.w / 2.0 + ox, b.y + b.h / 2.0 + oy};
    const auto [nx, ny] = turn(fx * width, fy * height, angle);
    const double cx = px - nx, cy = py - ny; // the new center
    return {int(std::lround(cx - width / 2.0)), int(std::lround(cy - height / 2.0)), width, height};
}

void Editor::resizeTransform(int width, int height)
{
    if (!m_float.active)
        return;
    width = std::clamp(width, 1, 4096);
    height = std::clamp(height, 1, 4096);
    setTransformBox([&] {
        const Rect b = boxKeepingPivot(width, height, m_float.angle);
        return QRect(b.x, b.y, b.w, b.h);
    }());
}

// Lifts the selected pixels (or the current cel's visible content) out of
// the cel into a float. False if there's nothing to lift.
// Cmd+T (or a flip or turn) on several layers, cels or frames: one transform for all of them. Each cel
// gives up its picture (it goes back if the transform is cancelled), every picture becomes a source the
// size of the union of them all (so one box, angle and pivot fit them all, and they stay as they were
// relative to each other), and the canvas shows the cels of the current frame together. The result lands
// in each cel when it ends. A live layer can only move: its emitter goes where the box's middle goes.
bool Editor::liftMulti(const char* label)
{
    endStroke();
    MoveGroup group;
    gatherMoveTargets(group);
    auto multi = std::make_shared<Float::Multi>();
    for (size_t i = 0; i < group.cels.size(); ++i) {
        Float::Part part;
        part.layer = group.celLayers[i];
        part.cel = group.cels[i].first;
        part.image = part.cel->image;
        part.origin = part.cel->position;
        // The cel's opaque pixels (outside the canvas too).
        const Image& image = *part.image;
        int l = image.width(), t = image.height(), r = -1, b = -1;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (!isTransparent(image.pixel(x, y))) {
                    l = std::min(l, x);
                    r = std::max(r, x);
                    t = std::min(t, y);
                    b = std::max(b, y);
                }
        if (r < l)
            continue; // nothing in it
        part.area = {l, t, r - l + 1, b - t + 1};
        part.before = image;
        multi->unionBox = multi->unionBox.united(part.area.translated(part.origin.x, part.origin.y));
        multi->parts.push_back(std::move(part));
    }
    multi->live = std::move(group.live);
    if (multi->parts.empty() && multi->live.empty())
        return false;
    Rect box = multi->unionBox;
    if (multi->parts.empty()) { // only live layers: a box around their emitters
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const LiveStart& l : multi->live) {
            x0 = std::min(x0, l.before.x);
            y0 = std::min(y0, l.before.y);
            x1 = std::max(x1, l.before.x);
            y1 = std::max(y1, l.before.y);
        }
        box = {int(std::floor(x0)) - 4, int(std::floor(y0)) - 4, std::max(8, int(std::ceil(x1 - x0)) + 8),
               std::max(8, int(std::ceil(y1 - y0)) + 8)};
    }
    multi->unionBox = box;
    for (Float::Part& part : multi->parts) {
        part.source = Image(box.w, box.h, part.image->format());
        part.source.clear(transparentValue());
        const Rect at = part.area.translated(part.origin.x - box.x, part.origin.y - box.y);
        part.source.blit(part.image->copy(part.area), {at.x, at.y});
        part.image->fillRect(part.area, transparentValue()); // lifted
        invalidate(part.area.translated(part.origin.x, part.origin.y));
    }
    if (m_tool != Move && m_tool != Select)
        setTool(Move);

    // What the canvas shows: the parts that are on the current frame, laid over each other.
    Image preview(box.w, box.h, m_doc->pixelFormat());
    preview.clear(transparentValue());
    for (const Float::Part& part : multi->parts) {
        if (part.layer->cel(m_currentFrame) != part.cel)
            continue;
        for (int y = 0; y < preview.height(); ++y)
            for (int x = 0; x < preview.width(); ++x)
                if (const uint32_t v = part.source.pixel(x, y); !isTransparent(v))
                    preview.setPixel(x, y, v);
    }
    m_float = {};
    m_float.multi = multi;
    m_float.lifted = true;
    m_float.keepSelection = false;
    startFloat(preview, box, label);
    return true;
}

// The live layers in a multi transform follow where the middle of the box goes (a live layer only moves).
void Editor::updateMultiLive()
{
    if (!m_float.multi || m_float.multi->live.empty())
        return;
    const Rect& u = m_float.multi->unionBox;
    const Rect& b = m_float.box;
    const double dx = std::round((b.x + b.w / 2.0) - (u.x + u.w / 2.0));
    const double dy = std::round((b.y + b.h / 2.0) - (u.y + u.h / 2.0));
    for (LiveStart& live : m_float.multi->live) {
        if (live.layer->particles->x == live.before.x + dx && live.layer->particles->y == live.before.y + dy)
            continue;
        live.layer->particles->x = live.before.x + dx;
        live.layer->particles->y = live.before.y + dy;
        renderParticleLayer(*m_doc, *live.layer);
    }
    emit celsChanged();
    emit liveChanged();
    invalidateAll();
}

// Ends a multi transform: each part's pixels, scaled and turned like the shared box says, go into its
// cel (what falls outside the cel's picture is dropped, as in a single transform); or, cancelled, back
// as they were. One undo step for all of it.
void Editor::landMulti(const Float& f, bool place)
{
    const std::shared_ptr<Float::Multi>& multi = f.multi;
    if (!place) {
        for (Float::Part& part : multi->parts) {
            part.image->blit(part.before.copy(part.area), {part.area.x, part.area.y});
            invalidate(part.area.translated(part.origin.x, part.origin.y));
        }
        for (LiveStart& live : multi->live) {
            *live.layer->particles = live.before;
            renderParticleLayer(*m_doc, *live.layer);
        }
        if (!multi->live.empty()) {
            emit celsChanged();
            emit liveChanged();
        }
        setSelection(Mask{});
        return;
    }
    const auto floorHalf = [](int v) { return v >= 0 ? v / 2 : (v - 1) / 2; };
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<ReplaceCelsCommand::Change> grownCels;
    for (Float::Part& part : multi->parts) {
        // Where this part's pixels land, and the rectangle they cover (document coordinates).
        Image img;
        Point at;
        Rect placed;
        if (!f.rendered.isNull()) { // (deleted: nothing goes back)
            img = scaledNearest(part.source, f.box.w, f.box.h);
            if (f.angle != 0)
                img = rotated(img, f.angle, transparentValue());
            at = {f.box.x + floorHalf(f.box.w - img.width()), f.box.y + floorHalf(f.box.h - img.height())};
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (!isTransparent(img.pixel(x, y)))
                        placed = placed.united({at.x + x, at.y + y, 1, 1});
        }
        const Rect oldRect{part.origin.x, part.origin.y, part.image->width(), part.image->height()};
        const Rect newRect = placed.isEmpty() ? oldRect : oldRect.united(placed);

        if (newRect != oldRect) {
            // The pixels reach past the cel's picture (a cel is cropped to what it holds): it grows to
            // hold them, as a new picture, so the old one comes back whole on undo.
            part.image->blit(part.before.copy(part.area), {part.area.x, part.area.y}); // as it was
            auto grown = std::make_shared<Image>(newRect.w, newRect.h, part.image->format());
            grown->clear(transparentValue());
            grown->blit(part.before, {oldRect.x - newRect.x, oldRect.y - newRect.y});
            grown->fillRect(part.area.translated(oldRect.x - newRect.x, oldRect.y - newRect.y), transparentValue());
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (const uint32_t v = img.pixel(x, y); !isTransparent(v))
                        grown->setPixel(at.x - newRect.x + x, at.y - newRect.y + y, v);
            grownCels.push_back({part.cel, part.image, part.origin, grown, {newRect.x, newRect.y}});
            part.cel->image = grown;
            part.cel->position = {newRect.x, newRect.y};
            invalidate(newRect.united(oldRect));
            continue;
        }

        Rect changed = part.area;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const uint32_t v = img.pixel(x, y);
                const int tx = at.x - part.origin.x + x, ty = at.y - part.origin.y + y;
                if (!part.image->bounds().contains(tx, ty) || isTransparent(v))
                    continue;
                part.image->setPixel(tx, ty, v);
                changed = changed.united({tx, ty, 1, 1});
            }
        changed = changed.intersected(part.image->bounds());
        invalidate(changed.translated(part.origin.x, part.origin.y));
        if (changed.isEmpty() || part.image->copy(changed) == part.before.copy(changed))
            continue;
        commands.push_back(std::make_unique<ImagePatchCommand>(part.image, Point{changed.x, changed.y},
                                                               part.before.copy(changed), part.image->copy(changed),
                                                               f.label));
    }
    if (!grownCels.empty())
        commands.push_back(std::make_unique<ReplaceCelsCommand>(std::move(grownCels), f.label));
    for (const LiveStart& live : multi->live)
        if (!(*live.layer->particles == live.before))
            commands.push_back(std::make_unique<SetParticlesCommand>(*live.layer, live.before, *live.layer->particles,
                                                                     "Move Emitter"));
    setSelection(Mask{});
    if (commands.empty()) {
        recordSelection(f.label.c_str());
        return;
    }
    pushCommand(std::make_unique<CommandGroup>(f.label, std::move(commands)));
    emit historyChanged();
}

bool Editor::liftForTransform(const char* label, bool useSelection)
{
    endStroke();
    Point origin;
    auto image = editableImage(origin, nullptr, false);
    if (!image)
        return false;
    const bool hadSelection = useSelection && !m_doc->selection.isEmpty();
    Rect area;
    if (hadSelection) {
        area = m_doc->selection.bounds()
                   .intersected(m_doc->bounds())
                   .translated(-origin.x, -origin.y)
                   .intersected(image->bounds());
    } else {
        // The cel's opaque pixels, outside the canvas too (a big import).
        const Rect all = image->bounds();
        int l = all.right(), t = all.bottom(), r = all.x - 1, b = all.y - 1;
        for (int y = all.y; y < all.bottom(); ++y)
            for (int x = all.x; x < all.right(); ++x)
                if (!isTransparent(image->pixel(x, y))) {
                    l = std::min(l, x);
                    r = std::max(r, x);
                    t = std::min(t, y);
                    b = std::max(b, y);
                }
        if (r >= l)
            area = {l, t, r - l + 1, b - t + 1};
    }
    if (area.isEmpty())
        return false;

    m_float = {};
    m_float.lifted = true;
    m_float.liftImage = image;
    m_float.liftBefore = *image;
    m_float.liftOrigin = origin;
    m_float.liftArea = area;
    Image source = image->copy(area);
    Image sourceMask;
    if (hadSelection) { // only the selected pixels lift; the shape goes along
        sourceMask = Image(area.w, area.h, PixelFormat::Indexed8);
        for (int y = 0; y < area.h; ++y)
            for (int x = 0; x < area.w; ++x) {
                const int ix = area.x + x, iy = area.y + y;
                if (m_doc->selection.contains(ix + origin.x, iy + origin.y)) {
                    sourceMask.setPixel(x, y, 1);
                    image->setPixel(ix, iy, transparentValue());
                } else {
                    source.setPixel(x, y, transparentValue());
                }
            }
    } else {
        image->fillRect(area, transparentValue());
    }
    invalidate(area.translated(origin.x, origin.y));
    if (m_tool != Move && m_tool != Select)
        setTool(Move);
    m_float.sourceMask = std::move(sourceMask);
    startFloat(source, area.translated(origin.x, origin.y), label);
    m_float.keepSelection = hadSelection;
    return true;
}

bool Editor::beginEffect()
{
    endStroke();
    commitFloating();
    cancelEffect();
    m_quantizePalette = {}; // the sprite may have changed since
    m_quantizeColors = 0;
    Point origin;
    auto image = editableImage(origin, nullptr, false);
    if (!image)
        return false;
    m_effect = {};
    m_effect.active = true;
    m_effect.image = image;
    m_effect.before = image->copy(image->bounds());
    m_effect.origin = origin;
    emit effectChanged();
    return true;
}

static std::vector<CurvePoint> curvePoints(const QVariantList& flat)
{
    std::vector<CurvePoint> points;
    for (qsizetype i = 0; i + 1 < flat.size(); i += 2)
        points.push_back({flat[i].toInt(), flat[i + 1].toInt()});
    return points;
}

QVariantList Editor::curveTable(const QVariantList& flatPoints) const
{
    QVariantList out;
    for (const uint8_t v : core::curveTable(curvePoints(flatPoints)))
        out.append(int(v));
    return out;
}

Image Editor::effectResult(const QVariantMap& options, const Image& src, Point origin, bool alphaLock) const
{
    const Mask* mask = m_doc->selection.isEmpty() ? nullptr : &m_doc->selection;
    const QString kind = options.value("kind").toString();
    Image out;
    if (kind == "outline") {
        const OutlineOptions o{toRgba(options.value("color").value<QColor>()), options.value("inside").toBool(),
                               options.value("square").toBool()};
        out = outline(*m_doc, src, o, mask, origin);
    } else if (kind == "replace") {
        out = replaceColor(*m_doc, src, toRgba(options.value("from").value<QColor>()),
                           toRgba(options.value("to").value<QColor>()),
                           std::clamp(options.value("tolerance").toInt(), 0, 255), mask, origin);
    } else if (kind == "adjust") {
        out = adjustHsl(*m_doc, src, options.value("hue").toInt(), options.value("saturation").toInt(),
                        options.value("lightness").toInt(), mask, origin);
    } else if (kind == "convolve") {
        std::vector<int> kernel;
        for (const QVariant& v : options.value("kernel").toList())
            kernel.push_back(v.toInt());
        out = convolve(*m_doc, src, kernel, options.value("size").toInt(), options.value("divisor").toInt(),
                       options.value("bias").toInt(), mask, origin);
    } else if (kind == "cleanup") {
        const CleanupOptions o{options.value("orphans").toBool(), options.value("pinholes").toBool(),
                               options.value("strays").toBool(), options.value("doubles").toBool()};
        out = cleanup(*m_doc, src, o, mask, origin);
    } else if (kind == "curves") {
        const auto table = [&](const char* key) { return core::curveTable(curvePoints(options.value(key).toList())); };
        out = applyCurves(*m_doc, src, table("master"), table("red"), table("green"), table("blue"), mask, origin);
    } else if (kind == "quantize") {
        const int count = std::clamp(options.value("colors").toInt(), 2, 255);
        if (m_quantizePalette.entries.empty() || m_quantizeColors != count) {
            m_quantizePalette = paletteFromSprite(*m_doc, count + 1); // and the transparent entry
            m_quantizeColors = count;
        }
        out = quantize(*m_doc, src, m_quantizePalette, ditherFor(options.value("dither").toString()), mask, origin,
                       ditherOptionsFrom(options));
    } else if (kind == "posterize") {
        out = posterize(*m_doc, src, options.value("levels").toInt(), mask, origin);
    } else if (kind == "shadow") {
        out = dropShadow(*m_doc, src, toRgba(options.value("color").value<QColor>()), options.value("dx").toInt(),
                         options.value("dy").toInt(), mask, origin);
    } else if (kind == "blur") {
        out = core::blur(*m_doc, src, options.value("radius").toInt(), options.value("gaussian").toBool(),
                         options.value("keepShape").toBool(), mask, origin);
    } else {
        out = src.copy(src.bounds());
    }
    if (alphaLock)
        keepAlpha(out, src, out.bounds(), transparentValue());
    return out;
}

void Editor::previewEffect(const QVariantMap& options)
{
    if (!m_effect.active)
        return;
    m_effect.options = options;
    m_effect.image->blit(effectResult(options, m_effect.before, m_effect.origin, m_current->isAlphaLocked()), {0, 0});
    invalidate(m_effect.image->bounds().translated(m_effect.origin.x, m_effect.origin.y));
    for (const auto& t : m_effect.targets) {
        t.image->blit(effectResult(options, t.before, t.origin, t.alphaLock), {0, 0});
        invalidate(t.image->bounds().translated(t.origin.x, t.origin.y));
    }
}

void Editor::setEffectScope(const QString& scope)
{
    if (!m_effect.active || scope == m_effect.scope)
        return;
    // Put back the cels previewed so far, then take the new scope's.
    for (const auto& t : m_effect.targets) {
        t.image->blit(t.before, {0, 0});
        invalidate(t.image->bounds().translated(t.origin.x, t.origin.y));
    }
    m_effect.targets.clear();
    m_effect.scope = scope;
    for (const EffectCel& cel : effectCels(scope, m_effect.image.get(), true))
        m_effect.targets.push_back({cel.image, cel.image->copy(cel.image->bounds()), cel.origin, cel.alphaLock});
    if (!m_effect.options.isEmpty())
        previewEffect(m_effect.options);
}

QColor Editor::effectColorAt(QPoint pixel) const
{
    if (!m_effect.active)
        return {};
    const int x = pixel.x() - m_effect.origin.x, y = pixel.y() - m_effect.origin.y;
    if (!m_effect.before.bounds().contains(x, y))
        return QColor(0, 0, 0, 0);
    const Rgba c = core::toRgba(*m_doc, m_effect.before, x, y);
    return QColor(c.r, c.g, c.b, c.a);
}

std::vector<Editor::EffectCel> Editor::effectCels(const QString& scope, const Image* except,
                                                  bool onlyCurrentFrame) const
{
    std::vector<EffectCel> cels;
    const auto add = [&](const Layer* layer, int frame) {
        if (!layer || layer->kind != LayerKind::Image || !effectivelyVisible(*layer) || effectivelyLocked(*layer))
            return;
        if (onlyCurrentFrame && frame != m_currentFrame)
            return;
        const CelRef cel = layer->cel(frame);
        if (!cel || !cel->image || cel->image.get() == except
            || std::any_of(cels.begin(), cels.end(), [&](const EffectCel& c) { return c.image == cel->image; }))
            return;
        cels.push_back({cel->image, cel->position, layer->isAlphaLocked()});
    };
    const auto allLayers = [&](int frame) {
        m_doc->forEachLayer([&](const Layer& layer) { add(&layer, frame); });
    };
    const int frames = m_doc->frameCount();
    if (scope == "range" && m_rangeType == FrameRange) {
        for (int f = m_range.x; f < m_range.right(); ++f)
            allLayers(f);
    } else if (scope == "range" && m_rangeType == CelRange) {
        for (int row = m_range.y; row < m_range.bottom() && row < int(m_layerList.size()); ++row)
            for (int f = m_range.x; f < m_range.right(); ++f)
                add(m_layerList[size_t(row)], f);
    } else if (scope == "tag") {
        for (const Tag& tag : m_doc->tags)
            if (m_currentFrame >= tag.from && m_currentFrame <= tag.to) {
                for (int f = tag.from; f <= tag.to && f < frames; ++f)
                    add(m_current, f);
                break;
            }
    } else if (scope == "layer") {
        for (int f = 0; f < frames; ++f)
            add(m_current, f);
    } else if (scope == "sprite") {
        for (int f = 0; f < frames; ++f)
            allLayers(f);
    }
    return cels;
}

void Editor::applyEffect(const QString& label, const QString& scope)
{
    if (!m_effect.active)
        return;
    Effect e = std::move(m_effect);
    m_effect = {};
    emit effectChanged();

    // The cels besides the current one; those already previewed hold their
    // result, so they start from the copy taken before it.
    const std::vector<EffectCel> cels = effectCels(scope, e.image.get(), false);

    // Only the pixels that changed go into the undo step.
    const auto changedRect = [](const Image& a, const Image& b) {
        Rect changed;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x)
                if (a.pixel(x, y) != b.pixel(x, y))
                    changed = changed.united({x, y, 1, 1});
        return changed;
    };
    const std::string name = label.toStdString();
    std::vector<std::unique_ptr<Command>> parts;
    if (const Rect r = changedRect(*e.image, e.before); !r.isEmpty())
        parts.push_back(std::make_unique<ImagePatchCommand>(e.image, Point{r.x, r.y}, e.before.copy(r),
                                                            e.image->copy(r), name));
    for (const EffectCel& cel : cels) {
        const auto& image = cel.image;
        const auto previewed = std::find_if(e.targets.begin(), e.targets.end(),
                                            [&](const Effect::Target& t) { return t.image == image; });
        const Image before = previewed != e.targets.end() ? previewed->before : image->copy(image->bounds());
        const Image after = effectResult(e.options, before, cel.origin, cel.alphaLock);
        if (const Rect r = changedRect(after, before); !r.isEmpty()) {
            image->blit(after, {0, 0});
            parts.push_back(std::make_unique<ImagePatchCommand>(image, Point{r.x, r.y}, before.copy(r),
                                                                after.copy(r), name));
        }
    }
    if (parts.empty())
        return;
    if (parts.size() == 1)
        pushCommand(std::move(parts.front()));
    else
        pushCommand(std::make_unique<CommandGroup>(name, std::move(parts)));
    emit historyChanged();
    invalidateAll();
}

void Editor::cancelEffect()
{
    cancelColorModePreview();
    if (!m_effect.active)
        return;
    Effect e = std::move(m_effect);
    m_effect = {};
    e.image->blit(e.before, {0, 0});
    invalidate(e.image->bounds().translated(e.origin.x, e.origin.y));
    for (const auto& t : e.targets) {
        t.image->blit(t.before, {0, 0});
        invalidate(t.image->bounds().translated(t.origin.x, t.origin.y));
    }
    emit effectChanged();
}

void Editor::startTransform()
{
    if (m_float.active)
        return;
    commitFloating();
    if (multiSelectionActive()) {
        liftMulti("Transform");
        return;
    }
    liftForTransform("Transform");
}

void Editor::flipHorizontal()
{
    if (!m_float.active && multiSelectionActive()) {
        flipMany(true);
        return;
    }
    const bool once = !m_float.active && liftForTransform("Flip Horizontal");
    if (!m_float.active)
        return;
    m_float.source = flippedHorizontally(m_float.source);
    if (!m_float.sourceMask.isNull())
        m_float.sourceMask = flippedHorizontally(m_float.sourceMask);
    if (m_float.multi)
        for (auto& part : m_float.multi->parts)
            part.source = flippedHorizontally(part.source);
    renderFloat();
    if (once)
        commitFloating();
}

void Editor::flipVertical()
{
    if (!m_float.active && multiSelectionActive()) {
        flipMany(false);
        return;
    }
    const bool once = !m_float.active && liftForTransform("Flip Vertical");
    if (!m_float.active)
        return;
    m_float.source = flippedVertically(m_float.source);
    if (!m_float.sourceMask.isNull())
        m_float.sourceMask = flippedVertically(m_float.sourceMask);
    if (m_float.multi)
        for (auto& part : m_float.multi->parts)
            part.source = flippedVertically(part.source);
    renderFloat();
    if (once)
        commitFloating();
}

void Editor::rotate90(bool clockwise)
{
    const bool once = !m_float.active
        && (multiSelectionActive() ? liftMulti(clockwise ? "Rotate 90° CW" : "Rotate 90° CCW")
                                   : liftForTransform(clockwise ? "Rotate 90° CW" : "Rotate 90° CCW"));
    if (!m_float.active)
        return;
    m_float.source = rotated90(m_float.source, clockwise);
    if (!m_float.sourceMask.isNull())
        m_float.sourceMask = rotated90(m_float.sourceMask, clockwise);
    if (m_float.multi)
        for (auto& part : m_float.multi->parts)
            part.source = rotated90(part.source, clockwise);
    // The box turns around its center.
    Rect& b = m_float.box;
    b = {b.x + (b.w - b.h) / 2, b.y + (b.h - b.w) / 2, b.h, b.w};
    m_float.pivot.reset();
    renderFloat();
    if (once)
        commitFloating();
}

void Editor::commitFloating() { landFloat(true); }

void Editor::cancelFloating()
{
    if (!m_float.active)
        return;
    landFloat(false);
    setSelection(m_recordedSelection); // as before the paste / transform
}

// Ends the float: `place` puts the rendered pixels into the layer as one undo
// step; otherwise a paste is dropped and lifted pixels go back.
void Editor::landFloat(bool place)
{
    // The text is done once its float lands or goes (not while retyping).
    const bool textDone = m_text.active && !m_retypingText;
    const QString placedText = m_text.text;
    if (textDone) {
        m_text = {};
        emit textChanged();
    }
    if (!m_float.active)
        return;
    Float f = std::move(m_float);
    m_float = {};
    emit floatingChanged();
    refreshOverlay();

    if (f.multi) {
        landMulti(f, place);
        return;
    }
    if (f.lifted && !place) {
        f.liftImage->blit(f.liftBefore.copy(f.liftArea), {f.liftArea.x, f.liftArea.y});
        invalidate(f.liftArea.translated(f.liftOrigin.x, f.liftOrigin.y));
        return;
    }
    if (!place)
        return;

    Point origin;
    bool created = false;
    std::shared_ptr<Image> image;
    if (f.lifted) {
        image = f.liftImage;
        origin = f.liftOrigin;
    } else {
        image = editableImage(origin, &created);
        if (!image)
            return; // the layer can't take pixels any more: the paste is dropped
    }
    const Image before = f.lifted ? f.liftBefore : *image;
    Rect changed = f.lifted ? f.liftArea : Rect{};
    for (int y = 0; y < f.rendered.height(); ++y)
        for (int x = 0; x < f.rendered.width(); ++x) {
            int tx = f.position.x - origin.x + x, ty = f.position.y - origin.y + y;
            if (m_tileMode) { // past the edge of a tiled axis: in at the other side
                const QPoint w = wrapPixel(QPoint(tx + origin.x, ty + origin.y));
                tx = w.x() - origin.x;
                ty = w.y() - origin.y;
            }
            const uint32_t v = f.rendered.pixel(x, y);
            if (!image->bounds().contains(tx, ty) || isTransparent(v))
                continue;
            image->setPixel(tx, ty, v);
            changed = changed.united({tx, ty, 1, 1});
        }
    changed = changed.intersected(image->bounds());
    // Nothing actually changed (e.g. a transform put back as it was).
    if (changed.isEmpty() || image->copy(changed) == before.copy(changed)) {
        if (created)
            dropCreatedCel();
        invalidate(changed.translated(origin.x, origin.y));
        setSelection(f.keepSelection ? f.mask.intersected(m_doc->bounds()) : Mask{});
        recordSelection(f.label.c_str());
        return;
    }
    setSelection(f.keepSelection ? f.mask.intersected(m_doc->bounds()) : Mask{});
    pushPixelEdit(image, changed, before, f.label, created);
    invalidate(changed.translated(origin.x, origin.y));
    if (textDone && place && !placedText.isEmpty())
        m_placedText = {true, placedText, {f.position.x, f.position.y},
                        {f.position.x, f.position.y, f.rendered.width(), f.rendered.height()}, m_undo.position(),
                        m_doc.get(), m_currentFrame, m_current};
}

// --- clipboard: timeline ----------------------------------------------------

void Editor::copyTimeline()
{
    endStroke();
    commitFloating();
    TimelineClip clip;
    clip.mode = m_doc->colorMode();
    std::unordered_map<const CelData*, CelRef> clones; // keep links inside the copy
    auto clone = [&](const CelRef& cel) -> CelRef {
        if (!cel)
            return nullptr;
        auto& c = clones[cel.get()];
        if (!c)
            c = cloneCel(cel);
        return c;
    };
    const auto [f0, f1] = frameSpan();
    if (m_rangeType == FrameRange) {
        clip.frames = true;
        for (int f = f0; f <= f1; ++f) {
            clip.durations.push_back(m_doc->frame(f).durationMs);
            auto cels = m_doc->celsAt(f);
            for (auto& cel : cels)
                cel = clone(cel);
            clip.rows.push_back(std::move(cels));
        }
    } else {
        const auto [l0, l1] = layerSpan();
        for (int l = l1; l >= l0; --l) { // top row first
            const Layer* layer = m_layerList[size_t(l)];
            std::vector<CelRef> row;
            for (int f = f0; f <= f1; ++f)
                row.push_back(layer->hasCels() ? clone(layer->cel(f)) : nullptr);
            clip.rows.push_back(std::move(row));
        }
    }
    m_timelineClip = std::move(clip);
    emit clipboardChanged();
}

void Editor::cutTimeline()
{
    copyTimeline();
    if (m_timelineClip.frames)
        removeFrame();
    else
        clearCels();
}

void Editor::pasteTimeline()
{
    endStroke();
    commitFloating();
    if (!canPasteTimeline())
        return;
    // Fresh copies each time, links inside the clip kept.
    std::unordered_map<const CelData*, CelRef> clones;
    auto clone = [&](const CelRef& cel) -> CelRef {
        if (!cel)
            return nullptr;
        auto& c = clones[cel.get()];
        if (!c)
            c = cloneCel(cel);
        return c;
    };
    std::vector<std::unique_ptr<Command>> parts;

    if (m_timelineClip.frames) {
        const int at = frameSpan().second + 1;
        const int count = int(m_timelineClip.rows.size());
        for (int i = 0; i < count; ++i) {
            std::vector<CelRef> cels;
            for (const CelRef& cel : m_timelineClip.rows[size_t(i)])
                cels.push_back(clone(cel));
            auto insert = std::make_unique<InsertFrameCommand>(at + i, Frame{m_timelineClip.durations[size_t(i)]},
                                                               std::move(cels));
            insert->redo(*m_doc);
            parts.push_back(std::move(insert));
        }
        pushCommand(std::make_unique<CommandGroup>("Paste Frames", std::move(parts)));
        emit historyChanged();
        m_currentFrame = at;
        structureChanged();
        selectFrames(at, at + count - 1);
        return;
    }

    // A block of cels: top-left on the current cel; what falls off the grid
    // or onto a group is skipped.
    const int top = currentLayer();
    const int rows = int(m_timelineClip.rows.size());
    const int columns = int(m_timelineClip.rows.front().size());
    for (int r = 0; r < rows; ++r) {
        const int l = top - r;
        if (l < 0)
            break;
        Layer* layer = m_layerList[size_t(l)];
        if (!layer->hasCels() || layer->isLive())
            continue;
        for (int c = 0; c < columns; ++c) {
            const int f = m_currentFrame + c;
            if (f >= m_doc->frameCount())
                break;
            const CelRef cel = clone(m_timelineClip.rows[size_t(r)][size_t(c)]);
            if (layer->cel(f) == cel)
                continue;
            auto set = std::make_unique<SetCelCommand>(*layer, f, layer->cel(f), cel);
            set->redo(*m_doc);
            parts.push_back(std::move(set));
        }
    }
    if (parts.empty())
        return;
    pushCommand(std::make_unique<CommandGroup>("Paste Cels", std::move(parts)));
    emit historyChanged();
    const int bottom = std::max(0, top - rows + 1);
    const int last = std::min(m_doc->frameCount() - 1, m_currentFrame + columns - 1);
    structureChanged();
    selectCels(bottom, m_currentFrame, top, last);
}

// --- selection --------------------------------------------------------------

QRect Editor::selection() const
{
    const Rect b = m_doc->selection.bounds();
    return {b.x, b.y, b.w, b.h};
}

void Editor::setSelection(Mask mask)
{
    if (mask == m_doc->selection)
        return;
    const Rect old = m_doc->selection.bounds();
    m_doc->selection = std::move(mask);
    selectionUpdated(old);
}

// After the selection changed (here or through undo): redraws the image the
// ants come from where it may differ.
void Editor::selectionUpdated(Rect oldBounds)
{
    const Rect area = oldBounds.united(m_doc->selection.bounds()).intersected(m_doc->bounds());
    drawSelectionImage(area);
    emit selectionChanged();
}

void Editor::drawSelectionImage(Rect area)
{
    area = area.intersected(m_selectionImage.bounds());
    for (int y = area.y; y < area.bottom(); ++y)
        for (int x = area.x; x < area.right(); ++x)
            m_selectionImage.setPixel(x, y, m_doc->selection.contains(x, y) ? 0xFFFFFFFFu : 0u);
    m_selectionDirty = m_selectionDirty.united(area);
}

QImage Editor::selectionImage() const
{
    return QImage(m_selectionImage.data(), m_selectionImage.width(), m_selectionImage.height(),
                  m_selectionImage.stride(), QImage::Format_RGBA8888);
}

QRect Editor::takeSelectionDirty()
{
    const Rect r = std::exchange(m_selectionDirty, Rect{});
    return {r.x, r.y, r.w, r.h};
}

bool Editor::nudge(int dx, int dy, bool outlineOnly)
{
    if (m_stroke.active || (dx == 0 && dy == 0))
        return false;
    // A live layer has no pixels of its own to move: the arrows move its emitter (a run of them
    // is one undo step); with a transform going on, they move that instead.
    if (liveLayer() && !m_float.active) {
        const QPointF at = liveEmitter();
        setLiveEmitter(QPointF(at.x() + dx, at.y() + dy));
        return true;
    }
    // Tile mode: the arrows step a cell at a time (Shift: ten): the selected cells move (with Alt,
    // only the selection's outline does).
    if (tilesActive() && !m_float.active) {
        const Tileset* set = currentTileset();
        if (m_doc->selection.isEmpty() || !set)
            return false;
        if (!outlineOnly)
            return nudgeCells(dx, dy);
        setSelection(m_doc->selection.translated(dx * set->tileWidth, dy * set->tileHeight));
        recordSelection("Move Selection", continuing(QStringLiteral("nudge")));
        return true;
    }
    if (outlineOnly) {
        if (m_doc->selection.isEmpty())
            return false;
        commitFloating();
        setSelection(m_doc->selection.translated(dx, dy));
        recordSelection("Move Selection", continuing(QStringLiteral("nudge")));
        return true;
    }
    // The pixels float (as when transforming) until Enter or the next edit,
    // so a run of nudges lands as one step.
    if (!m_float.active && (m_doc->selection.isEmpty() || !liftForTransform("Move")))
        return false;
    const Rect b = m_float.box.translated(dx, dy);
    setTransformBox(QRect(b.x, b.y, b.w, b.h));
    return true;
}

void Editor::selectAll()
{
    commitFloating();
    setSelection(Mask(m_doc->bounds()));
    recordSelection("Select All");
}

void Editor::invertSelection()
{
    commitFloating();
    setSelection(m_doc->selection.inverted(m_doc->bounds()));
    recordSelection("Invert Selection");
}

void Editor::selectLayerPixels(int index)
{
    Layer* layer = layerAt(index);
    if (!layer)
        return;
    commitFloating();
    Image bits(m_doc->width(), m_doc->height(), PixelFormat::Indexed8);
    const std::function<void(const Layer&)> add = [&](const Layer& l) {
        for (const auto& child : l.children)
            add(*child);
        const CelRef cel = l.kind == LayerKind::Image ? l.cel(m_currentFrame) : nullptr;
        if (!cel || !cel->image)
            return;
        const Image& image = *cel->image;
        const Rect area = image.bounds().translated(cel->position.x, cel->position.y).intersected(m_doc->bounds());
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x)
                if (!isTransparent(image.pixel(x - cel->position.x, y - cel->position.y)))
                    bits.setPixel(x, y, 1);
    };
    add(*layer);
    setSelection(Mask::fromImage(bits, {0, 0}));
    recordSelection("Select Layer Pixels");
}

// A select-tool click that deselects; a double-click's grid cell selection
// right after it replaces its undo step (selectGridCell).
void Editor::clickDeselect()
{
    const bool had = !m_doc->selection.isEmpty();
    deselect();
    if (had)
        continuing(QStringLiteral("click-deselect"));
}

bool Editor::selectGridCell(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    const bool afterDeselect = m_mergeKey == QLatin1String("click-deselect") && m_mergeTimer.isValid()
                            && m_mergeTimer.elapsed() < 1000;
    m_mergeKey.clear();
    if (m_stroke.active && (m_stroke.kind == Stroke::Select || m_stroke.kind == Stroke::MoveSelection) && !m_stroke.moved)
        m_stroke = {}; // the double-click's second press: not a click or move of its own
    commitFloating();
    const QRect g = grid();
    const int cx = int(std::floor(double(pixel.x() - g.x()) / g.width()));
    const int cy = int(std::floor(double(pixel.y() - g.y()) / g.height()));
    const Rect cell = Rect{g.x() + cx * g.width(), g.y() + cy * g.height(), g.width(), g.height()}.intersected(m_doc->bounds());
    if (cell.isEmpty())
        return false;
    if (afterDeselect && m_undo.canUndo()) {
        // Take the first click's Deselect back off the stack, so undoing the
        // cell goes straight back to the selection before the double-click.
        m_undo.undo(*m_doc);
        m_recordedSelection = m_doc->selection;
    }
    const Stroke::SelectMode mode = selectModeFor(modifiers);
    setSelection(combinedSelection(m_doc->selection, Mask(cell), mode));
    recordSelection(selectLabel(mode));
    return true;
}

void Editor::deselect()
{
    commitFloating(); // deselecting places a paste, as in Aseprite
    setSelection(Mask{});
    recordSelection("Deselect");
}

void Editor::deleteSelection()
{
    if (m_float.active) {
        if (m_float.lifted) { // the lifted pixels stay deleted
            m_float.rendered = Image();
            m_float.label = "Delete";
            m_float.keepSelection = false;
            commitFloating();
        } else {
            cancelFloating(); // a paste is dropped
        }
        return;
    }
    if (m_current && m_current->isSmear()) { // a Smear group has no pixels: the frame's key goes
        removeSmearKey(m_currentFrame);
        return;
    }
    if (!m_doc->selection.isEmpty()) {
        deleteArea(m_doc->selection, "Delete");
        return;
    }
    // What the timeline has selected: frames go, layers go, a block of cels is cleared.
    if (m_rangeType == FrameRange) {
        removeFrame();
        return;
    }
    if (picksActive() || (m_rangeType == CelRange && m_layerBlock && m_range.h > 1)) {
        removeLayer();
        return;
    }
    if (m_rangeType == CelRange && (m_range.w > 1 || m_range.h > 1)) {
        clearCels();
        return;
    }
    // Nothing selected: the whole cel, also what lies past the canvas.
    if (!m_current || !m_current->hasCels())
        return;
    const CelRef cel = m_current->cel(m_currentFrame);
    if (cel && cel->image)
        deleteArea(Mask(Rect{cel->position.x, cel->position.y, cel->image->width(), cel->image->height()}), "Clear");
}

// Clears the pixels of `area` (canvas pixels) in the current cel as one undo step.
void Editor::deleteArea(const Mask& area, const char* label)
{
    if (m_stroke.active)
        return;
    Point origin;
    auto image = editableImage(origin, nullptr, false);
    if (!image)
        return;
    const Rect r = area.bounds().translated(-origin.x, -origin.y).intersected(image->bounds());
    if (r.isEmpty())
        return;
    const Image before = image->copy(r);
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x)
            if (area.contains(x + origin.x, y + origin.y))
                image->setPixel(x, y, transparentValue());
    pushCommand(std::make_unique<ImagePatchCommand>(image, Point{r.x, r.y}, before, image->copy(r), label));
    emit historyChanged();
    invalidate(r.translated(origin.x, origin.y));
}

void Editor::pickColor(QPoint pixel, bool secondary)
{
    if (!m_composite.bounds().contains(toPoint(pixel)))
        return;
    const QColor c = toQColor(unpack(m_composite.pixel(pixel.x(), pixel.y())));
    if (secondary)
        setSecondaryColor(c);
    else
        setPrimaryColor(c);
}

// --- composite --------------------------------------------------------------

void Editor::clearFrameCache()
{
    m_frameCache.clear();
    m_frameCacheBytes = 0;
    m_previewCache.clear();
    m_previewCacheBytes = 0;
}

void Editor::showPlaybackFrame()
{
    const Rect all = m_doc->bounds();
    if (syncSmearAsDrawn() || m_frameCache.size() != size_t(m_doc->frameCount())
        || m_composite.width() != m_doc->width() || m_composite.height() != m_doc->height()) {
        clearFrameCache();
        m_frameCache.resize(size_t(m_doc->frameCount()));
    }
    auto& cached = m_frameCache[size_t(m_currentFrame)];
    if (cached) {
        m_composite.blit(*cached, {0, 0});
    } else {
        renderFrame(*m_doc, m_currentFrame, m_composite, all, true);
        const size_t bytes = size_t(m_composite.width()) * size_t(m_composite.height()) * size_t(m_composite.bpp());
        if (m_frameCacheBytes + bytes <= kFrameCacheLimit) {
            cached = m_composite;
            m_frameCacheBytes += bytes;
        }
    }
    m_dirty = m_dirty.united(all);
    renderOnion(all);
    emit canvasChanged();
}

void Editor::invalidate(Rect rect)
{
    clearFrameCache();
    // A posed Smear group draws its art somewhere else than where it is drawn, and its smear across
    // the canvas: while there is one, everything is drawn again.
    if (syncSmearAsDrawn() || anyPosedSmear())
        rect = m_doc->bounds();
    rect = rect.intersected(m_doc->bounds());
    if (rect.isEmpty())
        return;
    renderFrame(*m_doc, m_currentFrame, m_composite, rect, true);
    m_dirty = m_dirty.united(rect);
    renderOnion(rect);
    emit canvasChanged();
}

// Keeps the onion skin in step with the composite: any pixel change may be
// on a cel linked into a neighbouring frame.
void Editor::renderOnion(Rect area)
{
    if (m_onion.isNull())
        return;
    if (m_onionEnabled && !m_playing && m_doc->frameCount() > 1) {
        OnionSkin options = m_onionOptions;
        if (m_onionLoop) // inside a tag, the frames wrap around within it
            for (const Tag& tag : m_doc->tags)
                if (m_currentFrame >= tag.from && m_currentFrame <= tag.to) {
                    options.loopFrom = tag.from;
                    options.loopTo = tag.to;
                    break;
                }
        renderOnionSkin(*m_doc, m_currentFrame, options, m_onion, area, m_scratch);
        m_onionClear = false;
    } else if (!m_onionClear) {
        m_onion.clear();
        m_onionClear = true;
        area = m_doc->bounds();
    } else {
        return;
    }
    m_onionDirty = m_onionDirty.united(area);
}

QImage Editor::onion() const
{
    return QImage(m_onion.data(), m_onion.width(), m_onion.height(), m_onion.stride(), QImage::Format_RGBA8888);
}

QRect Editor::takeOnionDirty()
{
    const QRect r(m_onionDirty.x, m_onionDirty.y, m_onionDirty.w, m_onionDirty.h);
    m_onionDirty = {};
    return r;
}

void Editor::setOnionSkin(bool on)
{
    if (m_onionEnabled == on)
        return;
    m_onionEnabled = on;
    emit onionChanged();
    invalidateAll();
}

void Editor::setOnionBefore(int frames)
{
    frames = std::clamp(frames, 0, 10);
    if (m_onionOptions.before == frames)
        return;
    m_onionOptions.before = frames;
    emit onionChanged();
    invalidateAll();
}

void Editor::setOnionAfter(int frames)
{
    frames = std::clamp(frames, 0, 10);
    if (m_onionOptions.after == frames)
        return;
    m_onionOptions.after = frames;
    emit onionChanged();
    invalidateAll();
}

void Editor::setOnionOpacity(int percent)
{
    const int value = std::clamp(percent, 0, 100) * 255 / 100;
    if (m_onionOptions.opacity == value)
        return;
    m_onionOptions.opacity = value;
    emit onionChanged();
    invalidateAll();
}

void Editor::setOnionTint(bool on)
{
    if (m_onionOptions.tint == on)
        return;
    m_onionOptions.tint = on;
    emit onionChanged();
    invalidateAll();
}

void Editor::setOnionFront(bool on)
{
    if (m_onionFront == on)
        return;
    m_onionFront = on;
    emit onionChanged();
    invalidateAll();
}

void Editor::setOnionLoop(bool on)
{
    if (m_onionLoop == on)
        return;
    m_onionLoop = on;
    emit onionChanged();
    invalidateAll();
}

void Editor::invalidateAll() { invalidate(m_doc->bounds()); }

QImage Editor::composite() const
{
    return QImage(m_composite.data(), m_composite.width(), m_composite.height(), m_composite.stride(),
                  QImage::Format_RGBA8888);
}

QImage Editor::overlay() const
{
    return QImage(m_overlay.data(), m_overlay.width(), m_overlay.height(), m_overlay.stride(),
                  QImage::Format_RGBA8888);
}

QRect Editor::takeOverlayDirty()
{
    const QRect r(m_overlayDirty.x, m_overlayDirty.y, m_overlayDirty.w, m_overlayDirty.h);
    m_overlayDirty = {};
    return r;
}

std::optional<QPoint> Editor::lastPainted() const
{
    return m_lastPainted ? std::optional<QPoint>(QPoint(m_lastPainted->x, m_lastPainted->y)) : std::nullopt;
}

void Editor::setLinePreview(QPoint hover, bool active)
{
    if (hover == m_previewHover && active == m_previewActive)
        return;
    m_previewHover = hover;
    m_previewActive = active;
    refreshOverlay();
    if (m_polygon.active)
        emit lassoPathChanged(); // its next segment follows the pointer
}

// Whether the pointer is over the canvas (the symmetry copies of the hovered brush show only then).
void Editor::setHoverInside(bool inside)
{
    if (m_hoverInside == inside)
        return;
    m_hoverInside = inside;
    refreshOverlay();
}

bool Editor::canDrawOnCurrentLayer() const
{
    return m_current && m_current->kind == LayerKind::Image && effectivelyVisible(*m_current)
        && !effectivelyLocked(*m_current);
}

void Editor::refreshOverlay()
{
    if (m_overlay.isNull())
        return;
    m_overlay.fillRect(m_overlayDrawn, 0);
    m_overlayDirty = m_overlayDirty.united(m_overlayDrawn);
    Rect drawn;
    if (m_float.active) {
        if (m_tileMode) { // what's past a tiled edge shows at the other side
            for (int y = 0; y < m_float.rgba.height(); ++y)
                for (int x = 0; x < m_float.rgba.width(); ++x) {
                    const uint32_t v = m_float.rgba.pixel(x, y);
                    const QPoint w = wrapPixel(QPoint(m_float.position.x + x, m_float.position.y + y));
                    if ((v >> 24) && m_overlay.bounds().contains(w.x(), w.y()))
                        m_overlay.setPixel(w.x(), w.y(), v);
                }
            drawn = m_overlay.bounds();
        } else {
            m_overlay.blit(m_float.rgba, m_float.position);
            drawn = m_float.rect().intersected(m_overlay.bounds());
        }
    }
    // The Shift-line preview, or (with symmetry) the hovered brush's other copies, which the canvas's
    // own brush outline doesn't show.
    const bool lineGhost = m_previewActive && m_lastPainted;
    const bool copiesGhost = !lineGhost && m_hoverInside && (symmetryElements().size() > 1 || m_symmetryRadial >= 2);
    if ((lineGhost || copiesGhost) && !m_stroke.active && (m_tool == Pencil || m_tool == Eraser)
        && canDrawOnCurrentLayer()) {
        const bool ghost = m_tool == Eraser || m_shading; // no color of its own
        Rgba ink = toRgba(m_primary);
        ink.a = uint8_t(ink.a * 0.6);
        const uint32_t value = ghost ? pack({255, 255, 255, 90}) : pack(ink);
        std::optional<BrushSet> custom;
        if (m_brushShape == CustomBrush)
            custom = customBrushes(false, true);
        else if (brushIsImage())
            custom = customBrushes(false, false);
        const bool colors = previewsBrushColors();
        // In the tiled view the stamps repeat a canvas away on the tiled axes, so
        // what crosses an edge shows on the other side too.
        const int w = m_doc->width(), h = m_doc->height();
        const int kx = tileX() ? 1 : 0, ky = tileY() ? 1 : 0;
        auto stampAt = [&](Point p, bool skipFirst) {
            for (int cy = -ky; cy <= ky; ++cy)
                for (int cx = -kx; cx <= kx; ++cx)
                    drawn = drawn.united(stampMirrored(m_overlay, {0, 0}, {p.x + cx * w, p.y + cy * h}, value,
                                                       custom ? &*custom : nullptr, colors, skipFirst));
        };
        if (lineGhost)
            bresenham(*m_lastPainted, toPoint(m_previewHover), [&](Point p) { stampAt(p, false); });
        else
            stampAt(toPoint(m_previewHover), true);
        if (copiesGhost && colors) // a brush in its own colors: as faint as the canvas's outline
            for (int y = drawn.y; y < drawn.bottom(); ++y)
                for (int x = drawn.x; x < drawn.right(); ++x)
                    if (uint32_t v = m_overlay.pixel(x, y); v >> 24) {
                        Rgba c = unpack(v);
                        c.a = uint8_t(c.a * 0.75);
                        m_overlay.setPixel(x, y, pack(c));
                    }
        if (!m_doc->selection.isEmpty())
            for (int y = drawn.y; y < drawn.bottom(); ++y)
                for (int x = drawn.x; x < drawn.right(); ++x)
                    if (!m_doc->selection.contains(x, y))
                        m_overlay.setPixel(x, y, 0);
    }
    m_overlayDrawn = drawn;
    m_overlayDirty = m_overlayDirty.united(drawn);
    if (!m_overlayDirty.isEmpty())
        emit canvasChanged();
}

QList<QPointF> Editor::lassoPath() const
{
    // Pixel centers, so the line runs through the pixels the outline covers.
    QList<QPointF> path;
    auto add = [&](Point p) { path.append(QPointF(p.x + 0.5, p.y + 0.5)); };
    if (m_stroke.active && m_stroke.kind == Stroke::Select && !m_stroke.lasso.empty()) {
        for (const Point p : m_stroke.lasso)
            add(p);
    } else if (m_polygon.active) {
        for (const Point p : m_polygon.corners)
            add(p);
        // The next segment, to the pointer (Shift: 45° steps).
        const Point hover = toPoint(m_previewHover);
        const bool snap = m_previewActive && !(m_polygon.modeModifiers & Qt::ShiftModifier);
        add(snap ? snapLine(m_polygon.corners.back(), hover) : hover);
    }
    return path;
}

// --- polygon lasso ----------------------------------------------------------

void Editor::setLassoPolygon(bool on)
{
    if (m_lassoPolygon == on)
        return;
    cancelPolygon();
    m_lassoPolygon = on;
    emit toolOptionsChanged();
}

// A click with the lasso in polygon mode: the first one starts the polygon
// (Shift / Alt then pick its mode), later ones add corners (Shift: snapped to
// 45° steps from the last one); a click on the first corner closes it.
void Editor::polygonClick(QPoint pixel, Qt::KeyboardModifiers modifiers)
{
    const Point p = toPoint(pixel);
    if (!m_polygon.active) {
        m_polygon = {};
        m_polygon.active = true;
        m_polygon.mode = selectModeFor(modifiers);
        m_polygon.modeModifiers = modifiers & (Qt::ShiftModifier | Qt::AltModifier);
        m_polygon.corners = {p};
        emit polygonChanged();
        emit lassoPathChanged();
        return;
    }
    const bool snap = (modifiers & ~m_polygon.modeModifiers) & Qt::ShiftModifier;
    const Point q = snap ? snapLine(m_polygon.corners.back(), p) : p;
    if (m_polygon.corners.size() >= 3 && q == m_polygon.corners.front()) {
        closePolygon();
        return;
    }
    if (q != m_polygon.corners.back()) {
        m_polygon.corners.push_back(q);
        emit lassoPathChanged();
    }
}

void Editor::closePolygon()
{
    if (!m_polygon.active)
        return;
    const Polygon polygon = m_polygon;
    m_polygon = {};
    emit polygonChanged();
    emit lassoPathChanged();
    if (polygon.corners.size() < 2) { // just a click
        if (polygon.mode == Stroke::Replace)
            deselect();
        return;
    }
    commitFloating();
    setSelection(combinedSelection(m_doc->selection, polygonMask(polygon.corners, m_doc->bounds()), polygon.mode));
    recordSelection(selectLabel(polygon.mode));
}

void Editor::cancelPolygon()
{
    if (!m_polygon.active)
        return;
    m_polygon = {};
    emit polygonChanged();
    emit lassoPathChanged();
}

void Editor::removePolygonCorner()
{
    if (!m_polygon.active)
        return;
    m_polygon.corners.pop_back();
    if (m_polygon.corners.empty())
        cancelPolygon();
    else
        emit lassoPathChanged();
}

QRect Editor::takeDirty()
{
    const QRect r(m_dirty.x, m_dirty.y, m_dirty.w, m_dirty.h);
    m_dirty = {};
    return r;
}
