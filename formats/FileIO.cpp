#include "formats/FileIO.h"

#include <fstream>

namespace formats {

bool writeFileAtomically(const std::filesystem::path& path, const std::vector<uint8_t>& data,
                         std::string* error)
{
    auto fail = [&](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
            return fail("cannot write file");
        file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!file)
            return fail("cannot write file");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return fail("cannot replace file");
    }
    return true;
}

} // namespace formats
