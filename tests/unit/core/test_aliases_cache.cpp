#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstring>
#include <string>
#include "core/aliases_cache.h"

// --------------------------------------------------------------
// alias_cache - init / destroy
// --------------------------------------------------------------
TEST_CASE("alias_cache_init - zero-initialized cache", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    SECTION("init returns non-NULL") {
        REQUIRE(cache != nullptr);
    }

    SECTION("count starts at 0") {
        REQUIRE(cache->count == 0);
    }

    SECTION("pool_used starts at 0") {
        REQUIRE(cache->pool_used == 0);
    }

    SECTION("pool starts zeroed") {
        for (size_t i = 0; i < ALIAS_CACHE_POOL_SIZE; i++) {
            REQUIRE(cache->pool[i] == '\0');
        }
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_destroy - accepts NULL", "[alias][core][unit]") {
    SECTION("destroy(NULL) does not crash") {
        REQUIRE_NOTHROW(alias_cache_destroy(nullptr));
    }
}

// --------------------------------------------------------------
// alias_cache_search
// --------------------------------------------------------------
TEST_CASE("alias_cache_search - empty cache returns NOT_FOUND", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    char mac[MAC_STR_BUF_SIZE] = {0};

    REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_ERR_NOT_FOUND);

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_search - reject null pointers", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    char mac[MAC_STR_BUF_SIZE] = {0};

    SECTION("alias == NULL - return ALIAS_ERR_NULL") {
        REQUIRE(alias_cache_search(nullptr, mac, cache) == ALIAS_ERR_NULL);
    }

    SECTION("mac == NULL - return ALIAS_ERR_NULL") {
        REQUIRE(alias_cache_search("PC", nullptr, cache) == ALIAS_ERR_NULL);
    }

    SECTION("self == NULL - return ALIAS_ERR_NULL") {
        REQUIRE(alias_cache_search("PC", mac, nullptr) == ALIAS_ERR_NULL);
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_search - returns MAC of inserted alias", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);

    char mac[MAC_STR_BUF_SIZE] = {0};
    int ret = alias_cache_search("PC", mac, cache);

    SECTION("return ALIAS_OK") {
        REQUIRE(ret == ALIAS_OK);
    }

    SECTION("MAC is copied with null terminator") {
        REQUIRE(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_search - MAC of max length (17 chars) fits buffer", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    REQUIRE(alias_cache_insert("TV", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);

    char mac[MAC_STR_BUF_SIZE];
    memset(mac, 0xAA, sizeof(mac));

    SECTION("copy does not overflow mac[MAC_STR_BUF_SIZE]") {
        REQUIRE_NOTHROW(alias_cache_search("TV", mac, cache));
        REQUIRE(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);
        REQUIRE(mac[17] == '\0');
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_search - binary search finds alias among many", "[alias][core][unit]") { 
    alias_cache_t* cache = alias_cache_init();

    const char* aliases[]  = {"Router", "SmartTV", "PC", "Printer", "Tablet",
                                "Cameras", "Laptop", "Headphones"};

    const char* macs[]     = {"11:22:33:44:55:66", "22:33:44:55:66:77",
                                "33:44:55:66:77:88", "44:55:66:77:88:99",
                                "55:66:77:88:99:AA", "66:77:88:99:AA:BB",
                                "77:88:99:AA:BB:CC", "88:99:AA:BB:CC:DD"};

    for (size_t i = 0; i < 8; i++) {
        REQUIRE(alias_cache_insert(aliases[i], macs[i], cache) == ALIAS_OK);
    }

    SECTION("every alias is found with its own MAC") {
        char mac[MAC_STR_BUF_SIZE];
        for (size_t i = 0; i < 8; i++) {
            REQUIRE(alias_cache_search(aliases[i], mac, cache) == ALIAS_OK);
            REQUIRE(strcmp(mac, macs[i]) == 0);
        }
    }

    SECTION("unknown aliases are not found") {
        char mac[MAC_STR_BUF_SIZE];
        REQUIRE(alias_cache_search("PC2", mac, cache) == ALIAS_ERR_NOT_FOUND);
        REQUIRE(alias_cache_search("PCX", mac, cache) == ALIAS_ERR_NOT_FOUND);
        REQUIRE(alias_cache_search("P", mac, cache) == ALIAS_ERR_NOT_FOUND);
        REQUIRE(alias_cache_search("PC ", mac, cache) == ALIAS_ERR_NOT_FOUND);
    }

    alias_cache_destroy(cache);
}

// --------------------------------------------------------------
// alias_cache_insert
// --------------------------------------------------------------
TEST_CASE("alias_cache_insert - basic insert returns ALIAS_OK", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    SECTION("single insert succeeds and updates count") {
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        REQUIRE(cache->count == 1);
    }
}

TEST_CASE("alias_cache_insert - reject null pointers", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    SECTION("alias == NULL - return ALIAS_ERR_NULL") {
        REQUIRE(alias_cache_insert(nullptr, "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_NULL);
    }

    SECTION("mac == NULL - return ALIAS_ERR_NULL") {
        REQUIRE(alias_cache_insert("PC", nullptr, cache) == ALIAS_ERR_NULL);
    }

    SECTION("self == NULL - return ALIAS_ERR_NULL") {
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", nullptr) == ALIAS_ERR_NULL);
    }

    SECTION("failed inserts leave cache untouched") {
        REQUIRE(cache->count == 0);
        REQUIRE(cache->pool_used == 0);
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_insert - reject invalid lengths", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    SECTION("alias longer than 31 chars - return ALIAS_ERR_INVALID_LEN") {
        std::string long_alias(ALIAS_MAX_LEN + 1, 'a');
        REQUIRE(alias_cache_insert(long_alias.c_str(), "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_INVALID_LEN);
    }

    SECTION("alias of exactly 31 chars is accepted") {
        std::string max_alias(ALIAS_MAX_LEN, 'a');
        REQUIRE(alias_cache_insert(max_alias.c_str(), "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        REQUIRE(cache->count == 1);
    }

    SECTION("MAC longer than 17 chars - return ALIAS_ERR_INVALID_LEN") {
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF:00", cache) == ALIAS_ERR_INVALID_LEN);
    }

    SECTION("MAC of exactly 17 chars is accepted") {
        REQUIRE(alias_cache_insert("TV", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        REQUIRE(cache->count == 1);
    }

    SECTION("empty alias - return ALIAS_ERR_INVALID_LEN") {
        REQUIRE(alias_cache_insert("", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_INVALID_LEN);
    }

    SECTION("rejected inserts do not consume pool") {
        size_t used_before = cache->pool_used;
        REQUIRE(alias_cache_insert(std::string(ALIAS_MAX_LEN + 1, 'a').c_str(),
                                    "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_INVALID_LEN);
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF:00", cache) == ALIAS_ERR_INVALID_LEN);
        REQUIRE(alias_cache_insert("", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_INVALID_LEN);
        REQUIRE(cache->pool_used == used_before);   
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_insert - reject duplicate alias", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("PC", "11:22:33:44:55:66", cache) == ALIAS_ERR_DUPLICATE);

    SECTION("original entry is preserved") {
        char mac[MAC_STR_BUF_SIZE];
        REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_OK);
        REQUIRE(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);
    }

    SECTION("count stays at 1") {
        REQUIRE(cache->count == 1);
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_insert - cache full at 16 entries", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    const char* aliases[] = {"Zeta", "Alpha", "Milo", "Kilo", "Tango", "Bravo",
                                "Echo", "Golf", "Julio", "Oscar", "Sierra", "Uniform",
                                "Hotel", "Lima", "November", "Papa"};

    SECTION("16 inserts all succeed") {
        for (int i = 0; i < ALIAS_CACHE_MAX_ENTRIES; i++) {
            REQUIRE(alias_cache_insert(aliases[i], "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        }
        REQUIRE(cache->count == ALIAS_CACHE_MAX_ENTRIES);
    }

    SECTION("17th insert - return ALIAS_ERR_FULL") {
        for (int i = 0; i < ALIAS_CACHE_MAX_ENTRIES; i++) {
            REQUIRE(alias_cache_insert(aliases[i], "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        }
        REQUIRE(alias_cache_insert("Yankee", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_FULL);
        REQUIRE(cache->count == ALIAS_CACHE_MAX_ENTRIES);
    }
}

TEST_CASE("alias_cache_insert - white-box: defensive pool guard (ALIAS_ERR_POOL_FULL)", "[alias][core][unit][whitebox]") {
    alias_cache_t* cache = alias_cache_init();

    SECTION("guard rejects the insert when the pool has no room") {

        cache->pool_used = ALIAS_CACHE_POOL_SIZE - 1;
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_POOL_FULL);
    }

    SECTION("a failed pool insert does not modify count nor pool_used") {
        cache->pool_used = ALIAS_CACHE_POOL_SIZE - 1;
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_ERR_POOL_FULL);
        REQUIRE(cache->count == 0);
        REQUIRE(cache->pool_used == ALIAS_CACHE_POOL_SIZE - 1);
    }
}

TEST_CASE("alias_cache_insert - keeps array sorted (binary-searchable)", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    REQUIRE(alias_cache_insert("Zebra", "00:00:00:00:00:01", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("Apple", "00:00:00:00:00:02", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("Mango", "00:00:00:00:00:03", cache) == ALIAS_OK);

    SECTION("entries are in ascending strcmp order") {
        for (size_t i = 1; i < cache->count; i++) {
            REQUIRE(strcmp(cache->entries[i - 1].alias, cache->entries[i].alias) < 0);
        }
    }

    SECTION("expected positions: Apple, Mango, Zebra") {
        REQUIRE(strcmp(cache->entries[0].alias, "Apple") == 0);
        REQUIRE(strcmp(cache->entries[1].alias, "Mango") == 0);
        REQUIRE(strcmp(cache->entries[2].alias, "Zebra") == 0);
    }

    SECTION("MACs follow their alias after shifts") {
        REQUIRE(strcmp(cache->entries[0].mac, "00:00:00:00:00:02") == 0);
        REQUIRE(strcmp(cache->entries[1].mac, "00:00:00:00:00:03") == 0);
        REQUIRE(strcmp(cache->entries[2].mac, "00:00:00:00:00:01") == 0);
    }

    alias_cache_destroy(cache);
}

// --------------------------------------------------------------
// pool accounting
// --------------------------------------------------------------
TEST_CASE("alias_cache - pool_used grows by alias+mac lengths with terminators", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    // "PC" = 2 + 1, "AA:BB:CC:DD:EE:FF" = 17 + 1
    REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);

    SECTION("pool_used == (2+1) + (17+1)") {
        REQUIRE(cache->pool_used == (size_t)((2 + 1) + (17 + 1)));
    }

    SECTION("16 max-size entries exactly exhaust the pool") {
        alias_cache_t* full = alias_cache_init();

        std::string base(ALIAS_MAX_LEN - 2, 'a');
        const char* suffixes[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
                                    "a8", "a9", "b0", "b1", "b2", "b3", "b4", "b5"};
        for (int i = 0; i < ALIAS_CACHE_MAX_ENTRIES; i++) {
            std::string alias = base + suffixes[i];
            REQUIRE(alias_cache_insert(alias.c_str(), "AA:BB:CC:DD:EE:FF", full) == ALIAS_OK);
        }
        REQUIRE(full->pool_used == ALIAS_CACHE_POOL_SIZE);

        REQUIRE(alias_cache_insert("Overflow", "AA:BB:CC:DD:EE:FF", full) == ALIAS_ERR_FULL);
        REQUIRE(full->count == ALIAS_CACHE_MAX_ENTRIES);
        alias_cache_destroy(full);
    }
}

// --------------------------------------------------------------
// find_insert_pos / binary_search (helpers)
// --------------------------------------------------------------
TEST_CASE("find_insert_pos - empty cache returns position 0", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();

    REQUIRE(find_insert_pos("Anything", cache) == 0);

    alias_cache_destroy(cache);
}

TEST_CASE("find_insert_pos - lower bound on a populated cache", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("B", "00:00:00:00:00:01", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("D", "00:00:00:00:00:02", cache) == ALIAS_OK);

    SECTION("before first - 0") {
        REQUIRE(find_insert_pos("A", cache) == 0);
    }

    SECTION("between B and D - 1") {
        REQUIRE(find_insert_pos("C", cache) == 1);
    }

    SECTION("after last - count") {
        REQUIRE(find_insert_pos("Z", cache) == 2);
    }

    SECTION("existing alias - exact position (lower bound)") {
        REQUIRE(find_insert_pos("B", cache) == 0);
        REQUIRE(find_insert_pos("D", cache) == 1);
    }

    SECTION("prefix of an existing alias - position after it") {
        REQUIRE(find_insert_pos("DD", cache) == 2);
    }

    alias_cache_destroy(cache);
}

TEST_CASE("binary_search helper - returns entry pointer or NULL", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("Mid", "00:00:00:00:00:05", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("Top", "00:00:00:00:00:06", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("Low", "00:00:00:00:00:07", cache) == ALIAS_OK);

    SECTION("found: pointer references the right alias") {
        alias_entry_t* found = binary_search("Mid", cache);
        REQUIRE(found != nullptr);
        REQUIRE(strcmp(found->alias, "Mid") == 0);
        REQUIRE(strcmp(found->mac, "00:00:00:00:00:05") == 0);
    }

    SECTION("not found: NULL") {
        REQUIRE(binary_search("Missing", cache) == nullptr);
    }

    alias_cache_destroy(cache);
}

// --------------------------------------------------------------
// alias_cache_clear
// --------------------------------------------------------------
TEST_CASE("alias_cache_clear - resets counters only (count, pool_used)", "[alias][core][unit]") {
    alias_cache_t* cache = alias_cache_init();
    REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
    REQUIRE(alias_cache_insert("TV", "11:22:33:44:55:66", cache) == ALIAS_OK);

    alias_cache_clear(cache);

    SECTION("count and pool_used are 0") {
        REQUIRE(cache->count == 0);
        REQUIRE(cache->pool_used == 0);
    }

    SECTION("old data is no longer searchable (count == 0 bounds the search)") {
        char mac[MAC_STR_BUF_SIZE];
        REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_ERR_NOT_FOUND);
        REQUIRE(alias_cache_search("TV", mac, cache) == ALIAS_ERR_NOT_FOUND);
    }

    SECTION("pool bytes are NOT zeroed (stale data remains)") {
        REQUIRE(cache->pool[0] == 'P');
        REQUIRE(cache->pool[1] == 'C');
        REQUIRE(cache->pool[2] == '\0');
    }

    SECTION("cache can be reused after clear (pool is reused from offset 0)") {
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        char mac[MAC_STR_BUF_SIZE];
        REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_OK);
        REQUIRE(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);
    }

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_clear - accepts NULL", "[alias][core][unit]") {
    SECTION("clear(NULL) does not crash") {
        REQUIRE_NOTHROW(alias_cache_clear(nullptr));
    }
}
