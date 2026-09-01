#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <httplib.h>

#include "core/aliases_cache.h"
#include "core/translator.h"
#include "core/wol_packet.h"

#include "adapters/raspberry_pi/loader_linux.hpp"
#include "adapters/raspberry_pi/sender_linux.hpp"
#include "adapters/raspberry_pi/listener_linux.hpp"

namespace {

constexpr const char* kTokenEnvVar  = "COREWAKE_TOKEN";
constexpr int         kDefaultPort  = 8080;

const char* const kFixtureContent =
    "[aliases]\n"
    "PC=00:11:22:33:44:55\n"
    "TV = aa:bb:cc:dd:ee:ff\n"
    "ROBOT=11:22:33:44:55:66\n"
    "GUEST_1=DE:AD:BE:EF:00:01\n"
    "A234567890123456789012345678901=0A:0B:0C:0D:0E:0F\n";

constexpr int kFixtureEntryCount = 5;

// ---------------------------------------------------------------------------
// RAII helpers
// ---------------------------------------------------------------------------
struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        static std::atomic<unsigned long long> s_counter{0};
        dir = std::filesystem::temp_directory_path() /
              ("corewake_integration_" + std::to_string(::getpid()) + "_" +
               std::to_string(s_counter.fetch_add(1)));
        std::filesystem::create_directories(dir);
    }

    ~TempDir() {
        std::filesystem::remove_all(dir);
    }

    std::filesystem::path write_ini(const std::string& content) {
        const std::filesystem::path subdir = dir / "aliases";
        std::filesystem::create_directories(subdir);
        const std::filesystem::path p = subdir / "aliases.INI";
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
        return p;
    }
};

struct TokenEnv {
    bool        had_previous = false;
    std::string previous;

    explicit TokenEnv(const char* value) {
        if (const char* cur = std::getenv(kTokenEnvVar)) {
            previous     = cur;
            had_previous = true;
        }
        if (value == nullptr) {
            ::unsetenv(kTokenEnvVar);
        } else {
            ::setenv(kTokenEnvVar, value, /*overwrite=*/1);
        }
    }

    ~TokenEnv() {
        if (had_previous) {
            ::setenv(kTokenEnvVar, previous.c_str(), /*overwrite=*/1);
        } else {
            ::unsetenv(kTokenEnvVar);
        }
    }
};

// ---------------------------------------------------------------------------
// Listener callback
// ---------------------------------------------------------------------------
int on_wake_request(const char* alias, void* user) {
    return receive_request(alias, static_cast<translator_t*>(user));
}

// ---------------------------------------------------------------------------
// UDP loopback receiver
// ---------------------------------------------------------------------------
struct UdpLoopbackReceiver {
    int fd   = -1;
    uint16_t port = 0;

    UdpLoopbackReceiver() {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;

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

    ~UdpLoopbackReceiver() {
        if (fd >= 0) ::close(fd);
    }

    bool valid() const { return fd >= 0; }

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

// ---------------------------------------------------------------------------
// Port helpers
// ---------------------------------------------------------------------------
int find_free_port(int attempts = 16) {
    for (int i = 0; i < attempts; ++i) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = 0;

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            continue;
        }

        socklen_t alen = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
            ::close(fd);
            continue;
        }

        const int port = ntohs(addr.sin_port);
        ::close(fd);
        return port;
    }
    return -1;
}

bool wait_until_ready(int port, int timeout_ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port        = htons(static_cast<uint16_t>(port));

            if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
                ::close(fd);
                return true;
            }
            ::close(fd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

// ---------------------------------------------------------------------------
// HTTP client helpers
// ---------------------------------------------------------------------------
httplib::Client make_client(int port) {
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);
    return cli;
}

httplib::Response post_wake(httplib::Client& cli, const std::string& alias, const std::string& auth_header) {
    httplib::Request req;
    req.method = "POST";
    req.path   = "/wake/" + alias;
    if (!auth_header.empty()) req.headers.emplace("Authorization", auth_header);

    httplib::Response res;
    httplib::Error err = httplib::Error::Success;
    const bool sent = cli.send(req, res, err);
    if (!sent) INFO("client error code: " << static_cast<int>(err));
    return res;
}

struct CompositionRoot {
    LoaderLinux*   loader   = nullptr;
    SenderLinux*   sender   = nullptr;
    alias_cache_t* cache    = nullptr;
    wol_packet_t*  wp       = nullptr;
    translator_t*  tr       = nullptr;
    ListenerLinux* listener = nullptr;

    int                port           = 0;
    bool               serve_result   = false;
    std::atomic<bool>  serve_returned{false};
    std::thread        thread;

    ~CompositionRoot() {
        teardown();
    }

    bool start(const std::filesystem::path& ini, int http_port, UdpLoopbackReceiver& rx) {
        loader = LoaderLinux::create(ini.c_str());
        if (loader == nullptr) return false;

        sender = SenderLinux::create("127.0.0.1", static_cast<int>(rx.port));
        if (sender == nullptr) {
            delete loader;
            return false;
        }

        cache = alias_cache_init();
        wp    = wol_packet_create(sender->vtable());
        if (cache == nullptr || wp == nullptr) {
            if (cache != nullptr) alias_cache_destroy(cache);
            if (wp != nullptr)    wol_packet_destroy(wp);
            delete sender;
            delete loader;
            return false;
        }

        tr = translator_create(loader->vtable(), cache, wp);
        if (tr == nullptr) {
            translator_destroy(tr);
            alias_cache_destroy(cache);
            wol_packet_destroy(wp);
            delete sender;
            delete loader;
            return false;
        }

        const int pre = translator_preload(tr);
        if (pre != TRANSLATOR_OK) {
            translator_destroy(tr);
            alias_cache_destroy(cache);
            wol_packet_destroy(wp);
            delete sender;
            delete loader;
            return false;
        }

        listener = ListenerLinux::create(&on_wake_request, tr, http_port);
        if (listener == nullptr) {
            translator_destroy(tr);
            wol_packet_destroy(wp);
            alias_cache_destroy(cache);
            delete sender;
            delete loader;
            return false;
        }

        port = listener->port();
        thread = std::thread([this] {
            serve_result     = listener->serve();
            serve_returned = true;
        });
        return true;
    }

    void teardown() {
        if (listener != nullptr) {
            delete listener;
            listener = nullptr;
        }
        if (thread.joinable()) thread.join();

        if (tr != nullptr) {
            translator_destroy(tr);
            tr = nullptr;
        }
        if (wp != nullptr) {
            wol_packet_destroy(wp);
            wp = nullptr;
        }
        if (cache != nullptr) {
            alias_cache_destroy(cache);
            cache = nullptr;
        }
        if (sender != nullptr) {
            delete sender;
            sender = nullptr;
        }
        if (loader != nullptr) {
            delete loader;
            loader = nullptr;
        }
    }
};

} // namespace

TEST_CASE("composition root - boot completo: POST /wake/<alias> envia el magic packet por UDP",
          "[integration][composition_root]") {
    TempDir td;
    const auto ini = td.write_ini(kFixtureContent);

    TokenEnv env("integration-token");
    UdpLoopbackReceiver rx;
    REQUIRE(rx.valid());

    const int http_port = find_free_port();
    REQUIRE(http_port > 0);

    CompositionRoot app;
    REQUIRE(app.start(ini, http_port, rx));
    REQUIRE(app.port == http_port);
    REQUIRE(wait_until_ready(app.port));

    SECTION("alias precargado (PC) - 200 y magic packet con su MAC") {
        httplib::Client cli = make_client(app.port);
        const httplib::Response res = post_wake(cli, "PC", "Bearer integration-token");
        REQUIRE(res.status == 200);
        CHECK(res.body == "wake ok");

        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        REQUIRE(rx.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);

        const uint8_t mac[MAC_ADDRESS_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        require_magic_packet(buf, mac);
    }

    SECTION("todos los alias del fixture se resuelven y envian su magic packet") {
        struct { const char* alias; const uint8_t mac[MAC_ADDRESS_SIZE]; } kAll[] = {
            { "PC",                             { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } },
            { "TV",                             { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF } },
            { "ROBOT",                          { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 } },
            { "GUEST_1",                        { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 } },
            { "A234567890123456789012345678901", { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F } },
        };

        httplib::Client cli = make_client(app.port);
        for (const auto& e : kAll) {
            INFO("alias: " << e.alias);
            const httplib::Response res = post_wake(cli, e.alias, "Bearer integration-token");
            REQUIRE(res.status == 200);

            uint8_t buf[MAGIC_PACKET_SIZE] = {0};
            REQUIRE(rx.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
            require_magic_packet(buf, e.mac);
        }
    }

    SECTION("alias desconocido - 404 y sin trafico UDP (la cache no cambia)") {
        httplib::Client cli = make_client(app.port);
        const httplib::Response res = post_wake(cli, "NO_EXISTE", "Bearer integration-token");
        REQUIRE(res.status == 404);
        CHECK(res.body == "alias not found");
        CHECK(app.cache->count == kFixtureEntryCount);

        uint8_t buf[MAGIC_PACKET_SIZE];
        CHECK(rx.receive(buf, sizeof(buf)) == -1);
    }

    SECTION("sin Authorization - 401 y sin trafico UDP (el callback nunca corre)") {
        httplib::Client cli = make_client(app.port);
        const httplib::Response res = post_wake(cli, "PC", "");
        REQUIRE(res.status == 401);
        CHECK(res.body == "unauthorized");

        uint8_t buf[MAGIC_PACKET_SIZE];
        CHECK(rx.receive(buf, sizeof(buf)) == -1);
    }

    SECTION("token incorrecto - 401 y sin trafico UDP") {
        httplib::Client cli = make_client(app.port);
        const httplib::Response res = post_wake(cli, "PC", "Bearer otro-token");
        REQUIRE(res.status == 401);
        CHECK(res.body == "unauthorized");

        uint8_t buf[MAGIC_PACKET_SIZE];
        CHECK(rx.receive(buf, sizeof(buf)) == -1);
    }

    SECTION("preload - los 5 alias del fixture quedaron en cache") {
        CHECK(app.cache->count == kFixtureEntryCount);
    }

    app.teardown();
    CHECK(app.serve_returned.load());
    CHECK(app.serve_result == true);
}

TEST_CASE("composition root - alias no precargado: fallback a loader fetch y se cachea",
          "[integration][composition_root][fallback]") {
    TempDir td;
    const auto ini = td.write_ini(kFixtureContent);

    TokenEnv env("fallback-token");
    UdpLoopbackReceiver rx;
    REQUIRE(rx.valid());

    const int http_port = find_free_port();
    REQUIRE(http_port > 0);

    CompositionRoot app;
    REQUIRE(app.start(ini, http_port, rx));
    REQUIRE(wait_until_ready(app.port));

    {
        std::ofstream ofs(ini, std::ios::app);
        REQUIRE(ofs.good());
        ofs << "NEWPC=12:34:56:78:9A:BC\n";
    }

    httplib::Client cli = make_client(app.port);
    const httplib::Response res = post_wake(cli, "NEWPC", "Bearer fallback-token");
    REQUIRE(res.status == 200);
    CHECK(res.body == "wake ok");

    uint8_t buf[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(rx.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
    const uint8_t mac[MAC_ADDRESS_SIZE] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    require_magic_packet(buf, mac);

    CHECK(app.cache->count == kFixtureEntryCount + 1);
    const httplib::Response res2 = post_wake(cli, "NEWPC", "Bearer fallback-token");
    REQUIRE(res2.status == 200);
    uint8_t buf2[MAGIC_PACKET_SIZE] = {0};
    REQUIRE(rx.receive(buf2, sizeof(buf2)) == MAGIC_PACKET_SIZE);
    require_magic_packet(buf2, mac);
    CHECK(app.cache->count == kFixtureEntryCount + 1);

    app.teardown();
}

TEST_CASE("composition root - alias ausente en el INI: 404 y sin magic packet",
          "[integration][composition_root][not_found]") {
    TempDir td;
    const auto ini = td.write_ini(kFixtureContent);

    TokenEnv env("nf-token");
    UdpLoopbackReceiver rx;
    REQUIRE(rx.valid());

    const int http_port = find_free_port();
    REQUIRE(http_port > 0);

    CompositionRoot app;
    REQUIRE(app.start(ini, http_port, rx));
    REQUIRE(wait_until_ready(app.port));

    httplib::Client cli = make_client(app.port);
    const httplib::Response res = post_wake(cli, "PHANTOM", "Bearer nf-token");
    REQUIRE(res.status == 404);
    CHECK(res.body == "alias not found");

    uint8_t buf[MAGIC_PACKET_SIZE];
    CHECK(rx.receive(buf, sizeof(buf)) == -1);

    app.teardown();
}

TEST_CASE("composition root - sin aliases/aliases.INI: LoaderLinux::create() devuelve nullptr (boot abortado)",
          "[integration][composition_root][boot_failure]") {
    TempDir  td;
    TokenEnv env("bootfail-token");

    const std::filesystem::path missing = td.dir / "aliases" / "aliases.INI";
    REQUIRE_FALSE(std::filesystem::exists(missing));
    REQUIRE(LoaderLinux::create(missing.c_str()) == nullptr);

    const auto ini = td.write_ini(kFixtureContent);
    LoaderLinux* loader = LoaderLinux::create(ini.c_str());
    REQUIRE(loader != nullptr);
    CHECK(loader->vtable() != nullptr);
    CHECK(loader->vtable()->fetch != nullptr);
    CHECK(loader->vtable()->first != nullptr);
    CHECK(loader->vtable()->next  != nullptr);
    delete loader;
}

TEST_CASE("composition root - teardown: stop() termina serve() y libera todos los recursos",
          "[integration][composition_root][teardown]") {
    TempDir td;
    const auto ini = td.write_ini(kFixtureContent);

    TokenEnv env("td-token");
    UdpLoopbackReceiver rx;
    REQUIRE(rx.valid());

    const int http_port = find_free_port();
    REQUIRE(http_port > 0);

    {
        CompositionRoot app;
        REQUIRE(app.start(ini, http_port, rx));
        REQUIRE(wait_until_ready(app.port));

        httplib::Client cli = make_client(app.port);
        const httplib::Response res = post_wake(cli, "ROBOT", "Bearer td-token");
        REQUIRE(res.status == 200);
        uint8_t buf[MAGIC_PACKET_SIZE] = {0};
        REQUIRE(rx.receive(buf, sizeof(buf)) == MAGIC_PACKET_SIZE);
        const uint8_t mac[MAC_ADDRESS_SIZE] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        require_magic_packet(buf, mac);

        REQUIRE_NOTHROW(app.teardown());
        CHECK(app.serve_returned.load());
        CHECK(app.serve_result == true);
        CHECK(app.loader   == nullptr);
        CHECK(app.sender   == nullptr);
        CHECK(app.cache    == nullptr);
        CHECK(app.serve_result == true);
        CHECK(app.loader   == nullptr);
        CHECK(app.sender   == nullptr);
        CHECK(app.cache    == nullptr);
        CHECK(app.wp       == nullptr);
        CHECK(app.tr       == nullptr);
        CHECK(app.listener == nullptr);
    }
}
