// tests/test_adapters.cpp (solo Linux)
#include <catch2/catch_test_macros.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include "core/wol_packet.h"
#include "adapters/raspberry_pi/sender_linux.hpp"

TEST_CASE("sender_linux - magic packet completo por loopback", "[adapter]") {
    int listener = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(listener >= 0);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    socklen_t alen = sizeof(addr);
    REQUIRE(::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
    const uint16_t port = ntohs(addr.sin_port);

    SenderLinux sender("127.0.0.1", port);      // RAII: muere al cerrar el scope
    wol_packet_t* wp = wol_packet_create(sender.vtable());
    REQUIRE(wp != nullptr);
    REQUIRE(core_wake("AA:BB:CC:DD:EE:FF", wp) == WOL_OK);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(::recvfrom(listener, buf, sizeof(buf), 0, nullptr, nullptr) == MAGIC_PACKET_SIZE);

    for (int i = 0; i < 6; i++) REQUIRE(buf[i] == 0xFF);
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    for (int i = 0; i < MAC_REPETITIONS; i++)
        for (int j = 0; j < 6; j++) REQUIRE(buf[6 + i*6 + j] == mac[j]);

    wol_packet_destroy(wp);
    ::close(listener);
}

TEST_CASE("sender_linux - defaults y errores", "[adapter]") {
    SECTION("constructor por defecto: broadcast:9, vtable viva") {
        SenderLinux sender;
        REQUIRE(sender.vtable() != nullptr);
        REQUIRE(sender.vtable()->send != nullptr);
    }
    SECTION("IP malformada lanza invalid_argument") {
        REQUIRE_THROWS_AS(SenderLinux("no-es-una-ip", 0), std::invalid_argument);
    }
    SECTION("port fuera de rango lanza invalid_argument") {
        REQUIRE_THROWS_AS(SenderLinux(nullptr, 70000), std::invalid_argument);
    }
}
