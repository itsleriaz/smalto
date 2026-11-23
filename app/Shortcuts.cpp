#include "Shortcuts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QSettings>

#include <memory>

namespace {

const QString kGroup = QStringLiteral("shortcuts/");

std::unique_ptr<QSettings> openSettings(const QString& file)
{
    return file.isEmpty() ? std::make_unique<QSettings>() : std::make_unique<QSettings>(file, QSettings::IniFormat);
}

// A standard key's bindings on this platform.
QStringList standard(QKeySequence::StandardKey key)
{
    QStringList out;
    for (const QKeySequence& seq : QKeySequence::keyBindings(key))
        out.append(seq.toString(QKeySequence::PortableText));
    return out;
}

// Adds a key the standard ones may already have (Redo has Ctrl+Y on Windows).
QStringList operator+(QStringList a, const char* b)
{
    if (!a.contains(QString::fromLatin1(b)))
        a.append(QString::fromLatin1(b));
    return a;
}

#ifdef Q_OS_MACOS
constexpr bool kMac = true;
#else
constexpr bool kMac = false;
#endif

} // namespace

const std::vector<Shortcuts::Action>& Shortcuts::table()
{
    // Built on first use: standard keys need the platform (QGuiApplication).
    static const std::vector<Action> actions = [] {
        const QString tools = tr("Tools"), file = tr("File"), edit = tr("Edit"), sprite = tr("Sprite"),
                      brush = tr("Brush and color"), view = tr("View"), timeline = tr("Timeline");
        // Qt's next / previous tab keys; on macOS they lack Control+Tab ("Meta" there).
        const QStringList nextTab = kMac ? standard(QKeySequence::NextChild) + "Meta+Tab"
                                         : standard(QKeySequence::NextChild);
        const QStringList previousTab = kMac ? standard(QKeySequence::PreviousChild) + "Meta+Shift+Tab"
                                             : standard(QKeySequence::PreviousChild);
        return std::vector<Action>{
            {"pencil", tools, tr("Pencil"), {"B"}},
            {"eraser", tools, tr("Eraser"), {"E"}},
            {"fill", tools, tr("Fill"), {"G"}},
            {"gradient", tools, tr("Gradient"), {"Shift+G"}},
            {"eyedropper", tools, tr("Eyedropper"), {"I"}},
            {"line", tools, tr("Line"), {"L"}},
            {"rectangle", tools, tr("Rectangle"), {"U"}},
            {"ellipse", tools, tr("Ellipse"), {"O"}},
            {"select", tools, tr("Select"), {"M"}},
            {"lasso", tools, tr("Lasso"), {"Q"}},
            {"magicWand", tools, tr("Magic Wand"), {"W"}},
            {"move", tools, tr("Move"), {"V"}},
            {"hand", tools, tr("Hand"), {"H"}},
            {"blurTool", tools, tr("Blur brush"), {"R"}},
            {"jumbleTool", tools, tr("Jumble brush"), {"J"}},
            {"text", tools, tr("Text"), {"T"}},
            {"slice", tools, tr("Slice"), {"C"}},
            {"sourceTool", tools, tr("Particle source (on a live layer)"), {"Shift+S"}},
            {"fieldTool", tools, tr("Particle field (on a live layer)"), {"Shift+F"}},
            {"tileMode", tools, tr("Tilemap: pixel / tile mode"), {"Alt+T"}},

            {"newSprite", file, tr("New sprite"), {"Ctrl+N"}},
            {"open", file, tr("Open"), {"Ctrl+O"}},
            {"save", file, tr("Save"), {"Ctrl+S"}},
            {"saveAs", file, tr("Save as"), {"Ctrl+Shift+S"}},
            {"export", file, tr("Export"), {"Ctrl+E"}},
            {"closeTab", file, tr("Close tab"), {"Ctrl+W"}},
            {"nextTab", file, tr("Next tab"), nextTab},
            {"previousTab", file, tr("Previous tab"), previousTab},
            {"preferences", file, tr("Preferences"), {"Ctrl+,"}},

            {"undo", edit, tr("Undo"), standard(QKeySequence::Undo)},
            {"redo", edit, tr("Redo"), standard(QKeySequence::Redo) + "Ctrl+Y"},
            {"cut", edit, tr("Cut"), standard(QKeySequence::Cut)},
            {"copy", edit, tr("Copy"), standard(QKeySequence::Copy)},
            {"paste", edit, tr("Paste"), standard(QKeySequence::Paste)},
            {"copyMerged", edit, tr("Copy merged (all visible layers)"), {"Ctrl+Shift+C"}},
            {"pasteNewLayer", edit, tr("Paste as new layer"), {"Ctrl+Shift+V"}},
            {"pasteNewSprite", edit, tr("Paste as new sprite"), {"Ctrl+Alt+V"}},
            {"selectAll", edit, tr("Select all"), standard(QKeySequence::SelectAll)},
            {"deselect", edit, tr("Deselect"), {"Ctrl+D"}},
            {"invertSelection", edit, tr("Invert selection"), {"Ctrl+Shift+I"}},
            {"brushFromSelection", edit, tr("Brush from selection"), {"Ctrl+B"}},
            {"outline", edit, tr("Outline"), {"Shift+O"}},
            {"replaceColor", edit, tr("Replace color"), {"Shift+R"}},
            {"adjustHsl", edit, tr("Hue / saturation"), {"Ctrl+U"}},
            {"blur", edit, tr("Blur"), {}},
            {"convolve", edit, tr("Convolution matrix"), {}},
            {"cleanup", edit, tr("Pixel cleanup"), {}},
            {"curves", edit, tr("Color curves"), {}},
            {"posterize", edit, tr("Posterize"), {}},
            {"dropShadow", edit, tr("Drop shadow"), {}},

            {"transform", sprite, tr("Transform"), {"Ctrl+T"}},
            {"flipHorizontal", sprite, tr("Flip horizontally"), {"Shift+H"}},
            {"flipVertical", sprite, tr("Flip vertically"), {"Shift+V"}},
            {"rotateClockwise", sprite, tr("Rotate 90° clockwise"), {"Ctrl+]"}},
            {"rotateCounterclockwise", sprite, tr("Rotate 90° counterclockwise"), {"Ctrl+["}},
            {"spriteSize", sprite, tr("Sprite size"), {"Ctrl+Alt+I"}},
            {"canvasSize", sprite, tr("Canvas size"), {"Ctrl+Alt+C"}},

            {"swapColors", brush, tr("Swap colors"), {"X"}},
            {"brushSmaller", brush, tr("Smaller brush"), {"["}},
            {"brushBigger", brush, tr("Bigger brush"), {"]"}},

            {"zoomIn", view, tr("Zoom in"), {"+", "="}},
            {"zoomOut", view, tr("Zoom out"), {"-"}},
            {"fitToView", view, tr("Fit to view"), {"0"}},
            {"pixelGrid", view, tr("Pixel grid"), {"Ctrl+'"}},
            {"customGrid", view, tr("Grid"), {"Ctrl+Shift+'"}},
            {"snapToGrid", view, tr("Snap to the grid"), {"Ctrl+Alt+'"}},
            {"tiledView", view, tr("Tiled view (off, both, X, Y)"), {"Shift+T"}},
            {"hideTimeline", view, tr("Show / hide the timeline"), {"Tab"}},
            {"hideUi", view, tr("Show / hide all panels"), {"Shift+Tab"}},
            {"previewPanel", view, tr("Show / hide the preview"), {"F7"}},
            {"colorsPanel", view, tr("Show / hide the colors and palette"), {"F6"}},
            {"layersPanel", view, tr("Show / hide the layers list"), {"F8"}},
            {"tagsPanel", view, tr("Show / hide the tags list"), {"F10"}},
            {"liveOverlay", view, tr("Particle controls on the canvas: shown, faded, hidden"), {"F9"}},

            {"newFrame", timeline, tr("New frame"), {"Alt+N"}},
            {"duplicateFrame", timeline, tr("Duplicate frames"), {"Alt+D"}},
            {"newLayer", timeline, tr("New layer"), {"Shift+N"}},
            {"groupLayers", timeline, tr("Group the selected layers"), {"Ctrl+G"}},
            {"ungroupLayer", timeline, tr("Ungroup"), {"Ctrl+Shift+G"}},
            {"alphaLock", timeline, tr("Alpha lock the current layer"), {"Shift+A"}},
            {"onionSkin", timeline, tr("Onion skin"), {"F3"}},
        };
    }();
    return actions;
}

Shortcuts::Shortcuts(QObject* parent)
    : Shortcuts(QString(), parent)
{
}

Shortcuts::Shortcuts(const QString& settingsFile, QObject* parent)
    : QObject(parent)
    , m_settingsFile(settingsFile)
{
    auto settings = openSettings(m_settingsFile);
    for (const Action& a : table()) {
        QStringList keys = a.defaults;
        const QVariant saved = settings->value(kGroup + a.id);
        if (saved.isValid()) {
            keys.clear();
            for (const QJsonValue& v : QJsonDocument::fromJson(saved.toString().toUtf8()).array()) {
                const QString key = normalized(v.toString());
                if (!key.isEmpty() && !isReserved(key) && !keys.contains(key))
                    keys.append(key);
            }
        }
        m_keys.insert(a.id, keys);
    }
}

const Shortcuts::Action* Shortcuts::find(const QString& id) const
{
    for (const Action& a : table())
        if (a.id == id)
            return &a;
    return nullptr;
}

QVariantMap Shortcuts::keys() const
{
    return m_keys;
}

QStringList Shortcuts::keysFor(const QString& id) const
{
    return m_keys.value(id).toStringList();
}

QVariantMap Shortcuts::labels() const
{
    QVariantMap out;
    for (auto it = m_keys.begin(); it != m_keys.end(); ++it) {
        const QStringList keys = it.value().toStringList();
        out.insert(it.key(), keys.isEmpty() ? QString() : display(keys.first()));
    }
    return out;
}

QVariantMap Shortcuts::hints() const
{
    QVariantMap out;
    for (auto it = m_keys.begin(); it != m_keys.end(); ++it) {
        const QStringList keys = it.value().toStringList();
        out.insert(it.key(), keys.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(display(keys.first())));
    }
    return out;
}

QVariantList Shortcuts::actions() const
{
    QVariantList out;
    for (const Action& a : table()) {
        const QStringList keys = keysFor(a.id);
        QStringList shown;
        for (const QString& key : keys)
            shown.append(display(key));
        out.append(QVariantMap{
            {"id", a.id},
            {"group", a.group},
            {"name", a.name},
            {"keys", keys},
            {"shown", shown},
            {"custom", keys != a.defaults},
        });
    }
    return out;
}

bool Shortcuts::customized() const
{
    for (const Action& a : table())
        if (keysFor(a.id) != a.defaults)
            return true;
    return false;
}

QString Shortcuts::owner(const QString& key) const
{
    const QString k = normalized(key);
    for (const Action& a : table())
        if (keysFor(a.id).contains(k))
            return a.id;
    return {};
}

QString Shortcuts::nameOf(const QString& id) const
{
    const Action* a = find(id);
    return a ? a->name : QString();
}

QString Shortcuts::assign(const QString& id, const QString& key)
{
    const QString k = normalized(key);
    if (!find(id) || k.isEmpty() || isReserved(k))
        return {};
    QStringList keys = keysFor(id);
    if (keys.contains(k))
        return {};
    QString taken;
    const QString other = owner(k);
    if (!other.isEmpty()) {
        QStringList otherKeys = keysFor(other);
        otherKeys.removeAll(k);
        store(other, otherKeys);
        taken = nameOf(other);
    }
    keys.append(k);
    store(id, keys);
    emit changed();
    return taken;
}

void Shortcuts::unassign(const QString& id, const QString& key)
{
    QStringList keys = keysFor(id);
    if (keys.removeAll(normalized(key)) == 0)
        return;
    store(id, keys);
    emit changed();
}

void Shortcuts::reset(const QString& id)
{
    const Action* a = find(id);
    if (!a || keysFor(id) == a->defaults)
        return;
    // Its default keys come back; other actions given them since lose them.
    for (const QString& key : a->defaults) {
        const QString other = owner(key);
        if (!other.isEmpty() && other != id) {
            QStringList otherKeys = keysFor(other);
            otherKeys.removeAll(key);
            store(other, otherKeys);
        }
    }
    store(id, a->defaults);
    emit changed();
}

void Shortcuts::resetAll()
{
    for (const Action& a : table())
        store(a.id, a.defaults);
    emit changed();
}

void Shortcuts::store(const QString& id, const QStringList& keys)
{
    m_keys.insert(id, keys);
    auto settings = openSettings(m_settingsFile);
    const Action* a = find(id);
    if (a && keys == a->defaults) {
        settings->remove(kGroup + id);
    } else {
        // JSON: a key can contain a comma ("Ctrl+,"), and an empty list
        // (no key) must survive the round trip.
        settings->setValue(kGroup + id,
                           QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(keys)).toJson(QJsonDocument::Compact)));
    }
}

QString Shortcuts::normalized(const QString& key)
{
    const QKeySequence seq(key, QKeySequence::PortableText);
    if (seq.count() != 1 || seq[0].key() == Qt::Key_unknown)
        return {};
    return seq.toString(QKeySequence::PortableText);
}

bool Shortcuts::isReserved(const QString& key)
{
    const QKeySequence seq(key, QKeySequence::PortableText);
    if (seq.count() != 1)
        return false;
    const QKeyCombination combo = seq[0];
    switch (combo.key()) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    case Qt::Key_Space: // held for the hand tool
        return combo.keyboardModifiers() == Qt::NoModifier;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: // stepping frames and nudging, with Shift / Alt
        return (combo.keyboardModifiers() & ~(Qt::ShiftModifier | Qt::AltModifier)) == Qt::NoModifier;
    default:
        return false;
    }
}

QString Shortcuts::fromKeyEvent(int key, int modifiers)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_unknown:
    case 0:
        return {};
    case Qt::Key_Backtab: // Shift+Tab arrives as its own key
        key = Qt::Key_Tab;
        modifiers |= Qt::ShiftModifier;
        break;
    default:
        break;
    }
    Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers)
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    // A symbol typed with Shift ("+", "!") already says so; letters, digits
    // and named keys keep it.
    const bool symbol = key > 0x20 && key < 0x7f && !(key >= 'A' && key <= 'Z') && !(key >= '0' && key <= '9');
    if (symbol)
        mods &= ~Qt::ShiftModifier;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}

QString Shortcuts::display(const QString& key)
{
    return QKeySequence(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}
