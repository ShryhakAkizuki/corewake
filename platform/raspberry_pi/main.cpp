// platform/raspberry_pi/main.cpp
//
// Composition root — Raspberry Pi 5 (Linux).
//
// Carga todos los alias del INI hacia la cache al iniciar y escucha
// permanentemente alias desde la consola (stdin) a modo de prueba manual.
// Cada alias ingresado se traduce a MAC (cache -> loader) y dispara un
// magic packet UDP.
//
// Use: ./corewake_pi
//   p.ej.:
//     PC   -> traduce "PC" y envia el magic packet
//     exit -> finaliza el servicio

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
    // 3) Preload: cargar toda la informacion al iniciar hacia la cache
    // ------------------------------------------------------------------
    const int pre = translator_preload(tr);
    if (pre != TRANSLATOR_OK) {
        std::fprintf(stderr, "[corewake] aviso: translator_preload() -> %d (el loader sigue resolviendo por fetch)\n", pre);
    } else {
        std::printf("[corewake] preload OK: %zu alias en cache\n", cache->count);
    }

    // ------------------------------------------------------------------
    // 4) Escucha permanente de alias (stdin) — prueba manual del listener
    // ------------------------------------------------------------------
    std::printf("[corewake] esperando alias (stdin) — escribe 'exit' para salir\n");

    char line[128];

    for (;;) {
        std::printf("alias> ");
        std::fflush(stdout);

        if (std::fgets(line, sizeof(line), stdin) == nullptr)
            break;  // EOF (Ctrl-D o redirección agotada)

        // Se retira únicamente el newline de la entrada; el alias se usa tal cual.
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') continue;  // línea vacía
        if (std::strcmp(line, "exit") == 0) break;

        const int rc = receive_request(line, tr);

        switch (rc) {
            case TRANSLATOR_OK:
                std::printf("[corewake] OK: magic packet enviado para '%s'\n", line);
                break;
            case TRANSLATOR_ERR_NOT_FOUND:
                std::fprintf(stderr, "[corewake] error: alias '%s' no existe en el INI\n", line);
                break;
            case TRANSLATOR_ERR_WOL:
                std::fprintf(stderr, "[corewake] error: fallo enviando el magic packet de '%s'\n", line);
                break;
            default:
                std::fprintf(stderr, "[corewake] error: receive_request('%s') -> %d\n", line, rc);
                break;
        }
    }

    // ------------------------------------------------------------------
    // 5) Teardown (inverso al orden de creacion)
    // ------------------------------------------------------------------
    translator_destroy(tr);
    wol_packet_destroy(wp);
    alias_cache_destroy(cache);
    delete sender;
    delete loader;

    std::printf("[corewake] cerrado\n");
    return 0;
}
