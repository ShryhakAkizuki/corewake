// Composition root — Raspberry Pi 5 (Linux).
//
// Use: ./corewake_pi
//   p.ej.:
//     PC   -> translate "PC" and send the magic packet
//     exit -> finish the service

#include <cstdio>
#include <cstring>

#include "core/aliases_cache.h"
#include "core/translator.h"
#include "core/wol_packet.h"

#include "adapters/raspberry_pi/loader_linux.hpp"
#include "adapters/raspberry_pi/sender_linux.hpp"

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
    // 4) Temporal Console Alias Listener (stdin)
    // ------------------------------------------------------------------
    std::printf("[corewake] esperando alias (stdin) — escribe 'exit' para salir\n");

    char line[128];

    for (;;) {
        std::printf("alias> ");
        std::fflush(stdout);

        if (std::fgets(line, sizeof(line), stdin) == nullptr)
            break;  // EOF

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') continue;  // línea vacía
        if (std::strcmp(line, "exit") == 0) break;

        const int rc = receive_request(line, tr);

        switch (rc) {
            case TRANSLATOR_OK:
                std::printf("[corewake] OK: magic packet sent to '%s'\n", line);
                break;
            case TRANSLATOR_ERR_NOT_FOUND:
                std::fprintf(stderr, "[corewake] error: alias '%s' doesn't exist on INI file\n", line);
                break;
            case TRANSLATOR_ERR_WOL:
                std::fprintf(stderr, "[corewake] error: error sending the magic packet of '%s'\n", line);
                break;
            default:
                std::fprintf(stderr, "[corewake] error: receive_request('%s') -> %d\n", line, rc);
                break;
        }
    }

    // ------------------------------------------------------------------
    // 5) Teardown
    // ------------------------------------------------------------------
    translator_destroy(tr);
    wol_packet_destroy(wp);
    alias_cache_destroy(cache);
    delete sender;
    delete loader;

    std::printf("[corewake] cerrado\n");
    return 0;
}
