#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include "core/wol_engine.h"

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
                REQUIRE(packet[6 + (i * MAC_ADDRESS_SIZE) + j] == mac[j]);
            }
        }
    }
}

TEST_CASE("build_magic_packet - reject null pointers", "[wol]") {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t packet[MAGIC_PACKET_SIZE] = {0};

    SECTION("mac == NULL - return -1") {
        REQUIRE(build_magic_packet(NULL, packet) == -1);
    }

    SECTION("packet == NULL - return -1") {
        REQUIRE(build_magic_packet(mac, NULL) == -1);
    }

    SECTION("Ambos NULL - return -1") {
        REQUIRE(build_magic_packet(NULL, NULL) == -1);
    }
}

TEST_CASE("build_magic_packet - MAC 00:00:00:00:00:00", "[wol]") {
    uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t packet[MAGIC_PACKET_SIZE] = {0};

    int ret = build_magic_packet(mac, packet);

    REQUIRE(ret == MAGIC_PACKET_SIZE);

    REQUIRE(packet[0] == 0xFF);

    for (int i = 6; i < MAGIC_PACKET_SIZE; i++) {
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

    // Last Block - (bytes 96-101)
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

    REQUIRE(ret == 0);
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

    REQUIRE(ret == 0);
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

    REQUIRE(ret == 0);
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
    REQUIRE(result == 0);
    REQUIRE(mac[0] == 0x00);
    REQUIRE(mac[5] == 0x00);
}

TEST_CASE("parse_mac_string - MAC FF:FF:FF:FF:FF:FF", "[wol]") {
    uint8_t mac[6];
    int result = parse_mac_string("FF:FF:FF:FF:FF:FF", mac);
    REQUIRE(result == 0);
    REQUIRE(mac[0] == 0xFF);
    REQUIRE(mac[5] == 0xFF);
}

TEST_CASE("parse_mac_string - MAC 01:23:45:67:89:AB", "[wol]") {
    uint8_t mac[6];
    int result = parse_mac_string("01:23:45:67:89:AB", mac);
    REQUIRE(result == 0);
    REQUIRE(mac[0] == 0x01);
    REQUIRE(mac[5] == 0xAB);
}

TEST_CASE("parse_mac_string - reject null pointers", "[wol]") {
    uint8_t mac[6];

    SECTION("str == NULL - return -1") {
        REQUIRE(parse_mac_string(NULL, mac) == -1);
    }

    SECTION("mac == NULL - return -1") {
        REQUIRE(parse_mac_string("AA:BB:CC:DD:EE:FF", NULL) == -1);
    }

    SECTION("Ambos NULL - return -1") {
        REQUIRE(parse_mac_string(NULL, NULL) == -1);
    }
}

TEST_CASE("parse_mac_string - reject invalid format", "[wol]") {
    uint8_t mac[6];

    SECTION("No colon") {
        REQUIRE(parse_mac_string("AABBCCDDEEFF", mac) == -1);
    }

    SECTION("Incomplete MAC - 3 bytes") {
        uint8_t mac[6];
        REQUIRE(parse_mac_string("AA:BB:CC", mac) == -1);
    }

    SECTION("Empty string") {
        uint8_t mac[6];
        REQUIRE(parse_mac_string("", mac) == -1);
    }

    SECTION("No hexadecimal char") {
        uint8_t mac[6];
        REQUIRE(parse_mac_string("GG:HH:II:JJ:KK:LL", mac) == -1);
    }

    SECTION("Only colon") {
        uint8_t mac[6];
        REQUIRE(parse_mac_string(":::::", mac) == -1);
    }
}
