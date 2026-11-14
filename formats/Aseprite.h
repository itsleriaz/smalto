#pragma once

#include "core/Document.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

// .aseprite / .ase support, implemented from the public file format
// specification only (clean room).
namespace formats {

// Returns nullptr and sets `error` on failure.
std::unique_ptr<core::Document> readAseprite(std::span<const uint8_t> data, std::string* error = nullptr);
std::unique_ptr<core::Document> loadAseprite(const std::filesystem::path& path, std::string* error = nullptr);

std::vector<uint8_t> writeAseprite(const core::Document& doc);
bool saveAseprite(const core::Document& doc, const std::filesystem::path& path, std::string* error = nullptr);

} // namespace formats
