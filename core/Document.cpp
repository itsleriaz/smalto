#include "core/Document.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>

namespace core {

namespace {

template <class L, class F>
void visit(L& layer, const F& fn)
{
    for (auto& child : layer.children) {
        fn(*child);
        if (child->kind == LayerKind::Group)
            visit(*child, fn);
    }
}

} // namespace

Document::Document(int width, int height, ColorMode mode)
    : m_width(std::max(1, width))
    , m_height(std::max(1, height))
    , m_colorMode(mode)
    , m_frames(1)
{
}

void Document::setSize(int width, int height)
{
    m_width = std::max(1, width);
    m_height = std::max(1, height);
}

PixelFormat Document::pixelFormat() const
{
    switch (m_colorMode) {
    case ColorMode::Indexed: return PixelFormat::Indexed8;
    case ColorMode::Grayscale: return PixelFormat::Gray16;
    default: return PixelFormat::Rgba8;
    }
}

void Document::forEachLayer(const std::function<void(Layer&)>& fn) { visit(m_root, fn); }
void Document::forEachLayer(const std::function<void(const Layer&)>& fn) const { visit(m_root, fn); }

std::unique_ptr<Layer> Document::makeImageLayer(std::string name) const
{
    auto layer = std::make_unique<Layer>(LayerKind::Image, std::move(name));
    layer->cels.resize(size_t(frameCount()));
    return layer;
}

std::unique_ptr<Layer> Document::makeGroupLayer(std::string name) const
{
    return std::make_unique<Layer>(LayerKind::Group, std::move(name));
}

void Document::resizeCels(Layer& layer) const
{
    if (layer.hasCels())
        layer.cels.resize(size_t(frameCount()));
    for (auto& child : layer.children)
        resizeCels(*child);
}

Layer* Document::insertLayer(Layer& parent, int index, std::unique_ptr<Layer> layer)
{
    assert(parent.kind == LayerKind::Group);
    index = std::clamp(index, 0, int(parent.children.size()));
    resizeCels(*layer);
    layer->parent = &parent;
    Layer* raw = layer.get();
    parent.children.insert(parent.children.begin() + index, std::move(layer));
    return raw;
}

std::unique_ptr<Layer> Document::takeLayer(Layer& parent, int index)
{
    assert(index >= 0 && index < int(parent.children.size()));
    auto layer = std::move(parent.children[size_t(index)]);
    parent.children.erase(parent.children.begin() + index);
    layer->parent = nullptr;
    return layer;
}

void Document::insertFrame(int index, Frame frame, const std::vector<CelRef>& cels)
{
    index = std::clamp(index, 0, frameCount());
    for (Tag& tag : tags) {
        if (tag.from >= index)
            ++tag.from;
        if (tag.to >= index)
            ++tag.to;
    }
    m_frames.insert(m_frames.begin() + index, frame);
    size_t k = 0;
    forEachLayer([&](Layer& layer) {
        if (!layer.hasCels())
            return;
        layer.cels.insert(layer.cels.begin() + index, k < cels.size() ? cels[k] : nullptr);
        ++k;
    });
    refreshLiveLayers();
}

std::pair<Frame, std::vector<CelRef>> Document::removeFrame(int index)
{
    assert(index >= 0 && index < frameCount());
    std::pair<Frame, std::vector<CelRef>> removed{m_frames[size_t(index)], {}};
    for (Tag& tag : tags) {
        if (tag.from > index)
            --tag.from;
        if (tag.to >= index)
            --tag.to;
    }
    std::erase_if(tags, [](const Tag& tag) { return tag.to < tag.from; });
    m_frames.erase(m_frames.begin() + index);
    forEachLayer([&](Layer& layer) {
        if (!layer.hasCels())
            return;
        removed.second.push_back(layer.cels[size_t(index)]);
        layer.cels.erase(layer.cels.begin() + index);
    });
    refreshLiveLayers();
    return removed;
}

namespace {

// Moves v[first, first + count) so it starts at newFirst.
template <class T>
void moveBlock(std::vector<T>& v, int first, int count, int newFirst)
{
    auto begin = v.begin();
    if (newFirst < first)
        std::rotate(begin + newFirst, begin + first, begin + first + count);
    else if (newFirst > first)
        std::rotate(begin + first, begin + first + count, begin + newFirst + count);
}

} // namespace

void Document::moveFrames(int first, int count, int newFirst)
{
    assert(first >= 0 && count > 0 && first + count <= frameCount());
    assert(newFirst >= 0 && newFirst + count <= frameCount());
    moveBlock(m_frames, first, count, newFirst);
    forEachLayer([&](Layer& layer) {
        if (layer.hasCels())
            moveBlock(layer.cels, first, count, newFirst);
    });
    refreshLiveLayers();
}

void Document::reverseFrames(int first, int last)
{
    assert(first >= 0 && last < frameCount() && first <= last);
    std::reverse(m_frames.begin() + first, m_frames.begin() + last + 1);
    forEachLayer([&](Layer& layer) {
        if (layer.hasCels())
            std::reverse(layer.cels.begin() + first, layer.cels.begin() + last + 1);
    });
    refreshLiveLayers();
}

void Document::refreshLiveLayers()
{
    forEachLayer([&](Layer& layer) {
        if (layer.isLive())
            renderParticleLayer(*this, layer);
    });
}

std::vector<CelRef> Document::celsAt(int frame) const
{
    std::vector<CelRef> cels;
    forEachLayer([&](const Layer& layer) {
        if (layer.hasCels())
            cels.push_back(layer.cel(frame));
    });
    return cels;
}

std::unique_ptr<Layer> cloneLayer(const Layer& layer)
{
    auto copy = std::make_unique<Layer>(layer.kind, layer.name);
    copy->visible = layer.visible;
    copy->locked = layer.locked;
    copy->opacity = layer.opacity;
    copy->blend = layer.blend;
    copy->extraFlags = layer.extraFlags;
    copy->tilesetIndex = layer.tilesetIndex;
    copy->userData = layer.userData; // the UUID stays with the original
    copy->particles = layer.particles;
    copy->smear = layer.smear;
    std::unordered_map<const CelData*, CelRef> clones;
    for (const CelRef& cel : layer.cels) {
        if (!cel) {
            copy->cels.push_back(nullptr);
            continue;
        }
        auto& clone = clones[cel.get()];
        if (!clone)
            clone = cloneCel(cel);
        copy->cels.push_back(clone);
    }
    for (const auto& child : layer.children) {
        auto c = cloneLayer(*child);
        c->parent = copy.get();
        copy->children.push_back(std::move(c));
    }
    return copy;
}

std::unique_ptr<Document> cloneDocument(const Document& doc)
{
    auto copy = std::make_unique<Document>(doc.width(), doc.height(), doc.colorMode());
    copy->palette = doc.palette;
    copy->transparentIndex = doc.transparentIndex;
    copy->pixelRatio = doc.pixelRatio;
    copy->grid = doc.grid;
    copy->selection = doc.selection;
    copy->userData = doc.userData;
    copy->opaqueChunks = doc.opaqueChunks;
    copy->slices = doc.slices;
    copy->tilesets = doc.tilesets;
    for (Tileset& set : copy->tilesets)
        for (auto& tile : set.tiles)
            tile = tile ? std::make_shared<Image>(*tile) : nullptr;
    // Frames before layers, so each layer comes in with a cel slot per frame.
    copy->frame(0) = doc.frame(0);
    for (int f = 1; f < doc.frameCount(); ++f)
        copy->insertFrame(f, doc.frame(f));
    copy->tags = doc.tags; // after the frames: inserting frames shifts tags
    // cloneLayer leaves UUIDs behind (it's for duplicating); a copy keeps them.
    const std::function<void(const Layer&, Layer&)> keepUuids = [&](const Layer& from, Layer& to) {
        to.uuid = from.uuid;
        for (size_t i = 0; i < from.children.size(); ++i)
            keepUuids(*from.children[i], *to.children[i]);
    };
    int index = 0;
    for (const auto& layer : doc.root().children) {
        auto c = cloneLayer(*layer);
        keepUuids(*layer, *c);
        copy->insertLayer(copy->root(), index++, std::move(c));
    }
    return copy;
}

std::vector<std::shared_ptr<Image>> celImages(const Document& doc)
{
    std::vector<std::shared_ptr<Image>> images;
    std::unordered_set<const Image*> seen;
    doc.forEachLayer([&](const Layer& layer) {
        for (const CelRef& cel : layer.cels)
            if (cel && cel->image && seen.insert(cel->image.get()).second)
                images.push_back(cel->image);
    });
    return images;
}

Image remapped(const Image& image, const std::vector<int>& map)
{
    Image out = image;
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x) {
            const uint32_t v = out.pixel(x, y);
            if (v < map.size())
                out.setPixel(x, y, uint32_t(map[v]));
        }
    return out;
}

bool isWithin(const Layer& layer, const Layer& ancestor)
{
    for (const Layer* l = &layer; l; l = l->parent)
        if (l == &ancestor)
            return true;
    return false;
}

CelRef cloneCel(const CelRef& cel)
{
    if (!cel)
        return nullptr;
    auto copy = std::make_shared<CelData>(*cel);
    if (cel->image)
        copy->image = std::make_shared<Image>(*cel->image);
    return copy;
}

const SliceKey* Slice::keyAt(int frame) const
{
    const SliceKey* found = nullptr;
    for (const SliceKey& k : keys)
        if (k.frame <= frame)
            found = &k;
    return found;
}

} // namespace core
