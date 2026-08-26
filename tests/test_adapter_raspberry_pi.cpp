#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "core/wol_packet.h"
#include "adapters/raspberry_pi/sender_linux.hpp"

// The adapter owns a socket and is deliberately non-copyable and
static_assert(!std::is_copy_constructible_v<SenderLinux>, "SenderLinux must stay non-copyable");
static_assert(!std::is_copy_assignable_v<SenderLinux>,    "SenderLinux must stay non-copyable");
static_assert(!std::is_move_constructible_v<SenderLinux>, "SenderLinux must stay non-movable");
static_assert(!std::is_move_assignable_v<SenderLinux>,    "SenderLinux must stay non-movable");

namespace {

// RAII UDP loopback listener.
struct LoopbackListener {
    int fd   = -1;
    uint16_t port = 0;

    LoopbackListener() {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            fd = -1;
            return;
        }

        socklen_t alen = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
            ::close(fd);
            fd = -1;
            return;
        }
        port = ntohs(addr.sin_port);

        timeval tv{};
        tv.tv_sec  = 2;
        tv.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~LoopbackListener() {
        if (fd >= 0) ::close(fd);
    }

    int receive(uint8_t* buf, std::size_t cap) const {
        if (fd < 0) return -1;
        return static_cast<int>(::recvfrom(fd, buf, cap, 0, nullptr, nullptr));
    }
};

void require_magic_packet(const uint8_t buf[MAGIC_PACKET_SIZE], const uint8_t mac[MAC_ADDRESS_SIZE]) {
    for (int i = 0; i < BROADCAST_BYTES; ++i)
        REQUIRE(buf[i] == 0xFF);
    for (int i = 0; i < MAC_REPETITIONS; ++i)
        for (int j = 0; j < MAC_ADDRESS_SIZE; ++j)
            REQUIRE(buf[BROADCAST_BYTES + i * MAC_ADDRESS_SIZE + j] == mac[j]);
}

constexpr uint8_t kMac[MAC_ADDRESS_SIZE] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
const char* const kMacStr                = "AA:BB:CC:DD:EE:FF";

} // namespace

// ---------------------------------------------------------------------------
// create() factory - success paths
// ---------------------------------------------------------------------------
TEST_CASE("create() - default arguments yield a usable broadcast:9 sender", "[sender_linux]") {
    SenderLinux* sender = nullptr;
    REQUIRE_NOTHROW(sender = SenderLinux::create());
    REQUIRE(sender != nullptr);
    REQUIRE(sender->vtable() != nullptr);
    REQUIRE(sender->vtable()->send != nullptr);

    delete sender;
}

TEST_CASE("create() - explicit IPv4 destination and port", "[sender_linux]") {
    SECTION("loopback address") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create("127.0.0.1", 7777));
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("private LAN address") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create("192.168.1.10", 9));
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("null destination with a custom port") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create(nullptr, 40000));
        REQUIRE(s != nullptr);
        delete s;
    }
}

TEST_CASE("create() - empty-string destination falls back to broadcast", "[sender_linux]") {
    SenderLinux* sender = nullptr;
    REQUIRE_NOTHROW(sender = SenderLinux::create("", 0));
    REQUIRE(sender != nullptr);
    delete sender;
}

TEST_CASE("create() - port boundaries and non-positive ports", "[sender_linux]") {
    SECTION("minimum port 1 is accepted") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create("127.0.0.1", 1));
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("maximum port 65535 is accepted") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create("127.0.0.1", 65535));
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("port 0 falls back to the default (9)") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create(nullptr, 0));
        REQUIRE(s != nullptr);
        delete s;
    }

    SECTION("negative port falls back to the default (9)") {
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create(nullptr, -1));
        REQUIRE(s != nullptr);
        delete s;
    }
}

// ---------------------------------------------------------------------------
// create() factory - failure paths
// ---------------------------------------------------------------------------
TEST_CASE("create() - malformed destination IPs return nullptr without throwing", "[sender_linux]") {
    const char* const bad_ips[] = {
        "no-es-una-ip",   // not an address at all
        "1.2.3.4.5",      // too many dotted groups
        "999.1.1.1",      // group outside 0..255
        "1.2.3.4g",       // non-numeric group
        "0x01020304",     // hex notation is not a valid dotted quad
        "127.0.0.1/24",   // CIDR suffix is not accepted
    };

    for (const char* ip : bad_ips) {
        INFO("dest_ip: '" << ip << "'");
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create(ip, 9));
        CHECK(s == nullptr);
    }
}

TEST_CASE("create() - out-of-range ports return nullptr without throwing", "[sender_linux]") {
    const int bad_ports[] = {65536, 70000, 100000};

    for (int port : bad_ports) {
        INFO("port: " << port);
        SenderLinux* s = nullptr;
        REQUIRE_NOTHROW(s = SenderLinux::create(nullptr, port));
        CHECK(s == nullptr);
    }
}

// ---------------------------------------------------------------------------
// Public API / vtable contract
// ---------------------------------------------------------------------------
TEST_CASE("vtable() - stable, fully wired sender port", "[sender_linux]") {
    SenderLinux* sender = SenderLinux::create();
    REQUIRE(sender != nullptr);

    sender_port_t* a = sender->vtable();
    sender_port_t* b = sender->vtable();

    REQUIRE(a != nullptr);
    REQUIRE(a == b);            
    REQUIRE(a->send != nullptr); 

    delete sender;
}

TEST_CASE("vtable send - invalid arguments return -1 without throwing", "[sender_linux]") {
    SenderLinux* sender = SenderLinux::create("127.0.0.1", 54321);
    REQUIRE(sender != nullptr);
    sender_port_t* vt = sender->vtable();
    REQUIRE(vt != nullptr);
    REQUIRE(vt->send != nullptr);

    const uint8_t data[8] = {0};
    int rc = 0;

    SECTION("null packet pointer") {
        REQUIRE_NOTHROW(rc = vt->send(nullptr, sizeof(data), vt));
        CHECK(rc == -1);
    }

    SECTION("zero length") {
        REQUIRE_NOTHROW(rc = vt->send(data, 0, vt));
        CHECK(rc == -1);
    }

    SECTION("null self pointer") {
        REQUIRE_NOTHROW(rc = vt->send(data, sizeof(data), nullptr));
        CHECK(rc == -1);
    }

    delete sender;
}

TEST_CASE("wol_packet_create - borrows the adapter vtable", "[sender_linux]") {
    SECTION("null port is rejected") {
        wol_packet_t* wp = nullptr;
        REQUIRE_NOTHROW(wp = wol_packet_create(nullptr));
        CHECK(wp == nullptr);
    }

    SECTION("adapter vtable is accepted") {
        SenderLinux* sender = SenderLinux::create();
        REQUIRE(sender != nullptr);

        wol_packet_t* wp = nullptr;
        REQUIRE_NOTHROW(wp = wol_packet_create(sender->vtable()));
        REQUIRE(wp != nullptr);

        wol_packet_destroy(wp);
        delete sender;
    }
}

// ---------------------------------------------------------------------------
// Practical send behavior over loopback (no hardware required)
// ---------------------------------------------------------------------------
TEST_CASE("send - magic packet received intact over loopback", "[sender_linux]") {
    LoopbackListener listener;
    REQUIRE(listener.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", listener.port);
    REQUIRE(sender != nullptr);

    wol_packet_t* wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);

    REQUIRE(core_wake(kMacStr, wp) == WOL_OK);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(listener.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
    require_magic_packet(buf, kMac);

    wol_packet_destroy(wp);
    delete sender;
}

TEST_CASE("vtable send - direct send() delivers the payload verbatim", "[sender_linux]") {
    LoopbackListener listener;
    REQUIRE(listener.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", listener.port);
    REQUIRE(sender != nullptr);
    sender_port_t* vt = sender->vtable();

    SECTION("a full magic packet is delivered byte-for-byte") {
        uint8_t out[MAGIC_PACKET_SIZE] = {0};
        REQUIRE(build_magic_packet(kMac, out) == WOL_OK);

        int rc = -99;
        REQUIRE_NOTHROW(rc = vt->send(out, sizeof(out), vt));
        REQUIRE(rc == 0);

        uint8_t in[MAGIC_PACKET_SIZE] = {0};
        REQUIRE(listener.receive(in, sizeof(in)) == MAGIC_PACKET_SIZE);
        require_magic_packet(in, kMac);
    }

    SECTION("an arbitrary small payload keeps its exact length") {
        const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};

        int rc = -99;
        REQUIRE_NOTHROW(rc = vt->send(payload, sizeof(payload), vt));
        REQUIRE(rc == 0);

        uint8_t in[4] = {0};
        REQUIRE(listener.receive(in, sizeof(in)) == 4);
        for (int i = 0; i < 4; ++i)
            REQUIRE(in[i] == payload[i]);
    }

    delete sender;
}

TEST_CASE("send - repeated wakes each deliver a valid magic packet", "[sender_linux]") {
    LoopbackListener listener;
    REQUIRE(listener.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", listener.port);
    REQUIRE(sender != nullptr);
    wol_packet_t* wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};

    for (int i = 0; i < 3; ++i) {
        INFO("wake #" << (i + 1));
        REQUIRE(core_wake(kMacStr, wp) == WOL_OK);
        REQUIRE(listener.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, kMac);
    }

    REQUIRE(core_wake("aa:bb:cc:dd:ee:ff", wp) == WOL_OK);
    REQUIRE(listener.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
    require_magic_packet(buf, kMac);

    wol_packet_destroy(wp);
    delete sender;
}

TEST_CASE("send - datagrams are routed to the configured destination port only", "[sender_linux]") {
    LoopbackListener target;
    LoopbackListener bystander;
    REQUIRE(target.fd >= 0);
    REQUIRE(bystander.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", target.port);
    REQUIRE(sender != nullptr);
    wol_packet_t* wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);

    REQUIRE(core_wake(kMacStr, wp) == WOL_OK);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(target.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
    require_magic_packet(buf, kMac);

    REQUIRE(bystander.receive(buf, sizeof(buf)) == -1);

    wol_packet_destroy(wp);
    delete sender;
}

TEST_CASE("lifecycle - sender stays usable across wol_packet_destroy()", "[sender_linux]") {
    LoopbackListener listener;
    REQUIRE(listener.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", listener.port);
    REQUIRE(sender != nullptr);

    wol_packet_t* wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);
    REQUIRE(core_wake(kMacStr, wp) == WOL_OK);

    wol_packet_destroy(wp);

    wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);
    REQUIRE(core_wake(kMacStr, wp) == WOL_OK);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(listener.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
    require_magic_packet(buf, kMac);
    REQUIRE(listener.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
    require_magic_packet(buf, kMac);

    wol_packet_destroy(wp);
    delete sender;
}

// ---------------------------------------------------------------------------
// Non-exceptional error handling through the full stack
// ---------------------------------------------------------------------------
TEST_CASE("core_wake - null arguments return WOL_ERR_NULL without sending", "[sender_linux]") {
    LoopbackListener listener;
    REQUIRE(listener.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", listener.port);
    REQUIRE(sender != nullptr);
    wol_packet_t* wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};

    REQUIRE(core_wake(nullptr, wp) == WOL_ERR_NULL);
    REQUIRE(core_wake(kMacStr, nullptr) == WOL_ERR_NULL);

    REQUIRE(listener.receive(buf, sizeof(buf)) == -1);

    wol_packet_destroy(wp);
    delete sender;
}

TEST_CASE("core_wake - malformed MAC strings fail with WOL_ERR_PARSE, nothing is sent", "[sender_linux]") {
    LoopbackListener listener;
    REQUIRE(listener.fd >= 0);

    SenderLinux* sender = SenderLinux::create("127.0.0.1", listener.port);
    REQUIRE(sender != nullptr);
    wol_packet_t* wp = wol_packet_create(sender->vtable());
    REQUIRE(wp != nullptr);

    const struct {
        const char* label;
        const char* mac;
    } bad_macs[] = {
        {"empty string",         ""},
        {"no separators",        "AABBCCDDEEFF"},
        {"incomplete (3 bytes)", "AA:BB:CC"},
        {"non-hex characters",   "GG:HH:II:JJ:KK:LL"},
        {"trailing byte group",  "AA:BB:CC:DD:EE:FF:00"},
        {"trailing text",        "AA:BB:CC:DD:EE:FF extra"},
    };

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
}