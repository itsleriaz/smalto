#pragma once

#include "core/BlendMode.h"
#include "core/Image.h"
#include "core/Mask.h"
#include "core/Palette.h"
#include "core/Particles.h"
#include "core/Smear.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace core {

enum class ColorMode : uint8_t { Rgba, Grayscale, Indexed };
enum class LayerKind : uint8_t { Image, Group, Tilemap };
enum class AniDirection : uint8_t { Forward, Reverse, PingPong, PingPongReverse };

// Opaque bytes kept only so files round-trip (e.g. Aseprite user data).
using Bytes = std::vector<uint8_t>;

// Pixel content of a cel. Linked cels share the same CelData.
struct CelData {
    std::shared_ptr<Image> image;
    Point position;
    int opacity = 255;
    int zIndex = 0;
    Bytes userData;  // raw Aseprite user data chunk payload
    Bytes extraData; // raw Aseprite cel extra chunk payload
};
using CelRef = std::shared_ptr<CelData>;

struct Layer {
    Layer(LayerKind kind, std::string name)
        : kind(kind)
        , name(std::move(name))
    {
    }

    LayerKind kind;
    std::string name;
    bool visible = true;
    bool locked = false;
    int opacity = 255;
    BlendMode blend = BlendMode::Normal;

    // Preserved from .aseprite: layer flags other than visible/editable
    // (background, reference, collapsed, ...), tileset, UUID, user data.
    uint16_t extraFlags = 0;
    uint32_t tilesetIndex = 0;
    Bytes uuid; // empty or 16 bytes
    Bytes userData;

    static constexpr uint16_t kBackgroundFlag = 8;
    static constexpr uint16_t kCollapsedFlag = 32;
    // Ours, in a bit Aseprite doesn't use (it keeps it in our files; Aseprite
    // itself drops it when it saves): painting only changes pixels the
    // layer already has.
    static constexpr uint16_t kAlphaLockFlag = 0x8000;
    bool isBackground() const { return extraFlags & kBackgroundFlag; }
    bool isCollapsed() const { return extraFlags & kCollapsedFlag; }
    bool isAlphaLocked() const { return extraFlags & kAlphaLockFlag; }

    // Image/tilemap layers: one slot per frame, nullptr means an empty cel.
    std::vector<CelRef> cels;
    // A live layer: the recipe the cels are rendered from (renderParticleLayer). Its cels are a cache;
    // painting on it isn't allowed, so they always follow the recipe.
    std::optional<ParticleRecipe> particles;
    bool isLive() const { return particles.has_value(); }
    // Read from a file whose cels no longer match the recipe stored with the layer (they were changed
    // elsewhere): an ordinary layer until the user decides (ResolveStaleCommand).
    std::optional<ParticleRecipe> staleRecipe;
    // A Smear group (a group layer): its children are the art, shown posed by the recipe and smeared
    // along the motion (core/Smear). While a layer inside it is being painted the editor sets
    // `smearAsDrawn`, so the art shows as drawn and the brush lands where it is drawn (not saved).
    std::optional<SmearRecipe> smear;
    bool smearAsDrawn = false;
    bool isSmear() const { return kind == LayerKind::Group && smear.has_value(); }
    // Group layers: children from bottom to top.
    std::vector<std::unique_ptr<Layer>> children;
    Layer* parent = nullptr;

    bool hasCels() const { return kind != LayerKind::Group; }
    CelRef cel(int frame) const
    {
        return frame >= 0 && frame < int(cels.size()) ? cels[size_t(frame)] : nullptr;
    }
};

struct Frame {
    int durationMs = 100;
};

struct Tag {
    std::string name;
    int from = 0;
    int to = 0;
    AniDirection direction = AniDirection::Forward;
    int repeat = 0; // 0 = infinite
    Rgba color;
    Bytes userData;
};

// Tilemap cels (PixelFormat::Tilemap32) hold a tile number per grid cell
// plus flips, as Aseprite writes them.
constexpr uint32_t kTileIdMask = 0x1fffffff;
constexpr uint32_t kTileFlipX = 0x80000000;
constexpr uint32_t kTileFlipY = 0x40000000;
constexpr uint32_t kTileFlipDiagonal = 0x20000000; // swaps x and y (with the flips: rotations)

// The tiles of tilemap layers (Layer::tilesetIndex picks one), from
// Aseprite's tileset chunk. Tile 0 is the empty tile.
struct Tileset {
    std::string name;
    int tileWidth = 16;
    int tileHeight = 16;
    int baseIndex = 1; // the number the first tile shows as
    std::vector<std::shared_ptr<Image>> tiles; // in the document's pixel format
    // As read: the file flags (1 external file, 2 tiles inside, 4 tile 0
    // empty, ...), the external file reference (flag 1, 8 bytes) and the
    // user data chunks after it (the tileset's, then the tiles').
    uint32_t flags = 2 | 4;
    Bytes external;
    std::vector<Bytes> userData;
};

// A slice: a named rectangle over the sprite (an area of a sheet, a 9-slice
// panel, a hitbox). Its keys give its bounds from a frame on, until the next
// key; a key with empty bounds hides it from that frame. `center` (relative
// to the bounds) is the 9-slice middle when `nineSlice`, `pivot` (relative
// to the bounds' origin) a point when `hasPivot`.
struct SliceKey {
    int frame = 0;
    Rect bounds;
    Rect center;
    Point pivot;
    bool operator==(const SliceKey&) const = default;
};

struct Slice {
    std::string name;
    std::vector<SliceKey> keys; // by frame, ascending
    bool nineSlice = false;
    bool hasPivot = false;
    Rgba color{0, 0, 255, 255}; // Aseprite's default slice color
    Bytes userData;             // as read (text, properties), color kept in `color`
    // The key in effect at `frame` (the last one at or before it), or null.
    const SliceKey* keyAt(int frame) const;
    bool operator==(const Slice&) const = default;
};

// File chunks the document doesn't model (tilesets, color profile,
// per-frame palettes, ...), written back verbatim. `bytes` holds `count`
// complete chunks including their headers.
struct OpaqueChunks {
    int frame = 0;
    bool beforeLayers = true; // appeared before the frame's layer/cel chunks
    int count = 0;
    Bytes bytes;
};

class Document {
public:
    Document(int width, int height, ColorMode mode = ColorMode::Rgba);

    int width() const { return m_width; }
    int height() const { return m_height; }
    Rect bounds() const { return {0, 0, m_width, m_height}; }
    ColorMode colorMode() const { return m_colorMode; }
    PixelFormat pixelFormat() const;
    // Only the canvas size; cels keep their images and positions.
    void setSize(int width, int height);
    // Only the mode; cel images must be converted with it (colorModeCommand).
    void setColorMode(ColorMode mode) { m_colorMode = mode; }

    Palette palette;
    int transparentIndex = 0; // indexed mode only
    std::vector<Tag> tags;

    Point pixelRatio{1, 1};
    Rect grid{0, 0, 16, 16};
    // Selected area in canvas pixels; empty means none. Not saved in files.
    Mask selection;
    Bytes userData;
    // Smalto's own: twice the drawing symmetry's crossing point, in pixels (kept in a chunk of its own
    // that other programs skip). Only set while a file is read or written; the editor owns the live value.
    std::optional<Point> symmetryCenter2;
    std::vector<OpaqueChunks> opaqueChunks;
    std::vector<Slice> slices;
    std::vector<Tileset> tilesets; // by tileset ID

    Layer& root() { return m_root; }
    const Layer& root() const { return m_root; }

    // Visits every layer (depth first, bottom to top), excluding the root.
    void forEachLayer(const std::function<void(Layer&)>& fn);
    void forEachLayer(const std::function<void(const Layer&)>& fn) const;

    std::unique_ptr<Layer> makeImageLayer(std::string name) const;
    std::unique_ptr<Layer> makeGroupLayer(std::string name) const;
    Layer* insertLayer(Layer& parent, int index, std::unique_ptr<Layer> layer);
    std::unique_ptr<Layer> takeLayer(Layer& parent, int index);

    int frameCount() const { return int(m_frames.size()); }
    const Frame& frame(int index) const { return m_frames[size_t(index)]; }
    Frame& frame(int index) { return m_frames[size_t(index)]; }

    // `cels` holds one entry per cel layer in forEachLayer order (missing = empty).
    // Tags shift with the frames: inserting inside a tag grows it, removing
    // its last frame removes the tag.
    void insertFrame(int index, Frame frame, const std::vector<CelRef>& cels = {});
    std::pair<Frame, std::vector<CelRef>> removeFrame(int index);
    // Moves frames [first, first + count) so the block starts at `newFirst`
    // (indices after the move). Tags stay on their frame numbers.
    void moveFrames(int first, int count, int newFirst);
    // Reverses the order of frames [first, last].
    void reverseFrames(int first, int last);
    // Cels of a frame in forEachLayer order, one per cel layer.
    std::vector<CelRef> celsAt(int frame) const;
    // Renders every live layer's cels again from its recipe (the frame operations above do it).
    void refreshLiveLayers();

private:
    void resizeCels(Layer& layer) const;

    int m_width;
    int m_height;
    ColorMode m_colorMode;
    Layer m_root{LayerKind::Group, "root"};
    std::vector<Frame> m_frames;
};

// Deep copy of a cel (new image), for duplicating frames.
CelRef cloneCel(const CelRef& cel);

// Deep copy of a layer and its children: new cels, with links between the
// copy's own frames kept. Not attached to a parent.
std::unique_ptr<Layer> cloneLayer(const Layer& layer);

// Deep copy of a whole document, exactly (layer UUIDs too, links between
// frames kept), e.g. to save it on another thread while editing goes on.
std::unique_ptr<Document> cloneDocument(const Document& doc);

// Every distinct cel image (linked cels counted once), e.g. to remap indexed
// pixels when the palette changes.
std::vector<std::shared_ptr<Image>> celImages(const Document& doc);

// The image with each pixel value v replaced by map[v] (values outside the
// map stay).
Image remapped(const Image& image, const std::vector<int>& map);

// True if `layer` is `ancestor` or inside it.
bool isWithin(const Layer& layer, const Layer& ancestor);

} // namespace core
