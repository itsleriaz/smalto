#pragma once

#include "core/Document.h"

#include <cstdint>
#include <optional>

// How a live layer is kept in an .aseprite file: its rendered frames are ordinary cels (so Aseprite and
// engine importers see a normal layer), and the recipe is a nested property called "smalto" in the
// layer's user properties (Aseprite 1.3+ keeps them when it saves again), with a hash of the rendered
// cels. On reading, the layer is live again only when its cels still hash the same: pixels edited
// elsewhere leave it a normal layer (the recipe stays in its user data).
namespace formats {

struct StoredRecipe {
    core::ParticleRecipe recipe;
    uint64_t hash = 0; // of the cels the recipe rendered
};

// A hash of what the layer's cels show: the color of every visible pixel of every frame, by canvas
// position (not by how the cels are cropped, which a file may change).
uint64_t celsHash(const core::Document& doc, const core::Layer& layer);

// The layer's user data with the "smalto" property set (added, or replaced), other user data untouched.
// Data that can't be parsed is returned as it was.
core::Bytes withStoredRecipe(core::Bytes userData, const core::ParticleRecipe& recipe, uint64_t hash);
// The user data without the "smalto" property.
core::Bytes withoutStoredRecipe(core::Bytes userData);
std::optional<StoredRecipe> storedRecipeIn(const core::Bytes& userData);

// A Smear group's recipe, in the same "smalto" property of the group's user data (kind "smear"; a group
// has no cels, so no hash: other programs show the art as drawn).
core::Bytes withStoredSmear(core::Bytes userData, const core::SmearRecipe& recipe);
std::optional<core::SmearRecipe> storedSmearIn(const core::Bytes& userData);

// Makes the layers whose stored recipe still matches their cels live again, and groups with a stored
// smear Smear groups again.
void restoreLiveLayers(core::Document& doc);

} // namespace formats
