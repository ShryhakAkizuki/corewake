#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "adapters/raspberry_pi/loader_linux.hpp"
#include "fixtures/aliases.h"
#include "util/temp_dir.hpp"

using corewake::test::kEntryCount;
using corewake::test::kExpected;
using corewake::test::kFixtureContent;
using corewake::test::TempDir;

// ==========================================================
// LoaderLinux - create / validación de ruta
// ==========================================================

TEST_CASE("create - explicit path", "[loader_linux][pi][unit]") {
    TempDir td;

    SECTION("valid INI returns instance") {
        const auto p = td.write_ini(kFixtureContent);
        LoaderLinux* l = LoaderLinux::create(p.c_str());
        REQUIRE(l != nullptr);
        delete l;
    }

    SECTION("non-existent file returns nullptr") {
        REQUIRE(LoaderLinux::create((td.dir / "nope.INI").c_str()) == nullptr);
    }

    SECTION("empty path falls back to <exe_dir>/aliases/aliases.INI") {
        LoaderLinux* l = LoaderLinux::create("");
        REQUIRE(l != nullptr);
        delete l;
    }
}

TEST_CASE("create - validation rejects malformed INI", "[loader_linux][pi][unit]") {
    TempDir td;

    auto bad = [&](const std::string& content) {
        const auto p = td.write_ini(content);
        REQUIRE(LoaderLinux::create(p.c_str()) == nullptr);
    };

    SECTION("missing [aliases] section") {
        bad("PC=00:11:22:33:44:55\n");
    }

    SECTION("entry before [aliases] section") {
        bad("PC=00:11:22:33:44:55\n[aliases]\n");
    }

    SECTION("wrong section name") {
        bad("[alias]\nPC=00:11:22:33:44:55\n");
    }

    SECTION("duplicate [aliases] section") {
        bad("[aliases]\n[aliases]\n");
    }

    SECTION("invalid MAC - 16 chars (missing last group)") {
        bad("[aliases]\nPC=00:11:22:33:44:5\n");
    }

    SECTION("invalid MAC - non-hex char") {
        bad("[aliases]\nPC=GG:11:22:33:44:55\n");
    }

    SECTION("missing '=' separator") {
        bad("[aliases]\nPC 00:11:22:33:44:55\n");
    }

    SECTION("empty alias") {
        bad("[aliases]\n=00:11:22:33:44:55\n");
    }

    SECTION("empty file") {
        bad("");
    }
}

// ==========================================================
// LoaderLinux - fetch
// ==========================================================

TEST_CASE("fetch - known aliases return MAC", "[loader_linux][pi][unit]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);
    LoaderLinux* l = LoaderLinux::create(p.c_str());
    REQUIRE(l != nullptr);

    SECTION("PC") {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch("PC", mac, l->vtable()) == LOADER_OK);
        REQUIRE(std::strcmp(mac, "00:11:22:33:44:55") == 0);
    }

    SECTION("TV with padding spaces is trimmed") {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch("TV", mac, l->vtable()) == LOADER_OK);
        REQUIRE(std::strcmp(mac, "aa:bb:cc:dd:ee:ff") == 0);
    }

    SECTION("GUEST_1 - underscore alias") {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch("GUEST_1", mac, l->vtable()) == LOADER_OK);
        REQUIRE(std::strcmp(mac, "DE:AD:BE:EF:00:01") == 0);
    }

    SECTION("31-char alias (ALIAS_MAX_LEN)") {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch("A234567890123456789012345678901", mac, l->vtable()) == LOADER_OK);
        REQUIRE(std::strcmp(mac, "0A:0B:0C:0D:0E:0F") == 0);
    }

    SECTION("unknown alias returns LOADER_ERR_NOT_FOUND") {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch("GHOST", mac, l->vtable()) == LOADER_ERR_NOT_FOUND);
    }

    SECTION("null args return LOADER_ERR_NULL") {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch(nullptr, mac, l->vtable()) == LOADER_ERR_NULL);
        REQUIRE(l->vtable()->fetch("PC", nullptr, l->vtable()) == LOADER_ERR_NULL);
        REQUIRE(l->vtable()->fetch("PC", mac, nullptr) == LOADER_ERR_NULL);
    }

    delete l;
}

// ==========================================================
// LoaderLinux - first/next iteration
// ==========================================================

TEST_CASE("first/next - iterate all entries in file order", "[loader_linux][pi][unit]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);
    LoaderLinux* l = LoaderLinux::create(p.c_str());
    REQUIRE(l != nullptr);

    int n = 0;
    char alias[32];
    char mac[18];

    REQUIRE(l->vtable()->first(alias, mac, l->vtable()) == LOADER_OK);
    do {
        REQUIRE(std::strcmp(alias, kExpected[n].alias) == 0);
        REQUIRE(std::strcmp(mac, kExpected[n].mac) == 0);
        ++n;
    } while (l->vtable()->next(alias, mac, l->vtable()) == LOADER_OK);

    REQUIRE(n == kEntryCount);

    delete l;
}

TEST_CASE("first/next - comments and blank lines are skipped", "[loader_linux][pi][unit]") {
    TempDir td;
    const auto p = td.write_ini(
        "# comment\n"
        ";\n"
        "\n"
        "[aliases]\n"
        "# another comment\n"
        "PC=00:11:22:33:44:55\n"
        "\n"
        "TV=aa:bb:cc:dd:ee:ff\n");
    LoaderLinux* l = LoaderLinux::create(p.c_str());
    REQUIRE(l != nullptr);

    char alias[32];
    char mac[18];

    REQUIRE(l->vtable()->first(alias, mac, l->vtable()) == LOADER_OK);
    REQUIRE(std::strcmp(alias, "PC") == 0);

    REQUIRE(l->vtable()->next(alias, mac, l->vtable()) == LOADER_OK);
    REQUIRE(std::strcmp(alias, "TV") == 0);

    REQUIRE(l->vtable()->next(alias, mac, l->vtable()) == LOADER_END);

    delete l;
}

TEST_CASE("first/next - empty section ends immediately", "[loader_linux][pi][unit]") {
    TempDir td;
    const auto p = td.write_ini("[aliases]\n");
    LoaderLinux* l = LoaderLinux::create(p.c_str());
    REQUIRE(l != nullptr);

    char alias[32];
    char mac[18];
    REQUIRE(l->vtable()->first(alias, mac, l->vtable()) == LOADER_END);

    delete l;
}

TEST_CASE("first/next - first() restarts iteration", "[loader_linux][pi][unit]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);
    LoaderLinux* l = LoaderLinux::create(p.c_str());
    REQUIRE(l != nullptr);

    char alias[32];
    char mac[18];

    REQUIRE(l->vtable()->first(alias, mac, l->vtable()) == LOADER_OK);
    REQUIRE(std::strcmp(alias, "PC") == 0);
    REQUIRE(l->vtable()->next(alias, mac, l->vtable()) == LOADER_OK);
    REQUIRE(std::strcmp(alias, "TV") == 0);

    REQUIRE(l->vtable()->first(alias, mac, l->vtable()) == LOADER_OK);
    REQUIRE(std::strcmp(alias, "PC") == 0);

    delete l;
}

// ==========================================================
// LoaderLinux - fixture generado en build
// ==========================================================

TEST_CASE("fixture - default build-time INI is readable", "[loader_linux][pi][unit]") {
    const std::string p = COREWAKE_DEFAULT_INI;
    REQUIRE_FALSE(p.empty());

    LoaderLinux* l = LoaderLinux::create(p.c_str());
    REQUIRE(l != nullptr);

    for (int i = 0; i < kEntryCount; ++i) {
        char mac[18] = {0};
        REQUIRE(l->vtable()->fetch(kExpected[i].alias, mac, l->vtable()) == LOADER_OK);
        REQUIRE(std::strcmp(mac, kExpected[i].mac) == 0);
    }

    char alias[32];
    char mac[18];
    int n = 0;
    REQUIRE(l->vtable()->first(alias, mac, l->vtable()) == LOADER_OK);
    do { ++n; } while (l->vtable()->next(alias, mac, l->vtable()) == LOADER_OK);
    REQUIRE(n == kEntryCount);

    delete l;
}
