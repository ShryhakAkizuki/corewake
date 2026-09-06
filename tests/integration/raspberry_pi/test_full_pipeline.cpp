#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "core/aliases_cache.h"
#include "core/translator.h"
#include "core/wol_packet.h"

#include "fixtures/aliases.h"
#include "util/http_helpers.hpp"
#include "util/pipeline.hpp"
#include "util/udp_loopback.hpp"

using corewake::test::FullPipeline;
using corewake::test::UdpLoopbackReceiver;
using corewake::test::kEntryCount;
using corewake::test::kFixtureContent;
using corewake::test::make_client;
using corewake::test::post_wake;
using corewake::test::require_magic_packet;

namespace {

constexpr const char* kTestToken = "full-pipeline-t0k3n";

int on_wake_request(const char* alias, void* user) {
    return receive_request(alias, static_cast<translator_t*>(user));
}

void mac_str_to_bytes(const char* s, uint8_t out[MAC_ADDRESS_SIZE]) {
    REQUIRE(parse_mac_string(s, out) == WOL_OK);
}

std::string make_ini_with(int n) {
    std::string ini = "[aliases]\n";
    for (int i = 1; i <= n; ++i) {
        char line[64];
        std::snprintf(line, sizeof(line), "A%02d=%02X:%02X:%02X:%02X:%02X:%02X\n",
                      i,
                      i * 1 % 256, i * 2 % 256, i * 3 % 256,
                      i * 4 % 256, i * 5 % 256, i * 6 % 256);
        ini += line;
    }
    return ini;
}

void require_no_datagram(UdpLoopbackReceiver* rx) {
    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(rx->receive(buf, sizeof(buf)) <= 0);
}

} // namespace

TEST_CASE("full pipeline - real HTTP wakes up the device and the packet reaches the wire",
          "[full_pipeline][integration][pi]") {
    FullPipeline pipeline(kTestToken, kFixtureContent, &on_wake_request, /*preload=*/true);
    REQUIRE(pipeline.cache()->count == static_cast<size_t>(kEntryCount));

    httplib::Client cli = make_client(pipeline.port());
    const std::string auth = std::string("Bearer ") + kTestToken;

    SECTION("preloaded alias (PC) -> 200 and exact magic packet for 00:11:22:33:44:55") {
        const httplib::Response res = post_wake(cli, "PC", auth);
        REQUIRE(res.status == 200);
        REQUIRE(res.body == "wake ok");

        uint8_t mac[MAC_ADDRESS_SIZE];
        mac_str_to_bytes("00:11:22:33:44:55", mac);

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        const int n = pipeline.receiver()->receive(buf, sizeof(buf));
        REQUIRE(n == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, mac);
    }

    SECTION("preloaded alias with lowercase MAC (TV) -> 200 and packet for aa:bb:cc:dd:ee:ff") {
        const httplib::Response res = post_wake(cli, "TV", auth);
        REQUIRE(res.status == 200);

        uint8_t mac[MAC_ADDRESS_SIZE];
        mac_str_to_bytes("aa:bb:cc:dd:ee:ff", mac);

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        const int n = pipeline.receiver()->receive(buf, sizeof(buf));
        REQUIRE(n == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, mac);
    }

    SECTION("non-existent alias -> 404 and NOTHING travels over the wire") {
        const httplib::Response res = post_wake(cli, "GHOST", auth);
        REQUIRE(res.status == 404);
        REQUIRE(res.body == "alias not found");
    }

    SECTION("no token -> 401 and NOTHING travels over the wire") {
        const httplib::Response res = post_wake(cli, "PC", "");
        REQUIRE(res.status == 401);
        REQUIRE(res.body == "unauthorized");
    }

    require_no_datagram(pipeline.receiver());
}

TEST_CASE("full pipeline - alias NOT preloaded: lazy fetch via HTTP still wakes the device",
          "[full_pipeline][integration][pi]") {
    FullPipeline pipeline(kTestToken, kFixtureContent, &on_wake_request, /*preload=*/false);

    httplib::Client cli = make_client(pipeline.port());
    const std::string auth = std::string("Bearer ") + kTestToken;

    SECTION("first request (ROBOT): fetch of the real INI -> 200 + correct packet") {
        const httplib::Response res = post_wake(cli, "ROBOT", auth);
        REQUIRE(res.status == 200);
        REQUIRE(res.body == "wake ok");

        uint8_t mac[MAC_ADDRESS_SIZE];
        mac_str_to_bytes("11:22:33:44:55:66", mac);

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        const int n = pipeline.receiver()->receive(buf, sizeof(buf));
        REQUIRE(n == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, mac);

        char mac2[MAC_STR_BUF_SIZE] = {0};
        REQUIRE(alias_cache_search("ROBOT", mac2, pipeline.cache()) == ALIAS_OK);
        REQUIRE(std::strcmp(mac2, "11:22:33:44:55:66") == 0);
    }
}

TEST_CASE("full pipeline - INI with 20 entries: cache full (16) and the rest resolved via fetch",
          "[full_pipeline][integration][pi]") {

    FullPipeline pipeline(kTestToken, make_ini_with(20), &on_wake_request, /*preload=*/true);
    REQUIRE(pipeline.cache()->count == ALIAS_CACHE_MAX_ENTRIES);

    httplib::Client cli = make_client(pipeline.port());
    const std::string auth = std::string("Bearer ") + kTestToken;

    SECTION("alias inside the cache (A01) -> 200 + packet from the INI") {
        const httplib::Response res = post_wake(cli, "A01", auth);
        REQUIRE(res.status == 200);

        uint8_t mac[MAC_ADDRESS_SIZE];
        mac_str_to_bytes("01:02:03:04:05:06", mac);

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        const int n = pipeline.receiver()->receive(buf, sizeof(buf));
        REQUIRE(n == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, mac);
    }

    SECTION("alias outside the cache (A19) -> fetch -> 200 + packet from the INI") {
        const httplib::Response res = post_wake(cli, "A19", auth);
        REQUIRE(res.status == 200);

        uint8_t mac[MAC_ADDRESS_SIZE];
        mac_str_to_bytes("13:26:39:4C:5F:72", mac);

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        const int n = pipeline.receiver()->receive(buf, sizeof(buf));
        REQUIRE(n == MAGIC_PACKET_SIZE);
        require_magic_packet(buf, mac);
    }

    SECTION("non-existent alias with a full cache -> 404") {
        const httplib::Response res = post_wake(cli, "Z99", auth);
        REQUIRE(res.status == 404);
        REQUIRE(res.body == "alias not found");
    }

    require_no_datagram(pipeline.receiver());
}
