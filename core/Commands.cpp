#include "core/Commands.h"

#include "core/Particles.h"
#include "core/Transform.h"

#include <cmath>
#include <functional>
#include <unordered_set>

namespace core {

namespace {

size_t imageBytes(const std::shared_ptr<Image>& image)
{
    return image ? size_t(image->width()) * size_t(image->height()) * size_t(image->bpp()) : 0;
}

size_t celBytes(const CelRef& cel) { return cel ? imageBytes(cel->image) : 0; }

size_t celsBytes(const std::vector<CelRef>& cels)
{
    size_t total = 0;
    for (const auto& c : cels)
        total += celBytes(c);
    return total;
}

size_t layerBytes(const Layer& layer)
{
    size_t total = celsBytes(layer.cels);
    for (const auto& child : layer.children)
        total += layerBytes(*child);
    return total;
}

} // namespace


ImagePatchCommand::ImagePatchCommand(std::shared_ptr<Image> target, Point at, Image before,
                                     Image after, std::string label)
    : m_target(std::move(target))
    , m_at(at)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_label(std::move(label))
{
}

void ImagePatchCommand::undo(Document&) { m_target->blit(m_before, m_at); }
void ImagePatchCommand::redo(Document&) { m_target->blit(m_after, m_at); }

SetCelCommand::SetCelCommand(Layer& layer, int frame, CelRef before, CelRef after)
    : m_layer(layer)
    , m_frame(frame)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void SetCelCommand::undo(Document&) { m_layer.cels[size_t(m_frame)] = m_before; }
void SetCelCommand::redo(Document&) { m_layer.cels[size_t(m_frame)] = m_after; }

CommandGroup::CommandGroup(std::string label, std::vector<std::unique_ptr<Command>> commands)
    : m_label(std::move(label))
    , m_commands(std::move(commands))
{
}

void CommandGroup::undo(Document& doc)
{
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
        (*it)->undo(doc);
}

void CommandGroup::redo(Document& doc)
{
    for (auto& command : m_commands)
        command->redo(doc);
}

bool CommandGroup::changesContent() const
{
    for (const auto& c : m_commands)
        if (c->changesContent())
            return true;
    return false;
}

MoveLayerCommand::MoveLayerCommand(Layer& fromParent, int fromIndex, Layer& toParent, int toIndex)
    : m_fromParent(fromParent)
    , m_fromIndex(fromIndex)
    , m_toParent(toParent)
    , m_toIndex(toIndex)
{
}

void MoveLayerCommand::redo(Document& doc)
{
    doc.insertLayer(m_toParent, m_toIndex, doc.takeLayer(m_fromParent, m_fromIndex));
}

void MoveLayerCommand::undo(Document& doc)
{
    doc.insertLayer(m_fromParent, m_fromIndex, doc.takeLayer(m_toParent, m_toIndex));
}

ReplaceCelsCommand::ReplaceCelsCommand(std::vector<Change> changes, std::string label)
    : m_changes(std::move(changes))
    , m_label(std::move(label))
{
}

void ReplaceCelsCommand::undo(Document&)
{
    for (const Change& c : m_changes) {
        c.cel->image = c.imageBefore;
        c.cel->position = c.positionBefore;
    }
}

void ReplaceCelsCommand::redo(Document&)
{
    for (const Change& c : m_changes) {
        c.cel->image = c.imageAfter;
        c.cel->position = c.positionAfter;
    }
}

MoveCelCommand::MoveCelCommand(CelRef cel, Point from, Point to)
    : m_cel(std::move(cel))
    , m_from(from)
    , m_to(to)
{
}

void MoveCelCommand::undo(Document&) { m_cel->position = m_from; }
void MoveCelCommand::redo(Document&) { m_cel->position = m_to; }

InsertLayerCommand::InsertLayerCommand(Layer& parent, int index, std::unique_ptr<Layer> layer)
    : m_parent(parent)
    , m_index(index)
    , m_layer(std::move(layer))
{
}

void InsertLayerCommand::redo(Document& doc) { doc.insertLayer(m_parent, m_index, std::move(m_layer)); }
void InsertLayerCommand::undo(Document& doc) { m_layer = doc.takeLayer(m_parent, m_index); }

RemoveLayerCommand::RemoveLayerCommand(Layer& parent, int index)
    : m_parent(parent)
    , m_index(index)
{
}

void RemoveLayerCommand::redo(Document& doc) { m_layer = doc.takeLayer(m_parent, m_index); }
void RemoveLayerCommand::undo(Document& doc) { doc.insertLayer(m_parent, m_index, std::move(m_layer)); }

InsertFrameCommand::InsertFrameCommand(int index, Frame frame, std::vector<CelRef> cels)
    : m_index(index)
    , m_frame(frame)
    , m_cels(std::move(cels))
{
}

void InsertFrameCommand::redo(Document& doc)
{
    m_tags = doc.tags;
    doc.insertFrame(m_index, m_frame, m_cels);
}

void InsertFrameCommand::undo(Document& doc)
{
    doc.removeFrame(m_index);
    doc.tags = m_tags;
}

RemoveFrameCommand::RemoveFrameCommand(int index)
    : m_index(index)
{
}

void RemoveFrameCommand::redo(Document& doc)
{
    m_tags = doc.tags;
    auto [frame, cels] = doc.removeFrame(m_index);
    m_frame = frame;
    m_cels = std::move(cels);
}

void RemoveFrameCommand::undo(Document& doc)
{
    doc.insertFrame(m_index, m_frame, m_cels);
    doc.tags = m_tags;
}

MoveFramesCommand::MoveFramesCommand(int first, int count, int newFirst)
    : m_first(first)
    , m_count(count)
    , m_newFirst(newFirst)
{
}

void MoveFramesCommand::redo(Document& doc) { doc.moveFrames(m_first, m_count, m_newFirst); }
void MoveFramesCommand::undo(Document& doc) { doc.moveFrames(m_newFirst, m_count, m_first); }

ReverseFramesCommand::ReverseFramesCommand(int first, int last)
    : m_first(first)
    , m_last(last)
{
}

LayerProps LayerProps::of(const Layer& layer)
{
    return {layer.visible, layer.locked, layer.opacity, layer.blend, layer.name, layer.isAlphaLocked()};
}

void LayerProps::applyTo(Layer& layer) const
{
    layer.visible = visible;
    layer.locked = locked;
    layer.opacity = opacity;
    layer.blend = blend;
    layer.name = name;
    layer.extraFlags = alphaLock ? layer.extraFlags | Layer::kAlphaLockFlag
                                 : layer.extraFlags & ~Layer::kAlphaLockFlag;
}

SetLayerPropsCommand::SetLayerPropsCommand(Layer& layer, LayerProps before, LayerProps after, std::string label)
    : m_layer(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_label(std::move(label))
{
}

bool SetLayerPropsCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetLayerPropsCommand*>(&next);
    if (!other || &other->m_layer != &m_layer || other->m_label != m_label)
        return false;
    m_after = other->m_after;
    return true;
}

SetParticlesCommand::SetParticlesCommand(Layer& layer, ParticleRecipe before, ParticleRecipe after, std::string label)
    : m_layer(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_label(std::move(label))
{
}

void SetParticlesCommand::undo(Document& doc)
{
    m_layer.particles = m_before;
    renderParticleLayer(doc, m_layer);
}

void SetParticlesCommand::redo(Document& doc)
{
    m_layer.particles = m_after;
    renderParticleLayer(doc, m_layer);
}

bool SetParticlesCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetParticlesCommand*>(&next);
    if (!other || &other->m_layer != &m_layer || other->m_label != m_label)
        return false;
    m_after = other->m_after;
    return true;
}

SetSmearCommand::SetSmearCommand(Layer& group, SmearRecipe before, SmearRecipe after, std::string label)
    : m_group(group)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_label(std::move(label))
{
}

void SetSmearCommand::undo(Document&) { m_group.smear = m_before; }
void SetSmearCommand::redo(Document&) { m_group.smear = m_after; }

bool SetSmearCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetSmearCommand*>(&next);
    if (!other || &other->m_group != &m_group || other->m_label != m_label)
        return false;
    m_after = other->m_after;
    return true;
}

MoveSmearGroupCommand::MoveSmearGroupCommand(Layer& group, int dx, int dy)
    : m_group(group)
    , m_dx(dx)
    , m_dy(dy)
{
}

void MoveSmearGroupCommand::apply(Document& doc, int dx, int dy)
{
    if (m_group.smear) {
        m_group.smear->pivotX += dx;
        m_group.smear->pivotY += dy;
    }
    std::unordered_set<const CelData*> moved;
    std::function<void(Layer&)> visit = [&](Layer& layer) {
        for (auto& child : layer.children)
            visit(*child);
        if (layer.particles) { // drawn from its recipe: the recipe moves
            layer.particles = movedRecipe(*layer.particles, 1, 1, dx, dy);
            renderParticleLayer(doc, layer);
            return;
        }
        for (const CelRef& cel : layer.cels)
            if (cel && moved.insert(cel.get()).second) {
                cel->position.x += dx;
                cel->position.y += dy;
            }
    };
    for (auto& child : m_group.children)
        visit(*child);
}

bool MoveSmearGroupCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MoveSmearGroupCommand*>(&next);
    if (!other || &other->m_group != &m_group)
        return false;
    m_dx += other->m_dx;
    m_dy += other->m_dy;
    return true;
}

BakeLayerCommand::BakeLayerCommand(Layer& layer, Bytes userDataWithout)
    : m_layer(layer)
    , m_recipe(layer.particles.value_or(ParticleRecipe()))
    , m_with(layer.userData)
    , m_without(std::move(userDataWithout))
{
}

void BakeLayerCommand::undo(Document&)
{
    m_layer.particles = m_recipe;
    m_layer.userData = m_with;
}

void BakeLayerCommand::redo(Document&)
{
    m_layer.particles.reset();
    m_layer.userData = m_without;
}

ResolveStaleCommand::ResolveStaleCommand(Layer& layer, bool render, Bytes userDataWithout)
    : m_layer(layer)
    , m_render(render)
    , m_recipe(layer.staleRecipe.value_or(ParticleRecipe()))
    , m_cels(layer.cels)
    , m_with(layer.userData)
    , m_without(std::move(userDataWithout))
{
}

void ResolveStaleCommand::undo(Document&)
{
    m_layer.staleRecipe = m_recipe;
    m_layer.particles.reset();
    m_layer.cels = m_cels;
    m_layer.userData = m_with;
}

void ResolveStaleCommand::redo(Document& doc)
{
    m_layer.staleRecipe.reset();
    if (m_render) {
        m_layer.particles = m_recipe;
        renderParticleLayer(doc, m_layer);
    } else {
        m_layer.userData = m_without;
    }
}

SetFrameDurationsCommand::SetFrameDurationsCommand(Durations before, Durations after)
    : m_before(std::move(before))
    , m_after(std::move(after))
{
}

void SetFrameDurationsCommand::apply(Document& doc, const Durations& durations)
{
    for (const auto& [frame, ms] : durations)
        if (frame >= 0 && frame < doc.frameCount())
            doc.frame(frame).durationMs = ms;
}

bool SetFrameDurationsCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetFrameDurationsCommand*>(&next);
    if (!other || other->m_after.size() != m_after.size())
        return false;
    for (size_t i = 0; i < m_after.size(); ++i)
        if (other->m_after[i].first != m_after[i].first)
            return false; // different frames
    m_after = other->m_after;
    return true;
}

bool SetSlicesCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetSlicesCommand*>(&next);
    if (!other || other->m_label != m_label)
        return false;
    m_after = other->m_after;
    return true;
}

bool SetGridCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetGridCommand*>(&next);
    if (!other)
        return false;
    m_after = other->m_after;
    return true;
}

SetPaletteCommand::SetPaletteCommand(Palette before, int transparentBefore, Palette after, int transparentAfter,
                                     std::string label)
    : m_before(std::move(before))
    , m_transparentBefore(transparentBefore)
    , m_after(std::move(after))
    , m_transparentAfter(transparentAfter)
    , m_label(std::move(label))
{
}

void SetPaletteCommand::undo(Document& doc)
{
    doc.palette = m_before;
    doc.transparentIndex = m_transparentBefore;
    if (doc.colorMode() == ColorMode::Indexed)
        doc.refreshLiveLayers(); // their pixels are palette indices
}

void SetPaletteCommand::redo(Document& doc)
{
    doc.palette = m_after;
    doc.transparentIndex = m_transparentAfter;
    if (doc.colorMode() == ColorMode::Indexed)
        doc.refreshLiveLayers();
}

SetTagsCommand::SetTagsCommand(std::vector<Tag> before, std::vector<Tag> after, std::string label)
    : m_before(std::move(before))
    , m_after(std::move(after))
    , m_label(std::move(label))
{
}

SetSelectionCommand::SetSelectionCommand(Mask before, Mask after, std::string label)
    : m_before(std::move(before))
    , m_after(std::move(after))
    , m_label(std::move(label))
{
}

bool SetSelectionCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetSelectionCommand*>(&next);
    if (!other || other->m_label != m_label)
        return false;
    m_after = other->m_after;
    return true;
}

ResizeSpriteCommand::ResizeSpriteCommand(Point sizeBefore, Point sizeAfter, std::vector<CelChange> cels,
                                         std::string label, Rect gridBefore, Rect gridAfter,
                                         std::vector<Slice> slicesBefore, std::vector<Slice> slicesAfter,
                                         std::vector<Tileset> tilesetsBefore, std::vector<Tileset> tilesetsAfter)
    : m_sizeBefore(sizeBefore)
    , m_sizeAfter(sizeAfter)
    , m_cels(std::move(cels))
    , m_label(std::move(label))
    , m_gridBefore(gridBefore)
    , m_gridAfter(gridAfter)
    , m_slicesBefore(std::move(slicesBefore))
    , m_slicesAfter(std::move(slicesAfter))
    , m_tilesetsBefore(std::move(tilesetsBefore))
    , m_tilesetsAfter(std::move(tilesetsAfter))
{
}

void ResizeSpriteCommand::undo(Document& doc)
{
    doc.setSize(m_sizeBefore.x, m_sizeBefore.y);
    doc.grid = m_gridBefore;
    doc.slices = m_slicesBefore;
    if (!m_tilesetsBefore.empty() || !m_tilesetsAfter.empty())
        doc.tilesets = m_tilesetsBefore;
    for (const CelChange& c : m_cels) {
        c.cel->image = c.imageBefore;
        c.cel->position = c.positionBefore;
    }
    for (const LiveChange& l : m_live) {
        l.layer->particles = l.liveBefore;
        l.layer->staleRecipe = l.staleBefore;
        l.layer->smear = l.smearBefore;
    }
    doc.refreshLiveLayers(); // rendered for the new size
}

void ResizeSpriteCommand::redo(Document& doc)
{
    doc.setSize(m_sizeAfter.x, m_sizeAfter.y);
    doc.grid = m_gridAfter;
    doc.slices = m_slicesAfter;
    if (!m_tilesetsBefore.empty() || !m_tilesetsAfter.empty())
        doc.tilesets = m_tilesetsAfter;
    for (const CelChange& c : m_cels) {
        c.cel->image = c.imageAfter;
        c.cel->position = c.positionAfter;
    }
    for (const LiveChange& l : m_live) {
        l.layer->particles = l.liveAfter;
        l.layer->staleRecipe = l.staleAfter;
        l.layer->smear = l.smearAfter;
    }
    doc.refreshLiveLayers();
}

SetColorModeCommand::SetColorModeCommand(State before, State after, std::vector<CelChange> cels)
    : m_before(std::move(before))
    , m_after(std::move(after))
    , m_cels(std::move(cels))
{
}

void SetColorModeCommand::undo(Document& doc)
{
    doc.setColorMode(m_before.mode);
    doc.palette = m_before.palette;
    doc.transparentIndex = m_before.transparentIndex;
    doc.tilesets = m_before.tilesets;
    for (const CelChange& c : m_cels)
        c.cel->image = c.imageBefore;
    doc.refreshLiveLayers(); // rendered in the mode's pixel format
}

void SetColorModeCommand::redo(Document& doc)
{
    doc.setColorMode(m_after.mode);
    doc.palette = m_after.palette;
    doc.transparentIndex = m_after.transparentIndex;
    doc.tilesets = m_after.tilesets;
    for (const CelChange& c : m_cels)
        c.cel->image = c.imageAfter;
    doc.refreshLiveLayers();
}

namespace {

// Every live layer's recipe (and every recipe waiting for a decision) with its places moved as the
// artwork moves: x * scaleX + dx.
std::vector<ResizeSpriteCommand::LiveChange> movedLiveLayers(const Document& doc, double scaleX, double scaleY,
                                                             double dx, double dy)
{
    std::vector<ResizeSpriteCommand::LiveChange> out;
    const auto moved = [&](const std::optional<ParticleRecipe>& r) -> std::optional<ParticleRecipe> {
        if (!r)
            return std::nullopt;
        return movedRecipe(*r, scaleX, scaleY, dx, dy);
    };
    doc.forEachLayer([&](const Layer& layer) {
        // The command changes the layer when it runs, like the cels it holds (the builder only reads).
        if (layer.particles || layer.staleRecipe || layer.smear) {
            std::optional<SmearRecipe> smear;
            if (layer.smear)
                smear = movedSmear(*layer.smear, scaleX, scaleY, dx, dy);
            out.push_back({const_cast<Layer*>(&layer), layer.particles, moved(layer.particles), layer.staleRecipe,
                           moved(layer.staleRecipe), layer.smear, smear});
        }
    });
    return out;
}

} // namespace

std::unique_ptr<ResizeSpriteCommand> scaleSpriteCommand(const Document& doc, int width, int height, Resample method)
{
    width = std::max(1, width);
    height = std::max(1, height);
    const double sx = double(width) / doc.width();
    const double sy = double(height) / doc.height();
    // Edges scale and round, so neighboring cels stay flush and a cel that
    // covers the canvas covers the new one.
    auto edgeX = [&](int x) { return int(std::lround(x * sx)); };
    auto edgeY = [&](int y) { return int(std::lround(y * sy)); };

    std::vector<ResizeSpriteCommand::CelChange> changes;
    std::unordered_set<const CelData*> seen;
    doc.forEachLayer([&](const Layer& layer) {
        for (const CelRef& cel : layer.cels) {
            if (!cel || !cel->image || !seen.insert(cel.get()).second)
                continue;
            ResizeSpriteCommand::CelChange c{cel, cel->image, cel->position, cel->image, cel->position};
            const Image& image = *cel->image;
            const int x0 = edgeX(cel->position.x), x1 = edgeX(cel->position.x + image.width());
            const int y0 = edgeY(cel->position.y), y1 = edgeY(cel->position.y + image.height());
            c.positionAfter = {x0, y0};
            if (layer.kind != LayerKind::Tilemap)
                c.imageAfter = std::make_shared<Image>(scaled(image, std::max(1, x1 - x0), std::max(1, y1 - y0), method));
            changes.push_back(std::move(c));
        }
    });
    // The grid and slices scale like cel edges (a 0 x 0 "no grid" stays so).
    Rect grid = doc.grid;
    if (grid.w > 0 && grid.h > 0)
        grid = {edgeX(grid.x), edgeY(grid.y), std::max(1, edgeX(grid.x + grid.w) - edgeX(grid.x)),
                std::max(1, edgeY(grid.y + grid.h) - edgeY(grid.y))};
    const auto scaleRect = [&](Rect r) {
        if (r.isEmpty())
            return r;
        const int x0 = edgeX(r.x), y0 = edgeY(r.y);
        return Rect{x0, y0, std::max(1, edgeX(r.x + r.w) - x0), std::max(1, edgeY(r.y + r.h) - y0)};
    };
    std::vector<Slice> slices = doc.slices;
    for (Slice& slice : slices)
        for (SliceKey& k : slice.keys) {
            const Rect b = k.bounds;
            k.bounds = scaleRect(b);
            // Center and pivot are relative to the bounds: scale them in place.
            const Rect c = scaleRect({b.x + k.center.x, b.y + k.center.y, k.center.w, k.center.h});
            k.center = {c.x - k.bounds.x, c.y - k.bounds.y, c.w, c.h};
            k.pivot = {edgeX(b.x + k.pivot.x) - k.bounds.x, edgeY(b.y + k.pivot.y) - k.bounds.y};
        }
    // Tiles scale like the cels (a tile's size like a cel edge from 0), so
    // tilemaps keep their layout.
    std::vector<Tileset> tilesets = doc.tilesets;
    for (Tileset& set : tilesets) {
        const int tw = std::max(1, edgeX(set.tileWidth)), th = std::max(1, edgeY(set.tileHeight));
        for (auto& tile : set.tiles)
            if (tile)
                tile = std::make_shared<Image>(scaled(*tile, tw, th, method));
        set.tileWidth = tw;
        set.tileHeight = th;
    }
    auto command = std::make_unique<ResizeSpriteCommand>(Point{doc.width(), doc.height()}, Point{width, height},
                                                         std::move(changes), "Sprite Size", doc.grid, grid, doc.slices,
                                                         std::move(slices), doc.tilesets, std::move(tilesets));
    command->setLiveChanges(movedLiveLayers(doc, sx, sy, 0, 0)); // emitters and fields scale with the art
    return command;
}

std::unique_ptr<ResizeSpriteCommand> canvasSizeCommand(const Document& doc, Rect bounds, uint32_t backgroundFill)
{
    bounds.w = std::max(1, bounds.w);
    bounds.h = std::max(1, bounds.h);
    const uint32_t transparent = doc.colorMode() == ColorMode::Indexed ? uint32_t(doc.transparentIndex) : 0;

    std::vector<ResizeSpriteCommand::CelChange> changes;
    std::unordered_set<const CelData*> seen;
    doc.forEachLayer([&](const Layer& layer) {
        for (const CelRef& cel : layer.cels) {
            if (!cel || !cel->image || !seen.insert(cel.get()).second)
                continue;
            ResizeSpriteCommand::CelChange c{cel, cel->image, cel->position, cel->image, cel->position};
            const Image& image = *cel->image;
            // Cel rect in new canvas coordinates.
            const Rect rect{cel->position.x - bounds.x, cel->position.y - bounds.y, image.width(), image.height()};
            const Rect canvas{0, 0, bounds.w, bounds.h};
            c.positionAfter = {rect.x, rect.y};
            if (layer.kind == LayerKind::Tilemap) {
                // Tile indices: only move.
            } else if (layer.isBackground()) {
                auto filled = std::make_shared<Image>(bounds.w, bounds.h, image.format());
                filled->clear(backgroundFill);
                filled->blit(image, {rect.x, rect.y});
                c.imageAfter = std::move(filled);
                c.positionAfter = {0, 0};
            } else {
                const Rect kept = rect.intersected(canvas);
                if (kept.isEmpty()) {
                    auto empty = std::make_shared<Image>(1, 1, image.format());
                    empty->clear(transparent);
                    c.imageAfter = std::move(empty);
                    c.positionAfter = {0, 0};
                } else if (kept.w != rect.w || kept.h != rect.h) {
                    c.imageAfter = std::make_shared<Image>(
                        image.copy({kept.x - rect.x, kept.y - rect.y, kept.w, kept.h}));
                    c.positionAfter = {kept.x, kept.y};
                }
            }
            changes.push_back(std::move(c));
        }
    });
    // The grid and slices move with the pixels.
    Rect grid = doc.grid;
    grid.x -= bounds.x;
    grid.y -= bounds.y;
    std::vector<Slice> slices = doc.slices;
    for (Slice& slice : slices)
        for (SliceKey& k : slice.keys) {
            k.bounds.x -= bounds.x;
            k.bounds.y -= bounds.y;
        }
    auto command = std::make_unique<ResizeSpriteCommand>(Point{doc.width(), doc.height()}, Point{bounds.w, bounds.h},
                                                         std::move(changes), "Canvas Size", doc.grid, grid, doc.slices,
                                                         std::move(slices));
    command->setLiveChanges(movedLiveLayers(doc, 1, 1, -bounds.x, -bounds.y)); // emitters and fields move with the art
    return command;
}

} // namespace core

namespace core {

size_t ImagePatchCommand::bytes() const { return 256 + size_t(m_before.stride()) * m_before.height() + size_t(m_after.stride()) * m_after.height(); }
size_t SetCelCommand::bytes() const { return 256 + celBytes(m_before) + celBytes(m_after); }
size_t CommandGroup::bytes() const
{
    size_t total = 256;
    for (const auto& c : m_commands)
        total += c->bytes();
    return total;
}
size_t ReplaceCelsCommand::bytes() const
{
    size_t total = 256;
    for (const auto& c : m_changes)
        total += imageBytes(c.imageBefore) + imageBytes(c.imageAfter);
    return total;
}
size_t InsertLayerCommand::bytes() const { return 256 + (m_layer ? layerBytes(*m_layer) : 0); }
size_t RemoveLayerCommand::bytes() const { return 256 + (m_layer ? layerBytes(*m_layer) : 0); }
size_t InsertFrameCommand::bytes() const { return 256 + celsBytes(m_cels); }
size_t RemoveFrameCommand::bytes() const { return 256 + celsBytes(m_cels); }
size_t ResizeSpriteCommand::bytes() const
{
    size_t total = 256;
    for (const auto& c : m_cels)
        total += imageBytes(c.imageBefore) + imageBytes(c.imageAfter);
    return total;
}
size_t SetColorModeCommand::bytes() const
{
    size_t total = 256;
    for (const auto& c : m_cels)
        total += imageBytes(c.imageBefore) + imageBytes(c.imageAfter);
    return total;
}
size_t ResolveStaleCommand::bytes() const { return 256 + celsBytes(m_cels); }

} // namespace core
