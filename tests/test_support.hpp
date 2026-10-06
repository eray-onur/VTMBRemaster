#pragma once

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::test {

inline std::vector<std::byte> bytes(std::string_view s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    return {p, p + s.size()};
}

inline void write_raw(const std::filesystem::path& file, const std::vector<std::byte>& data) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

inline std::vector<std::byte> read_raw(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    std::vector<std::byte> data(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

// A unique temporary directory removed on destruction.
class TempDir {
public:
    TempDir() {
        std::random_device rd;
        path_ = std::filesystem::temp_directory_path() / ("openbl-test-" + std::to_string(rd()) + std::to_string(rd()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// Real game install for integration tests, from OPENBL_GAME_DIR.
inline std::optional<std::filesystem::path> game_dir() {
    const char* env = std::getenv("OPENBL_GAME_DIR");
    if (!env || !*env) {
        return std::nullopt;
    }
    return std::filesystem::path(env);
}

} // namespace openbl::test
