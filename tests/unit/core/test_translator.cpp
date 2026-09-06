#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstring>
#include "core/translator.h"
#include "core/wol_packet.h"
#include "core/aliases_cache.h"

// --------------------------------------------------------------
// translator - stubs
// --------------------------------------------------------------
namespace {

int stub_loader_fetch_result = LOADER_OK;
const char* stub_loader_fetch_mac = "11:22:33:44:55:66";

int stub_loader_count = 0;
int stub_loader_index = 0;
int stub_loader_next_result = LOADER_END;

const char* stub_loader_aliases[4] = { "PC", "TV", "ROBOT", "PRINTER" };
const char* stub_loader_macs[4]    = { "AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66",
                                        "22:33:44:55:66:77", "33:44:55:66:77:88" };

int stub_loader_fetch(const char* alias, char* mac, loader_port_t* self) {
    (void)alias; (void)self;
    if (stub_loader_fetch_result == LOADER_OK) {
        strcpy(mac, stub_loader_fetch_mac);
    }
    return stub_loader_fetch_result;
}

int stub_loader_first(char* alias, char* mac, loader_port_t* self) {
    (void)self;
    stub_loader_index = 0;
    if (stub_loader_index < stub_loader_count) {
        strcpy(alias, stub_loader_aliases[stub_loader_index]);
        strcpy(mac, stub_loader_macs[stub_loader_index]);
        stub_loader_index++;
        return LOADER_OK;
    }
    return LOADER_END;
}

int stub_loader_next(char* alias, char* mac, loader_port_t* self) {
    (void)self;
    if (stub_loader_index < stub_loader_count) {
        strcpy(alias, stub_loader_aliases[stub_loader_index]);
        strcpy(mac, stub_loader_macs[stub_loader_index]);
        stub_loader_index++;
        return LOADER_OK;
    }
    return stub_loader_next_result;
}

loader_port_t stub_loader_port = { stub_loader_fetch, stub_loader_first,
                                    stub_loader_next };

int stub_sender_result = 0;
int stub_sender_calls = 0;

int stub_sender_count(const uint8_t* packet, size_t len, sender_port_t* self) {
    (void)packet; (void)len; (void)self;
    stub_sender_calls++;
    return stub_sender_result;
}

sender_port_t stub_sender_port = { stub_sender_count };

} // namespace

// --------------------------------------------------------------
// translator - resolve_alias
// --------------------------------------------------------------
TEST_CASE("resolve_alias - reject null parameters", "[translator][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(&stub_loader_port, cache, wol);

    char mac[MAC_STR_BUF_SIZE];

    SECTION("alias == NULL - return TRANSLATOR_ERR_NULL") {
        REQUIRE(resolve_alias(nullptr, mac, tr) == TRANSLATOR_ERR_NULL);
    }

    SECTION("mac == NULL - return TRANSLATOR_ERR_NULL") {
        REQUIRE(resolve_alias("PC", nullptr, tr) == TRANSLATOR_ERR_NULL);
    }

    SECTION("self == NULL - return TRANSLATOR_ERR_NULL") {
        REQUIRE(resolve_alias("PC", mac, nullptr) == TRANSLATOR_ERR_NULL);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
}

TEST_CASE("resolve_alias - cache hit returns MAC", "[translator][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);

    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(&stub_loader_port, cache, wol);

    char mac[MAC_STR_BUF_SIZE] = {0};

    REQUIRE(resolve_alias("PC", mac, tr) == TRANSLATOR_OK);
    REQUIRE(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
}

TEST_CASE("resolve_alias - cache miss goes to loader", "[translator][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(&stub_loader_port, cache, wol);

    char mac[MAC_STR_BUF_SIZE] = {0};

    SECTION("fetch LOADER_OK - return TRANSLATOR_OK and cache the entry") {
        stub_loader_fetch_result = LOADER_OK;
        stub_loader_fetch_mac = "11:22:33:44:55:66";
        REQUIRE(resolve_alias("PC", mac, tr) == TRANSLATOR_OK);
        REQUIRE(strcmp(mac, "11:22:33:44:55:66") == 0);

        char mac2[MAC_STR_BUF_SIZE] = {0};
        REQUIRE(alias_cache_search("PC", mac2, cache) == ALIAS_OK);
        REQUIRE(strcmp(mac2, "11:22:33:44:55:66") == 0);
    }

    SECTION("fetch LOADER_ERR_NOT_FOUND - return TRANSLATOR_ERR_NOT_FOUND") {
        stub_loader_fetch_result = LOADER_ERR_NOT_FOUND;
        REQUIRE(resolve_alias("NOPE", mac, tr) == TRANSLATOR_ERR_NOT_FOUND);
    }

    SECTION("fetch LOADER_ERR_IO - return TRANSLATOR_ERR_LOADER") {
        stub_loader_fetch_result = LOADER_ERR_IO;
        REQUIRE(resolve_alias("NOPE", mac, tr) == TRANSLATOR_ERR_LOADER);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
}

// --------------------------------------------------------------
// translator - receive_request
// --------------------------------------------------------------
TEST_CASE("receive_request - full flow with preloaded alias", "[translator][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);

    stub_sender_result = 0;
    stub_sender_calls = 0;

    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(&stub_loader_port, cache, wol);

    SECTION("null parameters - return TRANSLATOR_ERR_NULL") {
        REQUIRE(receive_request(nullptr, tr) == TRANSLATOR_ERR_NULL);
        REQUIRE(receive_request("PC", nullptr) == TRANSLATOR_ERR_NULL);
    }

    SECTION("success - return TRANSLATOR_OK and sender called once") {
        REQUIRE(receive_request("PC", tr) == TRANSLATOR_OK);
        REQUIRE(stub_sender_calls == 1);
    }

    SECTION("unknown alias - propagate TRANSLATOR_ERR_NOT_FOUND") {
        stub_loader_fetch_result = LOADER_ERR_NOT_FOUND;
        REQUIRE(receive_request("NOPE", tr) == TRANSLATOR_ERR_NOT_FOUND);
        REQUIRE(stub_sender_calls == 0);
    }

    SECTION("loader failure - propagate TRANSLATOR_ERR_LOADER") {
        stub_loader_fetch_result = LOADER_ERR_IO;
        REQUIRE(receive_request("NOPE", tr) == TRANSLATOR_ERR_LOADER);
        REQUIRE(stub_sender_calls == 0);
    }

    SECTION("sender failure - map to TRANSLATOR_ERR_WOL") {
        stub_sender_result = -1;
        REQUIRE(receive_request("PC", tr) == TRANSLATOR_ERR_WOL);
    }

    SECTION("invalid cached MAC - map to TRANSLATOR_ERR_WOL") {
        REQUIRE(alias_cache_insert("BAD", "no-mac", cache) == ALIAS_OK);
        REQUIRE(receive_request("BAD", tr) == TRANSLATOR_ERR_WOL);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
}

// --------------------------------------------------------------
// translator - translator_preload
// --------------------------------------------------------------
TEST_CASE("translator_preload - fills cache from loader", "[translator][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    wol_packet_t* wol = wol_packet_create(&stub_sender_port);
    translator_t* tr = translator_create(&stub_loader_port, cache, wol);

    SECTION("self == NULL - return TRANSLATOR_ERR_NULL") {
        REQUIRE(translator_preload(nullptr) == TRANSLATOR_ERR_NULL);
    }

    SECTION("empty loader - return TRANSLATOR_OK with 0 entries") {
        stub_loader_count = 0;
        stub_loader_index = 0;
        stub_loader_next_result = LOADER_END;
        REQUIRE(translator_preload(tr) == TRANSLATOR_OK);
        REQUIRE(cache->count == 0);
    }

    SECTION("three entries - all cached and searchable") {
        stub_loader_count = 3;
        stub_loader_index = 0;
        stub_loader_next_result = LOADER_END;
        REQUIRE(translator_preload(tr) == TRANSLATOR_OK);
        REQUIRE(cache->count == 3);

        char mac[MAC_STR_BUF_SIZE];
        REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_OK);
        REQUIRE(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);
        REQUIRE(alias_cache_search("ROBOT", mac, cache) == ALIAS_OK);
        REQUIRE(strcmp(mac, "22:33:44:55:66:77") == 0);
    }

    SECTION("iterator IO failure - return TRANSLATOR_ERR_LOADER") {
        stub_loader_count = 1;
        stub_loader_index = 0;
        stub_loader_next_result = LOADER_ERR_IO;
        REQUIRE(translator_preload(tr) == TRANSLATOR_ERR_LOADER);
        REQUIRE(cache->count == 1);
    }

    translator_destroy(tr);
    wol_packet_destroy(wol);
    alias_cache_destroy(cache);
}
