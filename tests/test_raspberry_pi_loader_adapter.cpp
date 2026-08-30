#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/aliases_cache.h"
#include "adapters/raspberry_pi/loader_linux.hpp"

// The adapter owns a socket and is deliberately non-copyable and non-movable.
static_assert(!std::is_copy_constructible_v<LoaderLinux>, "LoaderLinux must stay non-copyable");
static_assert(!std::is_copy_assignable_v<LoaderLinux>,    "LoaderLinux must stay non-copyable");
static_assert(!std::is_move_constructible_v<LoaderLinux>, "LoaderLinux must stay non-movable");
static_assert(!std::is_move_assignable_v<LoaderLinux>,    "LoaderLinux must stay non-movable");

namespace {

struct ExpectedEntry {
    const char* alias;
    const char* mac;
};

constexpr ExpectedEntry kExpected[] = {
    { "PC",                             "00:11:22:33:44:55" },
    { "TV",                             "aa:bb:cc:dd:ee:ff" },
    { "ROBOT",                          "11:22:33:44:55:66" },
    { "GUEST_1",                        "DE:AD:BE:EF:00:01" },
    { "A234567890123456789012345678901", "0A:0B:0C:0D:0E:0F" }, // (ALIAS_MAX_LEN)
};
constexpr int kEntryCount = sizeof(kExpected) / sizeof(kExpected[0]);

const std::string kFixtureContent =
    "[aliases]\n"
    "PC=00:11:22:33:44:55\n"
    "TV = aa:bb:cc:dd:ee:ff\n"
    "ROBOT=11:22:33:44:55:66\n"
    "GUEST_1=DE:AD:BE:EF:00:01\n"
    "A234567890123456789012345678901=0A:0B:0C:0D:0E:0F\n";

struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        std::random_device rd;
        const std::uint64_t seed =
            (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint32_t>(rd());
        dir = std::filesystem::temp_directory_path() /
                ("corewake_loader_linux_" + std::to_string(seed));
        std::filesystem::create_directories(dir);
    }

    ~TempDir() {
        std::filesystem::remove_all(dir);
    }

    std::filesystem::path write(const std::string& name, const std::string& content) const {
        const std::filesystem::path p = dir / name;
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
        return p;
    }
};

bool iterate_all(loader_port_t* vt, std::vector<std::pair<std::string, std::string>>& out) {
    char alias[ALIAS_MAX_LEN + 1];
    char mac[MAC_STR_BUF_SIZE];
    alias[0] = '\0';
    mac[0]   = '\0';

    const int rc0 = vt->first(alias, mac, vt);
    if (rc0 == LOADER_END) return true;
    if (rc0 != LOADER_OK)  return false;

    out.emplace_back(alias, mac);
    int rc = rc0;
    while ((rc = vt->next(alias, mac, vt)) == LOADER_OK)
        out.emplace_back(alias, mac);

    return rc == LOADER_END;
}

void verify_default_fixture(LoaderLinux* loader) {
    loader_port_t* vt = loader->vtable();
    REQUIRE(vt != nullptr);

    char mac[MAC_STR_BUF_SIZE];

    for (const auto& e : kExpected) {
        mac[0] = '\0';
        REQUIRE(vt->fetch(e.alias, mac, vt) == LOADER_OK);
        REQUIRE(std::strcmp(mac, e.mac) == 0);
    }

    std::vector<std::pair<std::string, std::string>> seen;
    REQUIRE(iterate_all(vt, seen));
    REQUIRE(seen.size() == static_cast<std::size_t>(kEntryCount));
    for (int i = 0; i < kEntryCount; ++i) {
        REQUIRE(seen[i].first  == kExpected[i].alias);
        REQUIRE(seen[i].second == kExpected[i].mac);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// create() factory - success paths
// ---------------------------------------------------------------------------
TEST_CASE("create() - explicit path to a valid post-build format file", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("ok.ini", kFixtureContent);

    LoaderLinux* loader = nullptr;
    REQUIRE_NOTHROW(loader = LoaderLinux::create(p.c_str()));
    REQUIRE(loader != nullptr);
    REQUIRE(loader->vtable() != nullptr);
    REQUIRE(loader->vtable()->fetch != nullptr);
    REQUIRE(loader->vtable()->first != nullptr);
    REQUIRE(loader->vtable()->next  != nullptr);

    delete loader;
}

TEST_CASE("create() - valid format variants are accepted", "[loader_linux]") {
    TempDir td;

    const struct { const char* label; const char* content; } valid[] = {
        { "section with no entries",
            "[aliases]\n" },
        { "comments before and after the section",
            "; opening comment\n# another comment\n[aliases]\n# end\n" },
        { "padding in section and in entries",
            "  [aliases]  \n   PC  =  AA:BB:CC:DD:EE:FF  \n" },
        { "alias of 31 characters (maximum valid length)",
            "[aliases]\nA234567890123456789012345678901=AA:BB:CC:DD:EE:FF\n" },
        { "lowercase MAC",
            "[aliases]\nPC=aa:bb:cc:dd:ee:ff\n" },
    };

    for (std::size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        INFO("valid variant: " << valid[i].label);
        const auto p = td.write("valid_" + std::to_string(i) + ".ini", valid[i].content);
        LoaderLinux* loader = nullptr;
        REQUIRE_NOTHROW(loader = LoaderLinux::create(p.c_str()));
        REQUIRE(loader != nullptr);
        delete loader;
    }
}

TEST_CASE("create() - default path (nullptr or empty) loads <exe_dir>/aliases/aliases.INI", "[loader_linux]") {
    SECTION("file_path == nullptr") {
        LoaderLinux* loader = LoaderLinux::create(nullptr);
        if (loader == nullptr)
            SKIP("<exe_dir>/aliases/aliases.INI does not exist: the full CMake build is required");
        verify_default_fixture(loader);
        delete loader;
    }

    SECTION("file_path == empty string") {
        LoaderLinux* loader = LoaderLinux::create("");
        if (loader == nullptr)
            SKIP("<exe_dir>/aliases/aliases.INI does not exist: the full CMake build is required");
        verify_default_fixture(loader);
        delete loader;
    }
}

// ---------------------------------------------------------------------------
// create() factory - failure paths
// ---------------------------------------------------------------------------
TEST_CASE("create() - nonexistent path or directory return nullptr without throwing", "[loader_linux]") {
    TempDir td;

    SECTION("nonexistent file") {
        const auto p = td.dir / "missing" / "aliases.INI";
        LoaderLinux* loader = nullptr;
        REQUIRE_NOTHROW(loader = LoaderLinux::create(p.c_str()));
        CHECK(loader == nullptr);
    }

    SECTION("path points to a directory") {
        const auto d = td.dir / "i_am_a_directory";
        std::filesystem::create_directories(d);
        LoaderLinux* loader = nullptr;
        REQUIRE_NOTHROW(loader = LoaderLinux::create(d.c_str()));
        CHECK(loader == nullptr);
    }
}

TEST_CASE("create() - validate_file rejects every malformed format with nullptr", "[loader_linux]") {
    TempDir td;

    const struct { const char* label; const char* content; } bad[] = {
        { "empty file (no section)",
            "" },
        { "comments only, no section",
            "# nothing\n; nothing\n" },
        { "entry before the section",
            "PC=AA:BB:CC:DD:EE:FF\n[aliases]\n" },
        { "section with wrong name",
            "[alias]\nPC=AA:BB:CC:DD:EE:FF\n" },
        { "section with different casing",
            "[Aliases]\nPC=AA:BB:CC:DD:EE:FF\n" },
        { "duplicate section",
            "[aliases]\nPC=AA:BB:CC:DD:EE:FF\n[aliases]\nTV=AA:BB:CC:DD:EE:FF\n" },
        { "line without '='",
            "[aliases]\nPC\n" },
        { "empty alias",
            "[aliases]\n=AA:BB:CC:DD:EE:FF\n" },
        { "alias of 32 characters (exceeds ALIAS_MAX_LEN)",
            "[aliases]\nA2345678901234567890123456789012=AA:BB:CC:DD:EE:FF\n" },
        { "MAC with 5 groups",
            "[aliases]\nPC=AABB:CC:DD:EE:FF\n" },
        { "MAC with 7 groups",
            "[aliases]\nPC=AA:BB:CC:DD:EE:FF:00\n" },
        { "MAC with a single-digit byte",
            "[aliases]\nPC=A:BB:CC:DD:EE:FF\n" },
        { "MAC with non-hex characters",
            "[aliases]\nPC=GG:HH:II:JJ:KK:LL\n" },
        { "MAC with '-' separators",
            "[aliases]\nPC=AA-BB-CC-DD-EE-FF\n" },
        { "empty MAC",
            "[aliases]\nPC=\n" },
    };

    for (std::size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        INFO("invalid variant: " << bad[i].label);
        const auto p = td.write("bad_" + std::to_string(i) + ".ini", bad[i].content);
        LoaderLinux* loader = nullptr;
        REQUIRE_NOTHROW(loader = LoaderLinux::create(p.c_str()));
        CHECK(loader == nullptr);
    }
}

// ---------------------------------------------------------------------------
// vtable() - port contract
// ---------------------------------------------------------------------------
TEST_CASE("vtable() - stable pointer with the loader port fully wired", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("vt.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    loader_port_t* a = loader->vtable();
    loader_port_t* b = loader->vtable();

    REQUIRE(a != nullptr);
    REQUIRE(a == b);
    REQUIRE(a->fetch != nullptr);
    REQUIRE(a->first != nullptr);
    REQUIRE(a->next  != nullptr);

    delete loader;
}

// ---------------------------------------------------------------------------
// fetch() - alias -> MAC lookup
// ---------------------------------------------------------------------------
TEST_CASE("fetch() - resolves alias to exact MAC with LOADER_OK", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("fetch.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char mac[MAC_STR_BUF_SIZE];
    int rc = 0;

    SECTION("entry with padding resolves to its exact MAC") {
        mac[0] = '\0';
        REQUIRE_NOTHROW(rc = vt->fetch(kExpected[0].alias, mac, vt));
        REQUIRE(rc == LOADER_OK);
        REQUIRE(std::strcmp(mac, kExpected[0].mac) == 0);
    }

    SECTION("every fixture entry is found") {
        for (const auto& e : kExpected) {
            mac[0] = '\0';
            REQUIRE_NOTHROW(rc = vt->fetch(e.alias, mac, vt));
            REQUIRE(rc == LOADER_OK);
            REQUIRE(std::strcmp(mac, e.mac) == 0);
        }
    }

    SECTION("the MAC is returned with its exact case and length 17") {
        mac[0] = '\0';
        REQUIRE(vt->fetch(kExpected[2].alias, mac, vt) == LOADER_OK);
        REQUIRE(std::strcmp(mac, "aa:bb:cc:dd:ee:ff") == 0);
        REQUIRE(std::strlen(mac) == MAC_STR_MAX_LEN);
    }

    delete loader;
}

TEST_CASE("fetch() - missing alias returns LOADER_ERR_NOT_FOUND", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("fetch.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char mac[MAC_STR_BUF_SIZE];

    SECTION("unknown alias") {
        mac[0] = 'X';
        REQUIRE(vt->fetch("NO_SUCH_ALIAS", mac, vt) == LOADER_ERR_NOT_FOUND);
    }

    SECTION("alias lookup is case-sensitive (pc != PC)") {
        mac[0] = 'X';
        REQUIRE(vt->fetch("pc", mac, vt) == LOADER_ERR_NOT_FOUND);
    }

    SECTION("section with no entries") {
        const auto p2 = td.write("empty.ini", "[aliases]\n");
        LoaderLinux* l2 = LoaderLinux::create(p2.c_str());
        REQUIRE(l2 != nullptr);
        mac[0] = 'X';
        REQUIRE(l2->vtable()->fetch("PC", mac, l2->vtable()) == LOADER_ERR_NOT_FOUND);
        delete l2;
    }

    delete loader;
}

TEST_CASE("fetch() - null arguments return LOADER_ERR_NULL", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("fetch.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char mac[MAC_STR_BUF_SIZE];
    REQUIRE(vt->fetch(nullptr, mac, vt) == LOADER_ERR_NULL);
    REQUIRE(vt->fetch("PC", nullptr, vt) == LOADER_ERR_NULL);
    REQUIRE(vt->fetch("PC", mac, nullptr) == LOADER_ERR_NULL);

    delete loader;
}

TEST_CASE("fetch() - is stateless: repeatable and does not move the iterator", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("fetch.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char mac[MAC_STR_BUF_SIZE];
    char alias[ALIAS_MAX_LEN + 1];

    SECTION("repeated calls return the same result") {
        for (int i = 0; i < 3; ++i) {
            mac[0] = '\0';
            REQUIRE(vt->fetch("ROBOT", mac, vt) == LOADER_OK);
            REQUIRE(std::strcmp(mac, "11:22:33:44:55:66") == 0);
        }
    }

    SECTION("a fetch interleaved between first/next does not skip entries") {
        alias[0] = mac[0] = '\0';
        REQUIRE(vt->first(alias, mac, vt) == LOADER_OK);
        REQUIRE(std::strcmp(alias, kExpected[0].alias) == 0);

        // fetching entry #1 must not consume it in the iteration
        mac[0] = '\0';
        REQUIRE(vt->fetch(kExpected[1].alias, mac, vt) == LOADER_OK);

        REQUIRE(vt->next(alias, mac, vt) == LOADER_OK);
        REQUIRE(std::strcmp(alias, kExpected[1].alias) == 0);
        REQUIRE(std::strcmp(mac,   kExpected[1].mac)   == 0);
    }

    delete loader;
}

TEST_CASE("fetch() - file deleted at runtime returns LOADER_ERR_IO", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("tmp.ini", "[aliases]\nPC=AA:BB:CC:DD:EE:FF\n");
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    REQUIRE(std::filesystem::remove(p));

    char mac[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(loader->vtable()->fetch("PC", mac, loader->vtable()) == LOADER_ERR_IO);

    delete loader;
}

// ---------------------------------------------------------------------------
// first() / next() - file-order iteration
// ---------------------------------------------------------------------------
TEST_CASE("first()/next() - iterates the file in order until LOADER_END", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("iter.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char alias[ALIAS_MAX_LEN + 1];
    char mac[MAC_STR_BUF_SIZE];
    alias[0] = mac[0] = '\0';

    REQUIRE(vt->first(alias, mac, vt) == LOADER_OK);

    for (int i = 0; i < kEntryCount; ++i) {
        INFO("entry " << i);
        REQUIRE(std::strcmp(alias, kExpected[i].alias) == 0);
        REQUIRE(std::strcmp(mac,   kExpected[i].mac)   == 0);
        if (i + 1 < kEntryCount)
            REQUIRE(vt->next(alias, mac, vt) == LOADER_OK);
    }

    REQUIRE(vt->next(alias, mac, vt) == LOADER_END);

    delete loader;
}

TEST_CASE("first()/next() - LOADER_END persists and first() resets the iteration", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("iter.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char alias[ALIAS_MAX_LEN + 1];
    char mac[MAC_STR_BUF_SIZE];

    REQUIRE(vt->first(alias, mac, vt) == LOADER_OK);
    int rc = LOADER_OK;
    while (rc == LOADER_OK)
        rc = vt->next(alias, mac, vt);
    REQUIRE(rc == LOADER_END);

    REQUIRE(vt->next(alias, mac, vt) == LOADER_END);
    REQUIRE(vt->next(alias, mac, vt) == LOADER_END);

    alias[0] = mac[0] = '\0';
    REQUIRE(vt->first(alias, mac, vt) == LOADER_OK);
    REQUIRE(std::strcmp(alias, kExpected[0].alias) == 0);
    REQUIRE(std::strcmp(mac,   kExpected[0].mac)   == 0);

    delete loader;
}

TEST_CASE("first() - file with section but no entries returns LOADER_END", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("empty.ini", "[aliases]\n");
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    char alias[ALIAS_MAX_LEN + 1] = {0};
    char mac[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(loader->vtable()->first(alias, mac, loader->vtable()) == LOADER_END);

    delete loader;
}

TEST_CASE("first()/next() - null arguments return LOADER_ERR_NULL", "[loader_linux]") {
    TempDir td;
    const auto p = td.write("iter.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    char alias[ALIAS_MAX_LEN + 1];
    char mac[MAC_STR_BUF_SIZE];

    REQUIRE(vt->first(nullptr, mac, vt) == LOADER_ERR_NULL);
    REQUIRE(vt->first(alias, nullptr, vt) == LOADER_ERR_NULL);
    REQUIRE(vt->first(alias, mac, nullptr) == LOADER_ERR_NULL);
    REQUIRE(vt->next(nullptr, mac, vt) == LOADER_ERR_NULL);
    REQUIRE(vt->next(alias, nullptr, vt) == LOADER_ERR_NULL);
    REQUIRE(vt->next(alias, mac, nullptr) == LOADER_ERR_NULL);

    delete loader;
}

// ---------------------------------------------------------------------------
// End-to-end: post-build fixture loaded through the port into the core cache
// ---------------------------------------------------------------------------
TEST_CASE("end-to-end - the post-build fixture loads fully into alias_cache", "[loader_linux][integration]") {
    TempDir td;
    const auto p = td.write("fixture.ini", kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);
    loader_port_t* vt = loader->vtable();

    alias_cache_t* cache = alias_cache_init();
    REQUIRE(cache != nullptr);

    char alias[ALIAS_MAX_LEN + 1];
    char mac[MAC_STR_BUF_SIZE];
    alias[0] = mac[0] = '\0';

    int loaded = 0;
    int rc = vt->first(alias, mac, vt);
    while (rc == LOADER_OK) {
        REQUIRE(alias_cache_insert(alias, mac, cache) == ALIAS_OK);
        ++loaded;
        rc = vt->next(alias, mac, vt);
    }
    REQUIRE(rc == LOADER_END);
    REQUIRE(loaded == kEntryCount);
    REQUIRE(cache->count == kEntryCount);

    for (const auto& e : kExpected) {
        char out[MAC_STR_BUF_SIZE] = {0};
        REQUIRE(alias_cache_search(e.alias, out, cache) == ALIAS_OK);
        REQUIRE(std::strcmp(out, e.mac) == 0);
    }

    char out[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(alias_cache_search("NO_SUCH_ALIAS", out, cache) == ALIAS_ERR_NOT_FOUND);

    alias_cache_destroy(cache);
    delete loader;
}
