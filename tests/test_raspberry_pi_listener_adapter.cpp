#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

#include <httplib.h>

#include "adapters/raspberry_pi/listener_linux.hpp"

// The adapter owns an HTTP server and is deliberately non-copyable and non-movable.
static_assert(!std::is_copy_constructible_v<ListenerLinux>, "ListenerLinux must stay non-copyable");
static_assert(!std::is_copy_assignable_v<ListenerLinux>,    "ListenerLinux must stay non-copyable");
static_assert(!std::is_move_constructible_v<ListenerLinux>, "ListenerLinux must stay non-movable");
static_assert(!std::is_move_assignable_v<ListenerLinux>,    "ListenerLinux must stay non-movable");

namespace {

constexpr const char* kTokenEnvVar = "COREWAKE_TOKEN";
constexpr int         kDefaultPort = 8080;

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

struct CallbackState {
    std::atomic<int> rc{0};
    std::atomic<int> calls{0};
    std::string      last_alias;
    std::mutex       mu;
};

int recording_callback(const char* alias, void* ctx) {
    auto* st = static_cast<CallbackState*>(ctx);
    st->calls.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(st->mu);
        if (alias != nullptr) st->last_alias = alias;
    }
    return st->rc.load(std::memory_order_relaxed);
}

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

bool wait_until_ready(int port, int timeout_ms = 3000) {
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

struct RunningListener {
    ListenerLinux*     listener       = nullptr;
    int                port           = 0;
    std::atomic<bool>  serve_returned{false};
    bool               serve_result   = false;
    std::thread        thread;

    bool start(listener_receive_t cb, void* ctx, int port) {
        this->port   = port;
        listener = ListenerLinux::create(cb, ctx, port);
        if (listener == nullptr) return false;
        thread = std::thread([this] {
            serve_result   = listener->serve();
            serve_returned = true;
        });
        return true;
    }

    RunningListener() = default;
    RunningListener(const RunningListener&)            = delete;
    RunningListener& operator=(const RunningListener&) = delete;

    ~RunningListener() {
        stop();
    }

    void stop() {
        if (listener != nullptr) listener->stop();
        if (thread.joinable()) thread.join();
    }
};

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

httplib::Response get_path(httplib::Client& cli, const std::string& path) {
    httplib::Request req;
    req.method = "GET";
    req.path   = path;

    httplib::Response res;
    httplib::Error err = httplib::Error::Success;
    const bool sent = cli.send(req, res, err);
    if (!sent) INFO("client error code: " << static_cast<int>(err));
    return res;
}

} // namespace

// ---------------------------------------------------------------------------
// create() factory
// ---------------------------------------------------------------------------
TEST_CASE("create() - port argument is honored", "[listener_linux]") {
    TokenEnv env("create-token");
    CallbackState st;

    SECTION("omitted -> default port") {
        ListenerLinux* listener = nullptr;
        REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st));
        REQUIRE(listener != nullptr);
        CHECK(listener->port() == kDefaultPort);
        delete listener;
    }

    SECTION("explicit port") {
        ListenerLinux* listener = nullptr;
        REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st, 43210));
        REQUIRE(listener != nullptr);
        CHECK(listener->port() == 43210);
        delete listener;
    }

    SECTION("zero -> default port") {
        ListenerLinux* listener = nullptr;
        REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st, 0));
        REQUIRE(listener != nullptr);
        CHECK(listener->port() == kDefaultPort);
        delete listener;
    }
}

TEST_CASE("create() - invalid arguments return nullptr without throwing", "[listener_linux]") {
    CallbackState st;

    SECTION("null callback") {
        TokenEnv env("create-token");
        ListenerLinux* l = nullptr;
        REQUIRE_NOTHROW(l = ListenerLinux::create(nullptr, &st));
        CHECK(l == nullptr);
    }

    SECTION("null context") {
        TokenEnv env("create-token");
        ListenerLinux* l = nullptr;
        REQUIRE_NOTHROW(l = ListenerLinux::create(&recording_callback, nullptr));
        CHECK(l == nullptr);
    }

    SECTION("missing COREWAKE_TOKEN") {
        TokenEnv env(nullptr);
        ListenerLinux* l = nullptr;
        REQUIRE_NOTHROW(l = ListenerLinux::create(&recording_callback, &st));
        CHECK(l == nullptr);
    }

    SECTION("empty COREWAKE_TOKEN") {
        TokenEnv env("");
        ListenerLinux* l = nullptr;
        REQUIRE_NOTHROW(l = ListenerLinux::create(&recording_callback, &st));
        CHECK(l == nullptr);
    }
}

// ---------------------------------------------------------------------------
// serve() / stop() - lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("serve() - serves over loopback and stops cleanly", "[listener_linux]") {
    TokenEnv env("serve-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    const httplib::Response res = post_wake(cli, "PC", "Bearer serve-token");
    REQUIRE(res.status == 200);
    CHECK(res.body == "wake ok");
    CHECK(res.get_header_value("Content-Type") == "text/plain; charset=utf-8");
    CHECK(st.calls.load() == 1);

    rl.stop();
    CHECK(rl.serve_returned.load());
}

TEST_CASE("serve() - a busy port is reported as false without hanging", "[listener_linux]") {
    TokenEnv env("busy-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    const int holder = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(holder >= 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    REQUIRE(::bind(holder, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(::listen(holder, 1) == 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));

    for (int i = 0; i < 100 && !rl.serve_returned.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));

    REQUIRE(rl.serve_returned.load());
    CHECK(rl.serve_result == false);

    rl.stop();
    ::close(holder);
}

TEST_CASE("stop() - is safe to call without having served and to call twice", "[listener_linux]") {
    TokenEnv env("stop-token");
    CallbackState st;
    ListenerLinux* listener = ListenerLinux::create(&recording_callback, &st, find_free_port());
    REQUIRE(listener != nullptr);

    REQUIRE_NOTHROW(listener->stop());
    REQUIRE_NOTHROW(listener->stop());
    CHECK(listener->port() > 0);

    delete listener;
}

TEST_CASE("destructor - stops a running server (SIGINT pattern from main)", "[listener_linux]") {
    TokenEnv env("dtor-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    ListenerLinux* listener = ListenerLinux::create(&recording_callback, &st, port);
    REQUIRE(listener != nullptr);

    std::atomic<bool> serve_returned{false};
    std::thread t([&] {
        listener->serve();
        serve_returned = true;
    });
    REQUIRE(wait_until_ready(port));

    delete listener;

    t.join();
    REQUIRE(serve_returned.load());
}

// ---------------------------------------------------------------------------
// Authorization - Bearer token
// ---------------------------------------------------------------------------
TEST_CASE("auth - Bearer token", "[listener_linux]") {
    TokenEnv env("auth-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);

    SECTION("correct token is accepted (200)") {
        const httplib::Response res = post_wake(cli, "PC", "Bearer auth-token");
        REQUIRE(res.status == 200);
        CHECK(res.body == "wake ok");
        CHECK(st.calls.load() == 1);
    }

    SECTION("missing Authorization header (401)") {
        const httplib::Response res = post_wake(cli, "PC", "");
        REQUIRE(res.status == 401);
        CHECK(res.body == "unauthorized");
        CHECK(st.calls.load() == 0);
    }

    SECTION("wrong token (401)") {
        const httplib::Response res = post_wake(cli, "PC", "Bearer definitely-not-the-token");
        REQUIRE(res.status == 401);
        CHECK(st.calls.load() == 0);
    }

    SECTION("malformed Authorization header (401)") {
        const struct { const char* label; const char* header; } bad[] = {
            { "different scheme",      "Token auth-token" },
            { "the word 'Bearer' alone", "Bearer" },
            { "prefix without space",  "BearerX auth-token" },
            { "lowercase scheme",      "bearer auth-token" },
        };

        for (const auto& b : bad) {
            INFO("header: '" << b.header << "'");
            const httplib::Response res = post_wake(cli, "PC", b.header);
            REQUIRE(res.status == 401);
        }
        CHECK(st.calls.load() == 0);
    }
}

// ---------------------------------------------------------------------------
// Dispatch - callback result code -> HTTP status
// ---------------------------------------------------------------------------
TEST_CASE("dispatch - callback result code maps to the HTTP status", "[listener_linux]") {
    TokenEnv env("dispatch-token");

    SECTION("rc 0 -> 200 'wake ok'") {
        const int port = find_free_port();
        REQUIRE(port > 0);

        CallbackState st;   
        RunningListener rl;
        REQUIRE(rl.start(&recording_callback, &st, port));
        REQUIRE(wait_until_ready(port));

        httplib::Client cli = make_client(port);
        const httplib::Response res = post_wake(cli, "PC", "Bearer dispatch-token");
        REQUIRE(res.status == 200);
        CHECK(res.body == "wake ok");
        CHECK(st.calls.load() == 1);
    }

    SECTION("rc -3 -> 404 'alias not found'") {
        const int port = find_free_port();
        REQUIRE(port > 0);

        CallbackState st;
        st.rc = -3;
        RunningListener rl;
        REQUIRE(rl.start(&recording_callback, &st, port));
        REQUIRE(wait_until_ready(port));

        httplib::Client cli = make_client(port);
        const httplib::Response res = post_wake(cli, "GHOST", "Bearer dispatch-token");
        REQUIRE(res.status == 404);
        CHECK(res.body == "alias not found");
        CHECK(st.calls.load() == 1);
    }

    SECTION("any other non-zero rc -> 500 'internal error'") {
        int port = find_free_port();
        REQUIRE(port > 0);

        const int bad_rcs[] = {-1, -2, -4, -5, 1};
        for (int rc : bad_rcs) {
            INFO("rc: " << rc);

            CallbackState st;
            st.rc = rc;
            RunningListener rl;
            REQUIRE(rl.start(&recording_callback, &st, port));
            REQUIRE(wait_until_ready(port));

            httplib::Client cli = make_client(port);
            const httplib::Response res = post_wake(cli, "PC", "Bearer dispatch-token");
            REQUIRE(res.status == 500);
            CHECK(res.body == "internal error");
            CHECK(st.calls.load() == 1);

            rl.stop();
            port = find_free_port();
            REQUIRE(port > 0);
        }
    }

    SECTION("repeated sequential requests keep working (stateless endpoint)") {
        const int port = find_free_port();
        REQUIRE(port > 0);

        CallbackState st;
        RunningListener rl;
        REQUIRE(rl.start(&recording_callback, &st, port));
        REQUIRE(wait_until_ready(port));

        httplib::Client cli = make_client(port);
        for (int i = 0; i < 3; ++i) {
            const httplib::Response res = post_wake(cli, "ROBOT", "Bearer dispatch-token");
            REQUIRE(res.status == 200);
        }
        CHECK(st.calls.load() == 3);
    }
}

// ---------------------------------------------------------------------------
// Callback contract - alias and context forwarding
// ---------------------------------------------------------------------------
TEST_CASE("callback - receives the exact alias from the URL and the untouched context", "[listener_linux]") {
    TokenEnv env("ctx-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    const char* const aliases[] = {
        "PC",
        "GUEST_1",
        "a",
        "A234567890123456789012345678901",   // maximum valid length (31)
    };

    httplib::Client cli = make_client(port);
    for (const char* a : aliases) {
        INFO("alias: " << a);
        const httplib::Response res = post_wake(cli, a, "Bearer ctx-token");
        REQUIRE(res.status == 200);

        std::string alias_seen;
        {
            std::lock_guard<std::mutex> lk(st.mu);
            alias_seen = st.last_alias;
        }
        CHECK(alias_seen == a);
    }

    CHECK(st.calls.load() == static_cast<int>(sizeof(aliases) / sizeof(aliases[0])));
}

// ---------------------------------------------------------------------------
// Route matching
// ---------------------------------------------------------------------------
TEST_CASE("routes - only POST /wake/<alias> matches", "[listener_linux]") {
    TokenEnv env("routes-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);

    SECTION("GET on /wake/<alias> is not routed (404, the route is POST-only)") {
        const httplib::Response res = get_path(cli, "/wake/PC");
        REQUIRE(res.status == 404);
        CHECK(st.calls.load() == 0);
    }

    SECTION("alias longer than 31 characters is not routed (404)") {
        const httplib::Response res = post_wake(cli, std::string(32, 'A'), "Bearer routes-token");
        REQUIRE(res.status == 404);
        CHECK(st.calls.load() == 0);
    }

    SECTION("alias with 31 characters (maximum) is accepted (200)") {
        const httplib::Response res = post_wake(cli, std::string(31, 'a'), "Bearer routes-token");
        REQUIRE(res.status == 200);
        CHECK(st.calls.load() == 1);
    }

    SECTION("characters outside [A-Za-z0-9_] are not routed (404)") {
        const char* const bad_aliases[] = { "MY-PC", "PC!", "PC.1" };
        for (const char* a : bad_aliases) {
            INFO("alias: " << a);
            const httplib::Response res = post_wake(cli, a, "Bearer routes-token");
            REQUIRE(res.status == 404);
        }
        CHECK(st.calls.load() == 0);
    }

    SECTION("unknown paths return 404") {
        const char* const bad_paths[] = { "/", "/wake", "/wake/", "/wake/PC/extra", "/wakes/PC", "/health" };
        for (const char* p : bad_paths) {
            INFO("path: '" << p << "'");
            const httplib::Response res = get_path(cli, p);
            REQUIRE(res.status == 404);
        }
        CHECK(st.calls.load() == 0);
    }
}
