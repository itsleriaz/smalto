#pragma once

#include "core/Document.h"
#include "core/Transform.h"
#include "core/Undo.h"

namespace core {

// Swaps a rectangular region of an image between two states.
class ImagePatchCommand : public Command {
public:
    ImagePatchCommand(std::shared_ptr<Image> target, Point at, Image before, Image after,
                      std::string label);
    void undo(Document&) override;
    void redo(Document&) override;
    std::string label() const override { return m_label; }
    size_t bytes() const override;

private:
    std::shared_ptr<Image> m_target;
    Point m_at;
    Image m_before;
    Image m_after;
    std::string m_label;
};

// Swaps the cel in a layer's frame slot (nullptr = empty), e.g. to create a
// cel on an empty frame or remove it again.
class SetCelCommand : public Command {
public:
    SetCelCommand(Layer& layer, int frame, CelRef before, CelRef after);
    void undo(Document&) override;
    void redo(Document&) override;
    std::string label() const override { return "Cel"; }
    size_t bytes() const override;

private:
    Layer& m_layer;
    int m_frame;
    CelRef m_before;
    CelRef m_after;
};

// Several commands as one undo step (undone in reverse order).
class CommandGroup : public Command {
public:
    CommandGroup(std::string label, std::vector<std::unique_ptr<Command>> commands);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return m_label; }
    size_t bytes() const override;
    bool changesContent() const override;

private:
    std::string m_label;
    std::vector<std::unique_ptr<Command>> m_commands;
};

// Moves a layer (with its children) to another place in the tree.
// `toIndex` counts after the layer has been taken out of its parent.
class MoveLayerCommand : public Command {
public:
    MoveLayerCommand(Layer& fromParent, int fromIndex, Layer& toParent, int toIndex);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "Move Layer"; }

private:
    Layer& m_fromParent;
    int m_fromIndex;
    Layer& m_toParent;
    int m_toIndex;
};

// Moves a cel (all frames linked to it move too).
// Replaces the picture and the place of some cels (a flip of several at once). The images are swapped as
// objects, so earlier commands that patch them still apply once this one is undone.
class ReplaceCelsCommand : public Command {
public:
    struct Change {
        CelRef cel;
        std::shared_ptr<Image> imageBefore;
        Point positionBefore;
        std::shared_ptr<Image> imageAfter;
        Point positionAfter;
    };
    ReplaceCelsCommand(std::vector<Change> changes, std::string label);
    void undo(Document&) override;
    void redo(Document&) override;
    std::string label() const override { return m_label; }
    size_t bytes() const override;

private:
    std::vector<Change> m_changes;
    std::string m_label;
};

class MoveCelCommand : public Command {
public:
    MoveCelCommand(CelRef cel, Point from, Point to);
    void undo(Document&) override;
    void redo(Document&) override;
    std::string label() const override { return "Move"; }

private:
    CelRef m_cel;
    Point m_from;
    Point m_to;
};

class InsertLayerCommand : public Command {
public:
    InsertLayerCommand(Layer& parent, int index, std::unique_ptr<Layer> layer);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "New Layer"; }
    size_t bytes() const override;

private:
    Layer& m_parent;
    int m_index;
    std::unique_ptr<Layer> m_layer; // owned while not in the document
};

class RemoveLayerCommand : public Command {
public:
    RemoveLayerCommand(Layer& parent, int index);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "Remove Layer"; }
    size_t bytes() const override;

private:
    Layer& m_parent;
    int m_index;
    std::unique_ptr<Layer> m_layer;
};

class InsertFrameCommand : public Command {
public:
    InsertFrameCommand(int index, Frame frame, std::vector<CelRef> cels = {});
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "New Frame"; }
    size_t bytes() const override;

private:
    int m_index;
    Frame m_frame;
    std::vector<CelRef> m_cels;
    std::vector<Tag> m_tags; // before, restored exactly on undo
};

class RemoveFrameCommand : public Command {
public:
    explicit RemoveFrameCommand(int index);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "Remove Frame"; }
    size_t bytes() const override;

private:
    int m_index;
    Frame m_frame;
    std::vector<CelRef> m_cels;
    std::vector<Tag> m_tags; // before, restored exactly on undo
};

// Moves a block of frames (see Document::moveFrames).
class MoveFramesCommand : public Command {
public:
    MoveFramesCommand(int first, int count, int newFirst);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "Move Frames"; }

private:
    int m_first;
    int m_count;
    int m_newFirst;
};

class ReverseFramesCommand : public Command {
public:
    ReverseFramesCommand(int first, int last);
    void undo(Document& doc) override { doc.reverseFrames(m_first, m_last); }
    void redo(Document& doc) override { doc.reverseFrames(m_first, m_last); }
    std::string label() const override { return "Reverse Frames"; }

private:
    int m_first;
    int m_last;
};

// A layer's own properties.
struct LayerProps {
    bool visible = true;
    bool locked = false;
    int opacity = 255;
    BlendMode blend = BlendMode::Normal;
    std::string name;
    bool alphaLock = false;

    static LayerProps of(const Layer& layer);
    void applyTo(Layer& layer) const;
    bool operator==(const LayerProps&) const = default;
};

class SetLayerPropsCommand : public Command {
public:
    SetLayerPropsCommand(Layer& layer, LayerProps before, LayerProps after, std::string label);
    void undo(Document&) override { m_before.applyTo(m_layer); }
    void redo(Document&) override { m_after.applyTo(m_layer); }
    std::string label() const override { return m_label; }
    bool mergeWith(const Command& next) override;

private:
    Layer& m_layer;
    LayerProps m_before;
    LayerProps m_after;
    std::string m_label;
};

// Changes a live layer's recipe; its cels are rendered again from it (both ways).
class SetParticlesCommand : public Command {
public:
    SetParticlesCommand(Layer& layer, ParticleRecipe before, ParticleRecipe after, std::string label);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return m_label; }
    bool mergeWith(const Command& next) override;

private:
    Layer& m_layer;
    ParticleRecipe m_before;
    ParticleRecipe m_after;
    std::string m_label;
};

// Changes a Smear group's recipe (poses, smear settings). Merges a drag into one step.
class SetSmearCommand : public Command {
public:
    SetSmearCommand(Layer& group, SmearRecipe before, SmearRecipe after, std::string label);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return m_label; }
    bool mergeWith(const Command& next) override;

private:
    Layer& m_group;
    SmearRecipe m_before;
    SmearRecipe m_after;
    std::string m_label;
};

// Moves a whole Smear group by (dx, dy): every cel of its layers (a linked one once), its pivot, and the
// emitter and fields of any live layer in it (drawn again). Its keys stay, so the motion moves exactly.
// Merges a drag into one step.
class MoveSmearGroupCommand : public Command {
public:
    MoveSmearGroupCommand(Layer& group, int dx, int dy);
    void undo(Document& doc) override { apply(doc, -m_dx, -m_dy); }
    void redo(Document& doc) override { apply(doc, m_dx, m_dy); }
    std::string label() const override { return "Move"; }
    bool mergeWith(const Command& next) override;

private:
    void apply(Document& doc, int dx, int dy);
    Layer& m_group;
    int m_dx;
    int m_dy;
};

// Makes a live layer an ordinary one: the recipe goes, the rendered cels stay as they are. `userData`
// is the layer's user data without the stored recipe (and with it, to undo).
class BakeLayerCommand : public Command {
public:
    BakeLayerCommand(Layer& layer, Bytes userDataWithout);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "Bake Layer"; }

private:
    Layer& m_layer;
    ParticleRecipe m_recipe;
    Bytes m_with;
    Bytes m_without;
};

// Settles a layer whose file pixels no longer matched its stored recipe (Layer::staleRecipe): render the
// effect again from the recipe (the pixels are replaced), or keep the pixels as an ordinary layer
// (`userDataWithout` is its user data without the stored recipe).
class ResolveStaleCommand : public Command {
public:
    ResolveStaleCommand(Layer& layer, bool render, Bytes userDataWithout);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return m_render ? "Render Particle Layer" : "Keep Pixels"; }
    size_t bytes() const override;

private:
    Layer& m_layer;
    bool m_render;
    ParticleRecipe m_recipe;
    std::vector<CelRef> m_cels;
    Bytes m_with;
    Bytes m_without;
};

// Sets the duration of some frames.
class SetFrameDurationsCommand : public Command {
public:
    // frame -> duration
    using Durations = std::vector<std::pair<int, int>>;
    SetFrameDurationsCommand(Durations before, Durations after);
    void undo(Document& doc) override { apply(doc, m_before); }
    void redo(Document& doc) override { apply(doc, m_after); }
    std::string label() const override { return "Frame Duration"; }
    bool mergeWith(const Command& next) override;

private:
    static void apply(Document& doc, const Durations& durations);
    Durations m_before;
    Durations m_after;
};

// Sets the document grid (cell size and offset). Consecutive edits merge.
class SetGridCommand : public Command {
public:
    SetGridCommand(Rect before, Rect after) : m_before(before), m_after(after) {}
    void undo(Document& doc) override { doc.grid = m_before; }
    void redo(Document& doc) override { doc.grid = m_after; }
    std::string label() const override { return "Grid"; }
    bool mergeWith(const Command& next) override;

private:
    Rect m_before;
    Rect m_after;
};

// Replaces the whole list of tilesets (one added for a new tilemap layer).
class SetTilesetsCommand : public Command {
public:
    SetTilesetsCommand(std::vector<Tileset> before, std::vector<Tileset> after, std::string label)
        : m_before(std::move(before)), m_after(std::move(after)), m_label(std::move(label)) {}
    void undo(Document& doc) override { doc.tilesets = m_before; }
    void redo(Document& doc) override { doc.tilesets = m_after; }
    std::string label() const override { return m_label; }

private:
    std::vector<Tileset> m_before;
    std::vector<Tileset> m_after;
    std::string m_label;
};

// Replaces one tileset (by ID) as a whole: tiles added, removed, changed.
class SetTilesetCommand : public Command {
public:
    SetTilesetCommand(size_t id, Tileset before, Tileset after, std::string label)
        : m_id(id), m_before(std::move(before)), m_after(std::move(after)), m_label(std::move(label)) {}
    void undo(Document& doc) override { doc.tilesets[m_id] = m_before; }
    void redo(Document& doc) override { doc.tilesets[m_id] = m_after; }
    std::string label() const override { return m_label; }

private:
    size_t m_id;
    Tileset m_before;
    Tileset m_after;
    std::string m_label;
};

// Replaces the slices (adding, editing, removing one). Merges with the next
// of the same label, so a drag undoes as one step.
class SetSlicesCommand : public Command {
public:
    SetSlicesCommand(std::vector<Slice> before, std::vector<Slice> after, std::string label)
        : m_before(std::move(before)), m_after(std::move(after)), m_label(std::move(label)) {}
    void undo(Document& doc) override { doc.slices = m_before; }
    void redo(Document& doc) override { doc.slices = m_after; }
    std::string label() const override { return m_label; }
    bool mergeWith(const Command& next) override;

private:
    std::vector<Slice> m_before;
    std::vector<Slice> m_after;
    std::string m_label;
};

// Replaces the palette (and the transparent index).
class SetPaletteCommand : public Command {
public:
    SetPaletteCommand(Palette before, int transparentBefore, Palette after, int transparentAfter, std::string label);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return m_label; }

private:
    Palette m_before;
    int m_transparentBefore;
    Palette m_after;
    int m_transparentAfter;
    std::string m_label;
};

// Replaces all tags.
class SetTagsCommand : public Command {
public:
    SetTagsCommand(std::vector<Tag> before, std::vector<Tag> after, std::string label);
    void undo(Document& doc) override { doc.tags = m_before; }
    void redo(Document& doc) override { doc.tags = m_after; }
    std::string label() const override { return m_label; }

private:
    std::vector<Tag> m_before;
    std::vector<Tag> m_after;
    std::string m_label;
};

// Sets the selection. Consecutive ones with the same label can merge (nudges).
class SetSelectionCommand : public Command {
public:
    SetSelectionCommand(Mask before, Mask after, std::string label);
    void undo(Document& doc) override { doc.selection = m_before; }
    void redo(Document& doc) override { doc.selection = m_after; }
    std::string label() const override { return m_label; }
    bool mergeWith(const Command& next) override;
    bool changesContent() const override { return false; }

private:
    Mask m_before;
    Mask m_after;
    std::string m_label;
};

// Changes the sprite size and swaps cel images and positions (sprite size,
// canvas size). Images are swapped as objects, so earlier commands that patch
// them still apply once this one is undone.
class ResizeSpriteCommand : public Command {
public:
    struct CelChange {
        CelRef cel;
        std::shared_ptr<Image> imageBefore;
        Point positionBefore;
        std::shared_ptr<Image> imageAfter;
        Point positionAfter;
    };
    // The grid and slices follow the size change too (`after` as they'll be).
    ResizeSpriteCommand(Point sizeBefore, Point sizeAfter, std::vector<CelChange> cels, std::string label,
                        Rect gridBefore = {}, Rect gridAfter = {}, std::vector<Slice> slicesBefore = {},
                        std::vector<Slice> slicesAfter = {}, std::vector<Tileset> tilesetsBefore = {},
                        std::vector<Tileset> tilesetsAfter = {});
    // A live layer's recipe (and one waiting for a decision, Layer::staleRecipe) moves with the artwork.
    struct LiveChange {
        Layer* layer = nullptr;
        std::optional<ParticleRecipe> liveBefore, liveAfter;
        std::optional<ParticleRecipe> staleBefore, staleAfter;
        std::optional<SmearRecipe> smearBefore, smearAfter; // a Smear group's
    };
    void setLiveChanges(std::vector<LiveChange> changes) { m_live = std::move(changes); }

    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return m_label; }
    size_t bytes() const override;

private:
    std::vector<LiveChange> m_live;
    Point m_sizeBefore; // width, height
    Point m_sizeAfter;
    std::vector<CelChange> m_cels;
    std::string m_label;
    Rect m_gridBefore, m_gridAfter;
    std::vector<Slice> m_slicesBefore, m_slicesAfter;
    std::vector<Tileset> m_tilesetsBefore, m_tilesetsAfter;
};

// Changes the color mode: swaps the mode, palette and every cel's image (as
// objects, like ResizeSpriteCommand). Built by colorModeCommand.
class SetColorModeCommand : public Command {
public:
    struct State {
        ColorMode mode;
        Palette palette;
        int transparentIndex;
        std::vector<Tileset> tilesets; // their tiles are in the mode's format too
    };
    struct CelChange {
        CelRef cel;
        std::shared_ptr<Image> imageBefore;
        std::shared_ptr<Image> imageAfter;
    };
    SetColorModeCommand(State before, State after, std::vector<CelChange> cels);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    std::string label() const override { return "Color Mode"; }
    size_t bytes() const override;

private:
    State m_before;
    State m_after;
    std::vector<CelChange> m_cels;
};

// Scales the whole sprite to width x height, nearest-neighbor: every cel's
// image and position scale by the same factors, so the picture keeps its
// layout. Tilemap cels (tile indices) only move.
std::unique_ptr<ResizeSpriteCommand> scaleSpriteCommand(const Document& doc, int width, int height,
                                                        Resample method = Resample::Nearest);

// Canvas size: `bounds` is the new canvas in current canvas coordinates
// (x / y < 0 grows to the left / top). Cels move with it and are cut to the
// new canvas; a cel left with nothing becomes one transparent pixel.
// Background layer cels cover the new canvas, new area `backgroundFill`.
// Tilemap cels only move.
std::unique_ptr<ResizeSpriteCommand> canvasSizeCommand(const Document& doc, Rect bounds, uint32_t backgroundFill);

} // namespace core
