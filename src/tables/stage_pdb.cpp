#include "tables/stage_pdb.hpp"

#include <fstream>
#include <filesystem>

namespace rubik::tables {

bool writePdbFile(const std::filesystem::path& path,
                  const std::vector<std::uint8_t>& packed_data) {
    std::error_code ec;
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(packed_data.data()),
              static_cast<std::streamsize>(packed_data.size()));
    if (!ofs) return false;
    ofs.close();
    return true;
}

std::vector<std::uint8_t> readPdbFile(const std::filesystem::path& path,
                                      std::size_t expected_entries) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return {};

    std::size_t file_size = static_cast<std::size_t>(ifs.tellg());
    std::size_t expected_bytes = (expected_entries + 1) / 2;
    if (file_size != expected_bytes) return {};

    std::vector<std::uint8_t> data(expected_bytes);
    ifs.seekg(0);
    ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(expected_bytes));
    if (!ifs) return {};

    return data;
}

}  // namespace rubik::tables