#include "TimelineModel.h"

#include <cmath>
#include <unordered_map>

using namespace core;

TimelineModel::TimelineModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void TimelineModel::setEditor(Editor* editor)
{
    if (m_editor == editor)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = editor;
    if (m_editor) {
        for (auto signal : {&Editor::layersChanged, &Editor::celsChanged, &Editor::framesChanged,
                            &Editor::documentChanged})
            connect(m_editor, signal, this, &TimelineModel::rebuild);
    }
    rebuild();
    emit editorChanged();
}

int TimelineModel::rowForLayer(int layerIndex) const { return m_rows - 1 - layerIndex; }
int TimelineModel::layerForRow(int row) const { return m_rows - 1 - row; }

const Layer* TimelineModel::layerAt(int row) const
{
    if (!m_editor || row < 0 || row >= m_rows)
        return nullptr;
    return m_editor->layerRows()[size_t(layerForRow(row))];
}

int TimelineModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_rows; }
int TimelineModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_columns; }

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {
        {CelRole, "cel"},           {LayerIndexRole, "layerIndex"},     {NameRole, "name"},
        {KindRole, "kind"},         {DepthRole, "depth"},               {LayerVisibleRole, "layerVisible"},
        {ShownRole, "shown"},       {LockedRole, "locked"},             {AlphaLockRole, "alphaLock"},
        {CollapsedRole, "collapsed"},
        {OpacityRole, "layerOpacity"}, {BlendRole, "blendName"},
    };
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    const Layer* layer = layerAt(index.row());
    if (!layer || index.column() < 0 || index.column() >= m_columns)
        return {};
    switch (role) {
    case CelRole:
        return m_flags[size_t(index.row())][size_t(index.column())];
    case LayerIndexRole:
        return layerForRow(index.row());
    case NameRole:
        return QString::fromStdString(layer->name);
    case KindRole:
        return layer->isLive() ? QStringLiteral("particles")
             : layer->kind == LayerKind::Group ? QStringLiteral("group")
             : layer->kind == LayerKind::Tilemap ? QStringLiteral("tilemap")
                                                 : QStringLiteral("image");
    case DepthRole: {
        int depth = 0;
        for (const Layer* p = layer->parent; p && p->parent; p = p->parent)
            ++depth;
        return depth;
    }
    case LayerVisibleRole:
        return layer->visible;
    case ShownRole:
        for (const Layer* l = layer; l && l->parent; l = l->parent)
            if (!l->visible)
                return false;
        return true;
    case LockedRole:
        return layer->locked;
    case AlphaLockRole:
        return layer->isAlphaLocked();
    case CollapsedRole:
        return layer->isCollapsed();
    case OpacityRole:
        return (layer->opacity * 100 + 127) / 255;
    case BlendRole:
        return QString::fromLatin1(blendModeName(layer->blend));
    default:
        return {};
    }
}

void TimelineModel::rebuild()
{
    const int rows = m_editor ? int(m_editor->layerRows().size()) : 0;
    const int columns = m_editor ? m_editor->frameCount() : 0;

    std::vector<std::vector<int>> flags(size_t(rows), std::vector<int>(size_t(columns), 0));
    for (int r = 0; r < rows; ++r) {
        const Layer& layer = *m_editor->layerRows()[size_t(rows - 1 - r)];
        auto& row = flags[size_t(r)];
        if (!layer.hasCels()) {
            std::fill(row.begin(), row.end(), int(NoCels));
            continue;
        }
        std::unordered_map<const CelData*, int> uses;
        for (int f = 0; f < columns; ++f)
            if (const CelRef cel = layer.cel(f))
                ++uses[cel.get()];
        for (int f = 0; f < columns; ++f) {
            const CelRef cel = layer.cel(f);
            if (!cel)
                continue;
            int v = HasCel;
            if (uses[cel.get()] > 1)
                v |= Linked;
            if (f > 0 && layer.cel(f - 1) == cel)
                v |= LinkedToPrevious;
            if (f + 1 < columns && layer.cel(f + 1) == cel)
                v |= LinkedToNext;
            row[size_t(f)] = v;
        }
    }

    // Same shape: update in place so views keep their state and scroll.
    if (rows == m_rows && columns == m_columns) {
        m_flags = std::move(flags);
        if (rows > 0 && columns > 0)
            emit dataChanged(index(0, 0), index(rows - 1, columns - 1));
        return;
    }
    beginResetModel();
    m_flags = std::move(flags);
    m_rows = rows;
    m_columns = columns;
    endResetModel();
}

QPoint TimelineModel::liveRange(int row) const
{
    const Layer* layer = layerAt(row);
    if (!layer || !layer->isLive())
        return {-1, -1};
    const ParticleRecipe& r = *layer->particles;
    if (r.emission == ParticleRecipe::Emission::Burst)
        return {r.startFrame, r.startFrame};
    return {r.startFrame, r.untilEnd ? std::max(r.startFrame, columnCount() - 1) : r.endFrame}; // (to the end)
}

int TimelineModel::liveTail(int row) const
{
    const Layer* layer = layerAt(row);
    if (!layer || !layer->isLive() || layer->particles->loop)
        return 0;
    const ParticleRecipe& r = *layer->particles;
    // The longest a particle lives, counting the frame it is born on.
    return int(std::ceil(r.lifetime * (1.0 + r.lifetimeRandom / 100.0))) - 1;
}

bool TimelineModel::liveStream(int row) const
{
    const Layer* layer = layerAt(row);
    return layer && layer->isLive() && layer->particles->emission == ParticleRecipe::Emission::Stream;
}

QList<int> TimelineModel::smearKeys(int row) const
{
    QList<int> frames;
    const Layer* layer = layerAt(row);
    if (layer && layer->isSmear())
        for (const core::SmearKey& k : layer->smear->keys)
            frames.push_back(k.frame);
    return frames;
}

bool TimelineModel::liveOpenEnded(int row) const
{
    const Layer* layer = layerAt(row);
    return layer && layer->isLive() && layer->particles->emission == ParticleRecipe::Emission::Stream
        && layer->particles->untilEnd;
}
