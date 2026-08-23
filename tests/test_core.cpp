#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include "core/wol_packet.h"

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
        REQUIRE(wake(wp, "AA:BB:CC:DD:EE:FF") == WOL_OK);
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
        REQUIRE(wake(NULL, "AA:BB:CC:DD:EE:FF") == WOL_ERR_NULL);
    }

    SECTION("wake with NULL str returns WOL_ERR_NULL") {
        REQUIRE(wake(wp, NULL) == WOL_ERR_NULL);
    }

    SECTION("wake with invalid MAC returns WOL_ERR_PARSE") {
        REQUIRE(wake(wp, "no-mac") == WOL_ERR_PARSE);
    }

    wol_packet_destroy(wp);
}

TEST_CASE("wol_packet wake - send failure propagates port error", "[wol]") {
    sender_port_t port = {stub_send_fail, stub_close};
    wol_packet_t* wp = wol_packet_create(&port);

    REQUIRE(wake(wp, "AA:BB:CC:DD:EE:FF") == -1);

    wol_packet_destroy(wp);
}
