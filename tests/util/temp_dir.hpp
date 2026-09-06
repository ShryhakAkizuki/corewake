#pragma once

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace corewake::test {

struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        static std::atomic<unsigned long long> s_counter{0};
        dir = std::filesystem::temp_directory_path() /
                 ("corewake_test_" + std::to_string(::getpid()) + "_" +
                  std::to_string(s_counter.fetch_add(1)));
        std::filesystem::create_directories(dir);
    }

    ~TempDir() {
        std::filesystem::remove_all(dir);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    // Write a file at <dir>/<name>.
    std::filesystem::path write(const std::string& name, const std::string& content) const {
        const std::filesystem::path p = dir / name;
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
        return p;
    }

    // Write <dir>/aliases/aliases.INI (the layout LoaderLinux resolves by default).
    std::filesystem::path write_ini(const std::string& content) const {
        const std::filesystem::path subdir = dir / "aliases";
        std::filesystem::create_directories(subdir);
        const std::filesystem::path p = subdir / "aliases.INI";
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
        return p;
    }
};

} // namespace corewake::test
