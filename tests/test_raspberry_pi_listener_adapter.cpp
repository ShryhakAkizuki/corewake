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
#include <vector>

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

    // value == nullptr -> ensure the variable is unset
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

    TokenEnv(const TokenEnv&)            = delete;
    TokenEnv& operator=(const TokenEnv&) = delete;

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
    ListenerLinux*   listener       = nullptr;
    int              port           = 0;
    std::atomic<bool> serve_returned{false};
    bool             serve_result   = false;
    std::thread      thread;

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

    RunningListener(const RunningListener&)            = delete;
    RunningListener& operator=(const RunningListener&) = delete;

    ~RunningListener() {
        if (listener != nullptr) listener->stop();
        if (thread.joinable()) thread.join();
    }

    void stop_and_join() {
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
// create() factory - success paths
// ---------------------------------------------------------------------------
TEST_CASE("create() - valid COREWAKE_TOKEN yields a usable listener with the default port", "[listener_linux]") {
    TokenEnv env("default-port-token");

    CallbackState st;
    ListenerLinux* listener = nullptr;
    REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st));
    REQUIRE(listener != nullptr);
    REQUIRE(listener->port() == kDefaultPort);

    delete listener;
}

TEST_CASE("create() - explicit port is honored", "[listener_linux]") {
    TokenEnv env("explicit-token");

    CallbackState st;
    ListenerLinux* listener = nullptr;
    REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st, 43210));
    REQUIRE(listener != nullptr);
    REQUIRE(listener->port() == 43210);

    delete listener;
}

TEST_CASE("create() - port 0 falls back to the default port", "[listener_linux]") {
    TokenEnv env("zero-token");

    CallbackState st;
    ListenerLinux* listener = nullptr;
    REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st, 0));
    REQUIRE(listener != nullptr);
    REQUIRE(listener->port() == kDefaultPort);

    delete listener;
}

// ---------------------------------------------------------------------------
// create() factory - failure paths
// ---------------------------------------------------------------------------
TEST_CASE("create() - null callback or null context return nullptr without throwing", "[listener_linux]") {
    TokenEnv env("null-args-token");

    CallbackState st;

    ListenerLinux* l1 = nullptr;
    REQUIRE_NOTHROW(l1 = ListenerLinux::create(nullptr, &st));
    CHECK(l1 == nullptr);

    ListenerLinux* l2 = nullptr;
    REQUIRE_NOTHROW(l2 = ListenerLinux::create(&recording_callback, nullptr));
    CHECK(l2 == nullptr);
}

TEST_CASE("create() - missing COREWAKE_TOKEN returns nullptr", "[listener_linux]") {
    TokenEnv env(nullptr); 

    CallbackState st;
    ListenerLinux* listener = nullptr;
    REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st));
    CHECK(listener == nullptr);
}

TEST_CASE("create() - empty COREWAKE_TOKEN returns nullptr", "[listener_linux]") {
    TokenEnv env("");

    CallbackState st;
    ListenerLinux* listener = nullptr;
    REQUIRE_NOTHROW(listener = ListenerLinux::create(&recording_callback, &st));
    CHECK(listener == nullptr);
}

// ---------------------------------------------------------------------------
// serve() / stop() - lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("serve()/stop() - serves over loopback, answers requests and stops cleanly", "[listener_linux]") {
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

    rl.stop_and_join();
    CHECK(rl.serve_returned.load());
}

TEST_CASE("serve() - a busy port is reported as false without hanging", "[listener_linux]") {
    TokenEnv env("busy-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    // Occupy the port ourselves so that the adapter's bind must fail.
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

    // serve() must fail fast (bind error) and the worker thread must terminate.
    for (int i = 0; i < 100 && !rl.serve_returned.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));

    REQUIRE(rl.serve_returned.load());
    CHECK(rl.serve_result == false);

    rl.stop_and_join();
    ::close(holder);
}

TEST_CASE("destructor - stops a running server and the thread joins (SIGINT pattern)", "[listener_linux]") {
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

    delete listener;   // ~ListenerLinux -> stop() (same pattern as the SIGINT handler in main.cpp)

    t.join();
    REQUIRE(serve_returned.load());
}

TEST_CASE("stop() - is safe to call without having served and to call twice", "[listener_linux]") {
    TokenEnv env("stop-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    ListenerLinux* listener = ListenerLinux::create(&recording_callback, &st, port);
    REQUIRE(listener != nullptr);

    REQUIRE_NOTHROW(listener->stop());   // never served
    REQUIRE_NOTHROW(listener->stop());   // twice
    REQUIRE(listener->port() == port);

    delete listener;
}

// ---------------------------------------------------------------------------
// Authorization - Bearer token
// ---------------------------------------------------------------------------
TEST_CASE("auth - a correct Bearer token is accepted (200)", "[listener_linux]") {
    TokenEnv env("auth-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    const httplib::Response res = post_wake(cli, "PC", "Bearer auth-token");
    REQUIRE(res.status == 200);
    CHECK(res.body == "wake ok");
    CHECK(st.calls.load() == 1);

    rl.stop_and_join();
}

TEST_CASE("auth - missing Authorization header returns 401 and does not call the callback", "[listener_linux]") {
    TokenEnv env("auth-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    const httplib::Response res = post_wake(cli, "PC", "");
    REQUIRE(res.status == 401);
    CHECK(res.body == "unauthorized");
    CHECK(st.calls.load() == 0);

    rl.stop_and_join();
}

TEST_CASE("auth - a wrong token returns 401 and does not call the callback", "[listener_linux]") {
    TokenEnv env("auth-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    const httplib::Response res = post_wake(cli, "PC", "Bearer definitely-not-the-token");
    REQUIRE(res.status == 401);
    CHECK(res.body == "unauthorized");
    CHECK(st.calls.load() == 0);

    rl.stop_and_join();
}

TEST_CASE("auth - headers without a valid Bearer prefix return 401", "[listener_linux]") {
    TokenEnv env("auth-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    const struct { const char* label; const char* header; } bad[] = {
        { "different scheme",            "Token auth-token" },
        { "the word 'Bearer' alone",     "Bearer" },
        { "prefix without space",        "BearerX auth-token" },
        { "lowercase scheme",            "bearer auth-token" },
    };

    httplib::Client cli = make_client(port);
    for (const auto& b : bad) {
        INFO("header: '" << b.header << "'");
        const httplib::Response res = post_wake(cli, "PC", b.header);
        REQUIRE(res.status == 401);
        CHECK(res.body == "unauthorized");
    }
    CHECK(st.calls.load() == 0);

    rl.stop_and_join();
}

// ---------------------------------------------------------------------------
// Dispatch - callback result code -> HTTP status
// ---------------------------------------------------------------------------
TEST_CASE("dispatch - callback rc 0 maps to 200 'wake ok'", "[listener_linux]") {
    TokenEnv env("dispatch-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;   // rc defaults to 0
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    const httplib::Response res = post_wake(cli, "PC", "Bearer dispatch-token");
    REQUIRE(res.status == 200);
    CHECK(res.body == "wake ok");
    CHECK(st.calls.load() == 1);

    rl.stop_and_join();
}

TEST_CASE("dispatch - callback rc -3 maps to 404 'alias not found'", "[listener_linux]") {
    TokenEnv env("dispatch-token");
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

    rl.stop_and_join();
}

TEST_CASE("dispatch - any other non-zero rc maps to 500 'internal error'", "[listener_linux]") {
    TokenEnv env("dispatch-token");
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

        // A fresh port per iteration: never rely on re-binding a just-closed socket.
        rl.stop_and_join();
        port = find_free_port();
        REQUIRE(port > 0);
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

        // The callback already ran (the response follows it), so locking the same
        // mutex establishes a happens-before edge with the writer thread.
        const std::string alias_seen;
        {
            std::lock_guard<std::mutex> lk(st.mu);
            alias_seen = st.last_alias;
        }
        CHECK(alias_seen == a);
    }

    // recording_callback resolves its state through the context pointer; if the
    // adapter had corrupted callback_context, none of these counters would move.
    CHECK(st.calls.load() == static_cast<int>(sizeof(aliases) / sizeof(aliases[0])));

    rl.stop_and_join();
}

TEST_CASE("dispatch - the endpoint is stateless: repeated requests keep working", "[listener_linux]") {
    TokenEnv env("stateless-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    for (int i = 0; i < 3; ++i) {
        const httplib::Response res = post_wake(cli, "ROBOT", "Bearer stateless-token");
        REQUIRE(res.status == 200);
    }
    CHECK(st.calls.load() == 3);

    rl.stop_and_join();
}

// ---------------------------------------------------------------------------
// Route matching
// ---------------------------------------------------------------------------
TEST_CASE("routes - GET on /wake/<alias> returns 405 (the route is POST-only)", "[listener_linux]") {
    TokenEnv env("routes-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);
    const httplib::Response res = get_path(cli, "/wake/PC");
    REQUIRE(res.status == 405);
    CHECK(st.calls.load() == 0);

    rl.stop_and_join();
}

TEST_CASE("routes - an alias longer than 31 characters is not routed (404)", "[listener_linux]") {
    TokenEnv env("routes-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    httplib::Client cli = make_client(port);

    SECTION("31 characters (maximum) is accepted") {
        const httplib::Response res = post_wake(cli, std::string(31, 'a'), "Bearer routes-token");
        REQUIRE(res.status == 200);
        CHECK(st.calls.load() == 1);
    }

    SECTION("32 characters is rejected by the route") {
        const httplib::Response res = post_wake(cli, std::string(32, 'A'), "Bearer routes-token");
        REQUIRE(res.status == 404);
        CHECK(st.calls.load() == 0);
    }
}

TEST_CASE("routes - aliases with characters outside [A-Za-z0-9_] are not routed (404)", "[listener_linux]") {
    TokenEnv env("routes-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    const char* const bad_aliases[] = { "MY-PC", "PC!", "PC.1" };

    httplib::Client cli = make_client(port);
    for (const char* a : bad_aliases) {
        INFO("alias: " << a);
        const httplib::Response res = post_wake(cli, a, "Bearer routes-token");
        REQUIRE(res.status == 404);
    }
    CHECK(st.calls.load() == 0);

    rl.stop_and_join();
}

TEST_CASE("routes - unknown paths return 404", "[listener_linux]") {
    TokenEnv env("routes-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    const char* const bad_paths[] = { "/", "/wake", "/wake/", "/wake/PC/extra", "/wakes/PC", "/health" };

    httplib::Client cli = make_client(port);
    for (const char* p : bad_paths) {
        INFO("path: '" << p << "'");
        const httplib::Response res = get_path(cli, p);
        REQUIRE(res.status == 404);
    }
    CHECK(st.calls.load() == 0);

    rl.stop_and_join();
}

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------
TEST_CASE("concurrency - parallel wake requests are all handled", "[listener_linux][integration]") {
    TokenEnv env("conc-token");
    const int port = find_free_port();
    REQUIRE(port > 0);

    CallbackState st;
    RunningListener rl;
    REQUIRE(rl.start(&recording_callback, &st, port));
    REQUIRE(wait_until_ready(port));

    constexpr int kClients = 16;
    std::atomic<int> ok{0};
    std::vector<std::thread> workers;
    workers.reserve(kClients);

    for (int i = 0; i < kClients; ++i) {
        workers.emplace_back([&, i] {
            httplib::Client cli = make_client(port);
            const httplib::Response res = post_wake(cli, "WORKER_" + std::to_string(i), "Bearer conc-token");
            if (res.status == 200) ok.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& w : workers) w.join();

    REQUIRE(ok.load() == kClients);
    REQUIRE(st.calls.load() == kClients);

    rl.stop_and_join();
}
