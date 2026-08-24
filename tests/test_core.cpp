#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include "core/wol_packet.h"
#include "core/aliases_cache.h"

// --------------------------------------------------------------
// build_magic_packet()
// --------------------------------------------------------------
TEST_CASE("build_magic_packet - build packet", "[wol]") {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t packet[MAGIC_PACKET_SIZE] = {0};

    int ret = build_magic_packet(mac, packet);

    SECTION("return MAGIC_PACKET_SIZE") {
        REQUIRE(ret == MAGIC_PACKET_SIZE);
    }

    SECTION("First 6 bytes are 0xFF") {
        for (int i = 0; i < 6; i++) {
            REQUIRE(packet[i] == 0xFF);
        }
    }

    SECTION("MAC repeated 16 times starting at byte 6") {
        for (int i = 0; i < MAC_REPETITIONS; i++) {
            for (int j = 0; j < MAC_ADDRESS_SIZE; j++) {
                REQUIRE(packet[BROADCAST_BYTES + (i * MAC_ADDRESS_SIZE) + j] == mac[j]);
            }
        }
    }
}

TEST_CASE("build_magic_packet - reject null pointers", "[wol]") {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t packet[MAGIC_PACKET_SIZE] = {0};

    SECTION("mac == NULL - return WOL_ERR_NULL") {
        REQUIRE(build_magic_packet(NULL, packet) == WOL_ERR_NULL);
    }

    SECTION("packet == NULL - return WOL_ERR_NULL") {
        REQUIRE(build_magic_packet(mac, NULL) == WOL_ERR_NULL);
    }

    SECTION("Ambos NULL - return WOL_ERR_NULL") {
        REQUIRE(build_magic_packet(NULL, NULL) == WOL_ERR_NULL);
    }
}

TEST_CASE("build_magic_packet - MAC 00:00:00:00:00:00", "[wol]") {
    uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t packet[MAGIC_PACKET_SIZE] = {0};

    int ret = build_magic_packet(mac, packet);

    REQUIRE(ret == MAGIC_PACKET_SIZE);

    REQUIRE(packet[0] == 0xFF);

    for (int i = BROADCAST_BYTES; i < MAGIC_PACKET_SIZE; i++) {
        REQUIRE(packet[i] == 0x00);
    }
}

TEST_CASE("build_magic_packet - MAC 01:02:03:04:05:06", "[wol]") {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t packet[MAGIC_PACKET_SIZE] = {0};

    build_magic_packet(mac, packet);

    // First block - (bytes 6-11)
    REQUIRE(packet[6]  == 0x01);
    REQUIRE(packet[7]  == 0x02);
    REQUIRE(packet[8]  == 0x03);
    REQUIRE(packet[9]  == 0x04);
    REQUIRE(packet[10] == 0x05);
    REQUIRE(packet[11] == 0x06);

    // Last block - (bytes 96-101)
    REQUIRE(packet[96] == 0x01);
    REQUIRE(packet[97] == 0x02);
    REQUIRE(packet[98] == 0x03);
    REQUIRE(packet[99] == 0x04);
    REQUIRE(packet[100] == 0x05);
    REQUIRE(packet[101] == 0x06);
}

// --------------------------------------------------------------
// parse_mac_string()
// --------------------------------------------------------------
TEST_CASE("parse_mac_string - Uppercase MAC", "[wol]") {
    uint8_t mac[6];

    int ret = parse_mac_string("AA:BB:CC:DD:EE:FF", mac);

    REQUIRE(ret == WOL_OK);
    REQUIRE(mac[0] == 0xAA);
    REQUIRE(mac[1] == 0xBB);
    REQUIRE(mac[2] == 0xCC);
    REQUIRE(mac[3] == 0xDD);
    REQUIRE(mac[4] == 0xEE);
    REQUIRE(mac[5] == 0xFF);
}

TEST_CASE("parse_mac_string - Lowercase MAC", "[wol]") {
    uint8_t mac[6];

    int ret = parse_mac_string("aa:bb:cc:dd:ee:ff", mac);

    REQUIRE(ret == WOL_OK);
    REQUIRE(mac[0] == 0xAA);
    REQUIRE(mac[1] == 0xBB);
    REQUIRE(mac[2] == 0xCC);
    REQUIRE(mac[3] == 0xDD);
    REQUIRE(mac[4] == 0xEE);
    REQUIRE(mac[5] == 0xFF);
}

TEST_CASE("parse_mac_string - Mixedcase MAC", "[wol]") {
    uint8_t mac[6];

    int ret = parse_mac_string("Aa:Bb:Cc:Dd:Ee:Ff", mac);

    REQUIRE(ret == WOL_OK);
    REQUIRE(mac[0] == 0xAA);
    REQUIRE(mac[1] == 0xBB);
    REQUIRE(mac[2] == 0xCC);
    REQUIRE(mac[3] == 0xDD);
    REQUIRE(mac[4] == 0xEE);
    REQUIRE(mac[5] == 0xFF);
}

TEST_CASE("parse_mac_string - MAC 00:00:00:00:00:00", "[wol]") {
    uint8_t mac[6];
    int result = parse_mac_string("00:00:00:00:00:00", mac);
    REQUIRE(result == WOL_OK);
    REQUIRE(mac[0] == 0x00);
    REQUIRE(mac[5] == 0x00);
}

TEST_CASE("parse_mac_string - MAC FF:FF:FF:FF:FF:FF", "[wol]") {
    uint8_t mac[6];
    int result = parse_mac_string("FF:FF:FF:FF:FF:FF", mac);
    REQUIRE(result == WOL_OK);
    REQUIRE(mac[0] == 0xFF);
    REQUIRE(mac[5] == 0xFF);
}

TEST_CASE("parse_mac_string - MAC 01:23:45:67:89:AB", "[wol]") {
    uint8_t mac[6];
    int result = parse_mac_string("01:23:45:67:89:AB", mac);
    REQUIRE(result == WOL_OK);
    REQUIRE(mac[0] == 0x01);
    REQUIRE(mac[5] == 0xAB);
}

TEST_CASE("parse_mac_string - reject null pointers", "[wol]") {
    uint8_t mac[6];

    SECTION("str == NULL - return WOL_ERR_NULL") {
        REQUIRE(parse_mac_string(NULL, mac) == WOL_ERR_NULL);
    }

    SECTION("mac == NULL - return WOL_ERR_NULL") {
        REQUIRE(parse_mac_string("AA:BB:CC:DD:EE:FF", NULL) == WOL_ERR_NULL);
    }

    SECTION("Ambos NULL - return WOL_ERR_NULL") {
        REQUIRE(parse_mac_string(NULL, NULL) == WOL_ERR_NULL);
    }
}

TEST_CASE("parse_mac_string - reject invalid format", "[wol]") {
    uint8_t mac[6];

    SECTION("No colon") {
        REQUIRE(parse_mac_string("AABBCCDDEEFF", mac) == WOL_ERR_PARSE);
    }

    SECTION("Incomplete MAC - 3 bytes") {
        REQUIRE(parse_mac_string("AA:BB:CC", mac) == WOL_ERR_PARSE);
    }

    SECTION("Empty string") {
        REQUIRE(parse_mac_string("", mac) == WOL_ERR_PARSE);
    }

    SECTION("No hexadecimal char") {
        REQUIRE(parse_mac_string("GG:HH:II:JJ:KK:LL", mac) == WOL_ERR_PARSE);
    }

    SECTION("Only colon") {
        REQUIRE(parse_mac_string(":::::", mac) == WOL_ERR_PARSE);
    }

    SECTION("Trailing garbage after 6 bytes") {
        REQUIRE(parse_mac_string("AA:BB:CC:DD:EE:FF:00", mac) == WOL_ERR_PARSE);
    }

    SECTION("Trailing text after 6 bytes") {
        REQUIRE(parse_mac_string("AA:BB:CC:DD:EE:FF extra", mac) == WOL_ERR_PARSE);
    }
}

// --------------------------------------------------------------
// wol_packet_create / wol_packet_destroy / wake (stub port)
// --------------------------------------------------------------
namespace {

int stub_send_called = 0;
size_t stub_send_len = 0;
int stub_close_called = 0;

int stub_send(const uint8_t* packet, size_t len, sender_port_t* self) {
    (void)packet; (void)self;
    stub_send_called++;
    stub_send_len = len;
    return 0;
}

int stub_send_fail(const uint8_t* packet, size_t len, sender_port_t* self) {
    (void)packet; (void)self; (void)len;
    return -1;
}

void stub_close(sender_port_t* self) {
    (void)self;
    stub_close_called++;
}

} // namespace

TEST_CASE("wol_packet lifecycle - create/destroy with stub port", "[wol]") {
    sender_port_t port = {stub_send, stub_close};

    wol_packet_t* wp = wol_packet_create(&port);

    SECTION("create returns non-NULL") {
        REQUIRE(wp != NULL);
        wol_packet_destroy(wp);
    }

    SECTION("wake sends MAGIC_PACKET_SIZE bytes via the port") {
        REQUIRE(wp != NULL);
        int before = stub_send_called;
        REQUIRE(wake("AA:BB:CC:DD:EE:FF", wp) == WOL_OK);
        REQUIRE(stub_send_called - before == 1);
        REQUIRE(stub_send_len == (size_t)MAGIC_PACKET_SIZE);
        wol_packet_destroy(wp);
    }

    SECTION("destroy closes the port") {
        int before = stub_close_called;
        wol_packet_destroy(wp);
        REQUIRE(stub_close_called - before == 1);
    }
}

TEST_CASE("wol_packet lifecycle - invalid inputs", "[wol]") {
    SECTION("create with NULL port returns NULL") {
        REQUIRE(wol_packet_create(NULL) == NULL);
    }

    sender_port_t port = {stub_send, stub_close};
    wol_packet_t* wp = wol_packet_create(&port);

    SECTION("wake with NULL self returns WOL_ERR_NULL") {
        REQUIRE(wake("AA:BB:CC:DD:EE:FF", NULL) == WOL_ERR_NULL);
    }

    SECTION("wake with NULL str returns WOL_ERR_NULL") {
        REQUIRE(wake(NULL, wp) == WOL_ERR_NULL);
    }

    SECTION("wake with invalid MAC returns WOL_ERR_PARSE") {
        REQUIRE(wake("no-mac", wp) == WOL_ERR_PARSE);
    }

    wol_packet_destroy(wp);
}

TEST_CASE("wol_packet wake - send failure propagates port error", "[wol]") {
    sender_port_t port = {stub_send_fail, stub_close};
    wol_packet_t* wp = wol_packet_create(&port);

    REQUIRE(wake("AA:BB:CC:DD:EE:FF", wp) == -1);

    wol_packet_destroy(wp);
}

// --------------------------------------------------------------
// alias_cache - init / destroy
// --------------------------------------------------------------
TEST_CASE("alias_cache_init - zero-initialized cache", "[alias]") {
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

TEST_CASE("alias_cache_destroy - accepts NULL", "[alias]") {
    SECTION("destroy(NULL) does not crash") {
        REQUIRE_NOTHROW(alias_cache_destroy(nullptr));
    }
}

// --------------------------------------------------------------
// alias_cache_search
// --------------------------------------------------------------
TEST_CASE("alias_cache_search - empty cache returns NOT_FOUND", "[alias]") {
    alias_cache_t* cache = alias_cache_init();
    char mac[MAC_STR_BUF_SIZE] = {0};

    REQUIRE(alias_cache_search("PC", mac, cache) == ALIAS_ERR_NOT_FOUND);

    alias_cache_destroy(cache);
}

TEST_CASE("alias_cache_search - reject null pointers", "[alias]") {
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

TEST_CASE("alias_cache_search - returns MAC of inserted alias", "[alias]") {
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

TEST_CASE("alias_cache_search - MAC of max length (17 chars) fits buffer", "[alias]") {
    alias_cache_t* cache = alias_cache_init();
    // 17 chars: "AA:BB:CC:DD:EE:FF" -> exactly MAC_STR_MAX_LEN
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

TEST_CASE("alias_cache_search - binary search finds alias among many", "[alias]") {
    alias_cache_t* cache = alias_cache_init();

    const char* aliases[]  = {"Router", "SmartTV", "PC", "Impresora", "Tablet",
                                "Camaras", "Portatil", "Auriculares"};

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
TEST_CASE("alias_cache_insert - basic insert returns ALIAS_OK", "[alias]") {
    alias_cache_t* cache = alias_cache_init();

    SECTION("single insert succeeds and updates count") {
        REQUIRE(alias_cache_insert("PC", "AA:BB:CC:DD:EE:FF", cache) == ALIAS_OK);
        REQUIRE(cache->count == 1);
    }
}

TEST_CASE("alias_cache_insert - reject null pointers", "[alias]") {
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

TEST_CASE("alias_cache_insert - reject invalid lengths", "[alias]") {
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

TEST_CASE("alias_cache_insert - reject duplicate alias", "[alias]") {
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

TEST_CASE("alias_cache_insert - cache full at 16 entries", "[alias]") {
    alias_cache_t* cache = alias_cache_init();

    // 16 aliases en orden aleatorio
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

TEST_CASE("alias_cache_insert - white-box: defensive pool guard (ALIAS_ERR_POOL_FULL)", "[alias][whitebox]") {
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

TEST_CASE("alias_cache_insert - keeps array sorted (binary-searchable)", "[alias]") {
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
TEST_CASE("alias_cache - pool_used grows by alias+mac lengths with terminators", "[alias]") {
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
TEST_CASE("find_insert_pos - empty cache returns position 0", "[alias]") {
    alias_cache_t* cache = alias_cache_init();

    REQUIRE(find_insert_pos("Anything", cache) == 0);

    alias_cache_destroy(cache);
}

TEST_CASE("find_insert_pos - lower bound on a populated cache", "[alias]") {
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

TEST_CASE("binary_search helper - returns entry pointer or NULL", "[alias]") {
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
TEST_CASE("alias_cache_clear - resets counters only (count, pool_used)", "[alias]") {
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

TEST_CASE("alias_cache_clear - accepts NULL", "[alias]") {
    SECTION("clear(NULL) does not crash") {
        REQUIRE_NOTHROW(alias_cache_clear(nullptr));
    }
}
