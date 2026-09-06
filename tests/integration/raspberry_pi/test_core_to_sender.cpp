#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "adapters/raspberry_pi/sender_linux.hpp"
#include "core/wol_packet.h"
#include "util/udp_loopback.hpp"

using corewake::test::UdpLoopbackReceiver;
using corewake::test::require_magic_packet;

namespace {

void mac_str_to_bytes(const char* s, uint8_t out[MAC_ADDRESS_SIZE]) {
    REQUIRE(parse_mac_string(s, out) == WOL_OK);
}

} // namespace

TEST_CASE("send - UDP datagram arrives with exact bytes", "[core_to_sender][integration][pi]") {
    UdpLoopbackReceiver rx;
    REQUIRE(rx.valid());

    SenderLinux* s = SenderLinux::create("127.0.0.1", static_cast<int>(rx.port));
    REQUIRE(s != nullptr);

    uint8_t mac[MAC_ADDRESS_SIZE];
    mac_str_to_bytes("00:11:22:33:44:55", mac);

    uint8_t packet[MAGIC_PACKET_SIZE];
    REQUIRE(build_magic_packet(mac, packet) == WOL_OK);

    REQUIRE(s->vtable()->send(packet, sizeof(packet), s->vtable()) == 0);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    const int n = rx.receive(buf, sizeof(buf));
    REQUIRE(n == static_cast<int>(sizeof(packet)));

    require_magic_packet(buf, mac);
    REQUIRE(std::memcmp(buf, packet, sizeof(packet)) == 0);

    delete s;
}

TEST_CASE("core_wake - through real SenderLinux over loopback", "[core_to_sender][integration][pi]") {
    UdpLoopbackReceiver rx;
    REQUIRE(rx.valid());

    SenderLinux* s = SenderLinux::create("127.0.0.1", static_cast<int>(rx.port));
    REQUIRE(s != nullptr);

    wol_packet_t* wp = wol_packet_create(s->vtable());
    REQUIRE(wp != nullptr);

    SECTION("valid MAC returns WOL_OK and packet is on the wire") {
        REQUIRE(core_wake("00:11:22:33:44:55", wp) == WOL_OK);

        uint8_t mac[MAC_ADDRESS_SIZE];
        mac_str_to_bytes("00:11:22:33:44:55", mac);

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        const int n = rx.receive(buf, sizeof(buf));
        REQUIRE(n == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, mac);
    }

    SECTION("invalid MAC returns WOL_ERR_PARSE (nothing sent)") {
        REQUIRE(core_wake("zz:11:22:33:44:55", wp) == WOL_ERR_PARSE);
    }

    SECTION("null MAC returns WOL_ERR_NULL") {
        REQUIRE(core_wake(nullptr, wp) == WOL_ERR_NULL);
    }

    wol_packet_destroy(wp);
    delete s;
}
