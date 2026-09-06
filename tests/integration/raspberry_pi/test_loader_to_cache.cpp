#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "adapters/raspberry_pi/loader_linux.hpp"
#include "core/aliases_cache.h"
#include "core/translator.h"
#include "core/wol_packet.h"
#include "fixtures/aliases.h"
#include "util/temp_dir.hpp"

using corewake::test::kEntryCount;
using corewake::test::kExpected;
using corewake::test::kFixtureContent;
using corewake::test::TempDir;

namespace {

int stub_sender_count(const uint8_t* packet, size_t len, sender_port_t* self) {
    (void)packet; (void)len; (void)self;
    return 0;
}

sender_port_t stub_sender_port = { stub_sender_count };

} // namespace

TEST_CASE("preload - real LoaderLinux fills the cache with all entries", "[loader_to_cache][integration][pi]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);

    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(loader->vtable(), cache, wol);
    REQUIRE(tr != nullptr);

    REQUIRE(translator_preload(tr) == TRANSLATOR_OK);

    for (int i = 0; i < kEntryCount; ++i) {
        char mac[MAC_STR_BUF_SIZE] = {0};
        REQUIRE(alias_cache_search(kExpected[i].alias, mac, cache) == ALIAS_OK);
        REQUIRE(std::strcmp(mac, kExpected[i].mac) == 0);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
    delete loader;
}

TEST_CASE("resolve_alias - full cache: resolves without touching the loader", "[loader_to_cache][integration][pi]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);

    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(loader->vtable(), cache, wol);
    REQUIRE(tr != nullptr);
    REQUIRE(translator_preload(tr) == TRANSLATOR_OK);

    char mac[MAC_STR_BUF_SIZE] = {0};

    SECTION("PC") {
        REQUIRE(resolve_alias("PC", mac, tr) == TRANSLATOR_OK);
        REQUIRE(std::strcmp(mac, "00:11:22:33:44:55") == 0);
    }

    SECTION("TV - MAC in lowercase as it is in the INI") {
        REQUIRE(resolve_alias("TV", mac, tr) == TRANSLATOR_OK);
        REQUIRE(std::strcmp(mac, "aa:bb:cc:dd:ee:ff") == 0);
    }

    SECTION("GUEST_1") {
        REQUIRE(resolve_alias("GUEST_1", mac, tr) == TRANSLATOR_OK);
        REQUIRE(std::strcmp(mac, "DE:AD:BE:EF:00:01") == 0);
    }

    SECTION("nonexistent alias -> TRANSLATOR_ERR_NOT_FOUND") {
        REQUIRE(resolve_alias("GHOST", mac, tr) == TRANSLATOR_ERR_NOT_FOUND);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
    delete loader;
}

TEST_CASE("resolve_alias - empty cache: the loader responds and the entry is cached", "[loader_to_cache][integration][pi]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);

    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(loader->vtable(), cache, wol);
    REQUIRE(tr != nullptr);

    char mac[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(alias_cache_search("ROBOT", mac, cache) == ALIAS_ERR_NOT_FOUND);

    REQUIRE(resolve_alias("ROBOT", mac, tr) == TRANSLATOR_OK);
    REQUIRE(std::strcmp(mac, "11:22:33:44:55:66") == 0);

    char mac2[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(alias_cache_search("ROBOT", mac2, cache) == ALIAS_OK);
    REQUIRE(std::strcmp(mac2, "11:22:33:44:55:66") == 0);

    char mac3[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(resolve_alias("ROBOT", mac3, tr) == TRANSLATOR_OK);
    REQUIRE(std::strcmp(mac3, "11:22:33:44:55:66") == 0);

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
    delete loader;
}

TEST_CASE("receive_request - translates a real alias and 'wakes' it via WOL", "[loader_to_cache][integration][pi]") {
    TempDir td;
    const auto p = td.write_ini(kFixtureContent);

    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(loader->vtable(), cache, wol);
    REQUIRE(tr != nullptr);
    REQUIRE(translator_preload(tr) == TRANSLATOR_OK);

    SECTION("known alias -> TRANSLATOR_OK") {
        REQUIRE(receive_request("PC", tr) == TRANSLATOR_OK);
    }

    SECTION("nonexistent alias -> TRANSLATOR_ERR_NOT_FOUND") {
        REQUIRE(receive_request("GHOST", tr) == TRANSLATOR_ERR_NOT_FOUND);
    }

    SECTION("NULL alias -> TRANSLATOR_ERR_NULL") {
        REQUIRE(receive_request(nullptr, tr) == TRANSLATOR_ERR_NULL);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
    delete loader;
}

TEST_CASE("preload - empty INI: OK and empty cache", "[loader_to_cache][integration][pi]") {
    TempDir td;
    const auto p = td.write_ini("[aliases]\n");

    LoaderLinux* loader = LoaderLinux::create(p.c_str());
    REQUIRE(loader != nullptr);

    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(loader->vtable(), cache, wol);
    REQUIRE(tr != nullptr);

    REQUIRE(translator_preload(tr) == TRANSLATOR_OK);

    char mac[MAC_STR_BUF_SIZE] = {0};
    REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_ERR_NOT_FOUND);
    REQUIRE(resolve_alias("PC", mac, tr) == TRANSLATOR_ERR_NOT_FOUND);

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
    delete loader;
}
