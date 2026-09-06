#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

#include "adapters/raspberry_pi/sender_linux.hpp"

namespace {

constexpr std::size_t kPacketSize = 6 + 16 * 6;

} // namespace

// ==========================================================
// SenderLinux - create / validaciones
// ==========================================================

TEST_CASE("create - valid configurations", "[sender_linux][pi][unit]") {
    SECTION("defaults (null ip, port 0) - broadcast :9") {
        SenderLinux* s = SenderLinux::create(nullptr, 0);
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("explicit loopback ip and port") {
        SenderLinux* s = SenderLinux::create("127.0.0.1", 5000);
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("port <= 0 falls back to default 9") {
        SenderLinux* s = SenderLinux::create("127.0.0.1", -3);
        REQUIRE(s != nullptr);
        delete s;
    }
}

TEST_CASE("create - invalid configurations return nullptr", "[sender_linux][pi][unit]") {
    SECTION("port > 65535") {
        REQUIRE(SenderLinux::create("127.0.0.1", 65536) == nullptr);
    }

    SECTION("invalid ip - not numeric") {
        REQUIRE(SenderLinux::create("nope", 9) == nullptr);
    }

    SECTION("invalid ip - 5 octets") {
        REQUIRE(SenderLinux::create("1.2.3.4.5", 9) == nullptr);
    }

    SECTION("invalid ip - octet out of range") {
        REQUIRE(SenderLinux::create("256.1.1.1", 9) == nullptr);
    }
}

TEST_CASE("send - vtable rejects invalid args", "[sender_linux][pi][unit]") {
    SenderLinux* s = SenderLinux::create("127.0.0.1", 9);
    REQUIRE(s != nullptr);

    uint8_t dummy[kPacketSize] = {0};

    SECTION("null packet") {
        REQUIRE(s->vtable()->send(nullptr, sizeof(dummy), s->vtable()) == -1);
    }

    SECTION("zero length") {
        REQUIRE(s->vtable()->send(dummy, 0, s->vtable()) == -1);
    }

    SECTION("null vtable") {
        REQUIRE(s->vtable()->send(dummy, sizeof(dummy), nullptr) == -1);
    }

    delete s;
}
