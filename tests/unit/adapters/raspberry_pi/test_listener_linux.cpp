#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>

#include "adapters/raspberry_pi/listener_linux.hpp"
#include "util/http_helpers.hpp"
#include "util/token_env.hpp"

using corewake::test::TokenEnv;
using corewake::test::find_free_port;
using corewake::test::make_client;
using corewake::test::post_wake;

namespace {

constexpr const char* kTestToken = "s3cr3t-t0k3n-42";

// Contexto compartido con el callback C del listener.
struct CbCtx {
    std::atomic<int>         rc{0};
    std::atomic<int>         calls{0};
    std::atomic<int>         last_alias_len{0};
    char                     last_alias[64]{};
};

int cb(const char* alias, void* context) {
    CbCtx* ctx = static_cast<CbCtx*>(context);
    ctx->calls.fetch_add(1);
    ctx->last_alias_len.store(static_cast<int>(std::string(alias).size()));
    std::string(alias).copy(ctx->last_alias, sizeof(ctx->last_alias) - 1);
    ctx->last_alias[sizeof(ctx->last_alias) - 1] = '\0';
    return ctx->rc.load();
}

} // namespace

// ==========================================================
// ListenerLinux - create
// ==========================================================

TEST_CASE("create - token requirements", "[listener_linux][pi][unit]") {
    CbCtx ctx;

    SECTION("missing COREWAKE_TOKEN returns nullptr") {
        TokenEnv env(nullptr);
        REQUIRE(ListenerLinux::create(&cb, &ctx) == nullptr);
    }

    SECTION("empty COREWAKE_TOKEN returns nullptr") {
        TokenEnv env("");
        REQUIRE(ListenerLinux::create(&cb, &ctx) == nullptr);
    }

    SECTION("valid token returns instance") {
        TokenEnv env(kTestToken);
        ListenerLinux* l = ListenerLinux::create(&cb, &ctx, 0);
        REQUIRE(l != nullptr);
        delete l;
    }

    SECTION("null callback returns nullptr") {
        TokenEnv env(kTestToken);
        REQUIRE(ListenerLinux::create(nullptr, &ctx, 0) == nullptr);
    }

    SECTION("null context returns nullptr") {
        TokenEnv env(kTestToken);
        REQUIRE(ListenerLinux::create(&cb, nullptr, 0) == nullptr);
    }
}

// ==========================================================
// ListenerLinux - puerto
// ==========================================================

TEST_CASE("port - default and explicit", "[listener_linux][pi][unit]") {
    TokenEnv env(kTestToken);
    CbCtx ctx;

    SECTION("port 0 falls back to default 8080") {
        ListenerLinux* l = ListenerLinux::create(&cb, &ctx, 0);
        REQUIRE(l != nullptr);
        REQUIRE(l->port() == 8080);
        delete l;
    }

    SECTION("explicit port is kept") {
        ListenerLinux* l = ListenerLinux::create(&cb, &ctx, 43210);
        REQUIRE(l != nullptr);
        REQUIRE(l->port() == 43210);
        delete l;
    }
}

// ==========================================================
// ListenerLinux - HTTP /wake
// ==========================================================

TEST_CASE("http - POST /wake/<alias> full behavior", "[listener_linux][pi][unit]") {
    TokenEnv env(kTestToken);
    CbCtx ctx;

    const int port = find_free_port();
    REQUIRE(port > 0);

    ListenerLinux* listener = ListenerLinux::create(&cb, &ctx, port);
    REQUIRE(listener != nullptr);

    std::thread server([&] { (void)listener->serve(); });
    REQUIRE(corewake::test::wait_until_ready(port));

    httplib::Client cli = make_client(port);

    SECTION("missing Authorization header -> 401, callback not called") {
        const httplib::Response res = post_wake(cli, "PC", "");
        REQUIRE(res.status == 401);
        REQUIRE(res.body == "unauthorized");
        REQUIRE(ctx.calls.load() == 0);
    }

    SECTION("wrong Bearer token -> 401") {
        const httplib::Response res = post_wake(cli, "PC", "Bearer wrong-token");
        REQUIRE(res.status == 401);
        REQUIRE(ctx.calls.load() == 0);
    }

    SECTION("Authorization without Bearer prefix -> 401") {
        const httplib::Response res = post_wake(cli, "PC", "Basic cGk6cGk=");
        REQUIRE(res.status == 401);
        REQUIRE(ctx.calls.load() == 0);
    }

    SECTION("valid token -> 200 'wake ok', callback invoked with alias") {
        const httplib::Response res = post_wake(cli, "PC", std::string("Bearer ") + kTestToken);
        REQUIRE(res.status == 200);
        REQUIRE(res.body == "wake ok");
        REQUIRE(ctx.calls.load() == 1);
        REQUIRE(std::string(ctx.last_alias) == "PC");
    }

    SECTION("callback -3 (alias not found) -> 404") {
        ctx.rc.store(-3);
        const httplib::Response res = post_wake(cli, "GHOST", std::string("Bearer ") + kTestToken);
        REQUIRE(res.status == 404);
        REQUIRE(res.body == "alias not found");
        REQUIRE(ctx.calls.load() == 1);
        REQUIRE(std::string(ctx.last_alias) == "GHOST");
    }

    SECTION("callback -4 (other error) -> 500") {
        ctx.rc.store(-4);
        const httplib::Response res = post_wake(cli, "PC", std::string("Bearer ") + kTestToken);
        REQUIRE(res.status == 500);
        REQUIRE(res.body == "internal error");
    }

    SECTION("alias with invalid char -> 404 (no route match)") {
        const httplib::Response res = post_wake(cli, "BAD-NAME", std::string("Bearer ") + kTestToken);
        REQUIRE(res.status == 404);
        REQUIRE(ctx.calls.load() == 0);
    }

    SECTION("alias longer than 31 chars -> 404 (no route match)") {
        std::string long_alias(32, 'A');
        const httplib::Response res = post_wake(cli, long_alias, std::string("Bearer ") + kTestToken);
        REQUIRE(res.status == 404);
        REQUIRE(ctx.calls.load() == 0);
    }

    listener->stop();
    server.join();
    delete listener;
}
