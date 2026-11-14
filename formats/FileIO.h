#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace formats {

// Writes next to the target, then replaces it, so a failed write never
// destroys an existing file.
bool writeFileAtomically(const std::filesystem::path& path, const std::vector<uint8_t>& data,
                         std::string* error);

} // namespace formats
