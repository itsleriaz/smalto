#pragma once

#include "core/Document.h"

namespace core {

// Renders the visible layers of `frame` into dst (RGBA8, document sized),
// touching only `area`. `asEdited`: as the editor's canvas shows it, where a Smear group whose art is
// being painted (Layer::smearAsDrawn) shows as drawn; otherwise (exports, previews) always posed.
void renderFrame(const Document& doc, int frame, Image& dst, Rect area, bool asEdited = false);
// What a group (its visible children, together) or a tilemap layer shows on `frame`, alone, into
// dst (RGBA8, document sized, cleared first) over `area`; the layer's own visibility, opacity and
// blend mode don't apply (a layer's thumbnail), the children's do. Nothing for an image layer.
void renderLayerContent(const Document& doc, const Layer& layer, int frame, Image& dst, Rect area);

// A Smear group's art as drawn on `frame` (its visible children, together, unposed) into dst (RGBA8,
// document sized, cleared first).
void renderSmearArt(const Document& doc, const Layer& group, int frame, Image& dst);

// Bake: an ordinary image layer holding what the Smear group shows on every frame (posed and smeared,
// in the document's format; frames that show nothing have no cel, identical neighbors share one), with
// the group's name, visibility, opacity and blend mode.
std::unique_ptr<Layer> bakeSmearGroup(const Document& doc, const Layer& group);

// The smallest rectangle holding every visible, non-transparent pixel of any
// frame's render (empty when there is none). What "trim to content" crops to.
Rect contentBounds(const Document& doc);

struct OnionSkin {
    int before = 1;      // frames shown before the current one
    int after = 1;       // ... and after it
    int opacity = 96;    // 0..255, for the nearest frame; farther ones fade
    bool tint = true;    // red before, blue after
    // Frames loop within [loopFrom, loopTo] (a tag) instead of running off its ends; -1: no loop.
    int loopFrom = -1;
    int loopTo = -1;
};

// Renders the frames around `frame` (not `frame` itself) into dst (RGBA8,
// document sized), touching only `area`: farthest first, so nearer frames
// are on top. `scratch` is a document-sized RGBA8 image used while rendering.
void renderOnionSkin(const Document& doc, int frame, const OnionSkin& onion, Image& dst, Rect area, Image& scratch);

// Converts one pixel of a cel image to RGBA using the document palette.
// On background layers the transparent index is an ordinary opaque color.
Rgba toRgba(const Document& doc, const Image& image, int x, int y, bool background = false);

// Pixel value in the document's format for a color (the inverse of toRgba).
// Grayscale uses luminance; indexed picks the nearest palette entry, and the
// transparent index only for fully transparent colors.
uint32_t fromRgba(const Document& doc, Rgba color);
// The same for a given mode and palette. On background layers (`background`)
// indexed may also pick the transparent index, which is opaque there.
uint32_t fromRgba(ColorMode mode, const Palette& palette, int transparentIndex, Rgba color,
                  bool background = false);

// An RGBA8 image in the document's pixel format.
Image convertToDocument(const Document& doc, const Image& rgba);

} // namespace core
