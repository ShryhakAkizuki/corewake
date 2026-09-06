#pragma once

#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "adapters/raspberry_pi/listener_linux.hpp"
#include "adapters/raspberry_pi/loader_linux.hpp"
#include "adapters/raspberry_pi/sender_linux.hpp"

#include "core/aliases_cache.h"
#include "core/translator.h"
#include "core/wol_packet.h"

#include "util/http_helpers.hpp"
#include "util/temp_dir.hpp"
#include "util/token_env.hpp"
#include "util/udp_loopback.hpp"

namespace corewake::test {

class FullPipeline {
public:

    FullPipeline(const char* token, const std::string& ini_content,
                 listener_receive_t on_request, bool preload)
        :   token_env_(token),
            ini_(td_.write_ini(ini_content)),
            loader_(LoaderLinux::create(ini_.c_str())),
            rx_(),
            sender_(SenderLinux::create("127.0.0.1", static_cast<int>(rx_.port))),
            cache_(alias_cache_init()),
            wol_(wol_packet_create(sender_ != nullptr ? sender_->vtable() : nullptr)),
            tr_(translator_create(loader_ != nullptr ? loader_->vtable() : nullptr, cache_, wol_)) {

        REQUIRE(loader_ != nullptr);
        REQUIRE(rx_.valid());
        REQUIRE(sender_ != nullptr);
        REQUIRE(cache_ != nullptr);
        REQUIRE(wol_ != nullptr);
        REQUIRE(tr_ != nullptr);

        if (preload) {
            REQUIRE(translator_preload(tr_) == TRANSLATOR_OK);
        }

        port_ = find_free_port();
        REQUIRE(port_ > 0);

        listener_ = ListenerLinux::create(on_request, tr_, port_);
        REQUIRE(listener_ != nullptr);

        server_ = std::thread([this] { (void)listener_->serve(); });
        REQUIRE(wait_until_ready(port_));
    }

    ~FullPipeline() {
        if (listener_ != nullptr) listener_->stop();
        if (server_.joinable())  server_.join();
        if (listener_ != nullptr) delete listener_;

        if (tr_     != nullptr) translator_destroy(tr_);
        if (wol_    != nullptr) wol_packet_destroy(wol_);
        if (cache_  != nullptr) alias_cache_destroy(cache_);
        if (sender_ != nullptr) delete sender_;
        if (loader_ != nullptr) delete loader_;
    }

    FullPipeline(const FullPipeline&) = delete;
    FullPipeline& operator=(const FullPipeline&) = delete;

    int port() const { return port_; }

    UdpLoopbackReceiver* receiver()   { return &rx_; }
    alias_cache_t*       cache()      { return cache_; }
    translator_t*        translator() { return tr_; }

private:

    TokenEnv            token_env_;   
    TempDir             td_;          
    std::string         ini_;

    LoaderLinux*        loader_   = nullptr;
    UdpLoopbackReceiver rx_;
    SenderLinux*        sender_   = nullptr;
    alias_cache_t*      cache_    = nullptr;
    wol_packet_t*       wol_      = nullptr;
    translator_t*       tr_       = nullptr;
    ListenerLinux*      listener_ = nullptr;
    int                 port_     = 0;
    std::thread         server_;
};

} // namespace corewake::test
