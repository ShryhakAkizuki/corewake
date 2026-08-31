// Composition root — Raspberry Pi 5 (Linux).
//
// Use: COREWAKE_TOKEN=<token> ./corewake_pi
//   p.ej.:
//     POST /wake/PC  (Authorization: Bearer <token>) -> send the magic packet

#include <csignal>
#include <cstdio>

#include "core/aliases_cache.h"
#include "core/translator.h"
#include "core/wol_packet.h"

#include "adapters/raspberry_pi/loader_linux.hpp"
#include "adapters/raspberry_pi/sender_linux.hpp"
#include "adapters/raspberry_pi/listener_linux.hpp"

namespace {

ListenerLinux* g_listener = nullptr;

void on_sigint(int) {
    if (g_listener != nullptr) g_listener->stop();
}

int on_wake_request(const char* alias, void* user) {
    return receive_request(alias, static_cast<translator_t*>(user));
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // 1) Adapters
    // ------------------------------------------------------------------
    LoaderLinux* loader = LoaderLinux::create();  // <exe_dir>/aliases/aliases.INI
    if (loader == nullptr) {
        std::fprintf(stderr, "[corewake] error: LoaderLinux::create() [Adapter]\n");
        return 1;
    }

    // 255.255.255.255:9 (default WOL)
    SenderLinux* sender = SenderLinux::create(nullptr, 9);
    if (sender == nullptr) {
        std::fprintf(stderr, "[corewake] error: SenderLinux::create() [Adapter]\n");
        delete loader;
        return 1;
    }

    // ------------------------------------------------------------------
    // 2) Core
    // ------------------------------------------------------------------
    alias_cache_t* cache = alias_cache_init();
    wol_packet_t*  wp    = wol_packet_create(sender->vtable());

    if (cache == nullptr || wp == nullptr) {
        std::fprintf(stderr, "[corewake] error: init del core [Cache/WolPacket]\n");
        if (cache != nullptr) alias_cache_destroy(cache);
        if (wp != nullptr)    wol_packet_destroy(wp);
        delete sender;
        delete loader;
        return 1;
    }

    translator_t* tr = translator_create(loader->vtable(), cache, wp);
    if (tr == nullptr) {
        std::fprintf(stderr, "[corewake] error: translator_create() [Core]\n");
        alias_cache_destroy(cache);
        wol_packet_destroy(wp);
        delete sender;
        delete loader;
        return 1;
    }

    // ------------------------------------------------------------------
    // 3) Initialization
    // ------------------------------------------------------------------
    const int pre = translator_preload(tr);
    if (pre != TRANSLATOR_OK) {
        std::fprintf(stderr, "[corewake] aviso: translator_preload() -> %d (el loader sigue resolviendo por fetch)\n", pre);
    } else {
        std::printf("[corewake] preload OK: %zu alias en cache\n", cache->count);
    }

    // ------------------------------------------------------------------
    // 4) HTTP Listener (POST /wake/<alias>, Bearer token)
    // ------------------------------------------------------------------
    ListenerLinux* listener = ListenerLinux::create(&on_wake_request, tr);
    if (listener == nullptr) {
        std::fprintf(stderr, "[corewake] error: ListenerLinux::create() [Adapter] (verificar COREWAKE_TOKEN)\n");
        translator_destroy(tr);
        wol_packet_destroy(wp);
        alias_cache_destroy(cache);
        delete sender;
        delete loader;
        return 1;
    }

    g_listener = listener;
    std::signal(SIGINT, on_sigint);

    std::printf("[corewake] escuchando en 0.0.0.0:%d — Ctrl+C para salir\n", listener->port());

    listener->serve();  // blocks until stop()

    // ------------------------------------------------------------------
    // 5) Teardown
    // ------------------------------------------------------------------
    delete listener;
    g_listener = nullptr;

    translator_destroy(tr);
    wol_packet_destroy(wp);
    alias_cache_destroy(cache);
    delete sender;
    delete loader;

    std::printf("[corewake] cerrado\n");
    return 0;
}
