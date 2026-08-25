// platform/raspberry_pi/main.cpp
//
// Composition root — Raspberry Pi 5 (Linux).
// Flujo base de prueba: SenderLinux + wol_packet + core_wake.
//
// Uso: ./corewake_pi [MAC]
//   p.ej.: ./corewake_pi AA:BB:CC:DD:EE:FF

#include <cstdio>
#include <exception>
#include <string>

#include "core/wol_packet.h"
#include "adapters/raspberry_pi/sender_linux.hpp"

int main(int argc, char* argv[]) {
    // MAC por defecto (prueba); se puede sobrescribir desde la CLI
    const char* mac = (argc > 1) ? argv[1] : "AA:BB:CC:DD:EE:FF";

    try {
        // El sender nace primero y muere último (LIFO):
        // todo sender_port_t* emitido por él debe seguir vivo mientras exista.
        SenderLinux sender(nullptr, 9);  // 255.255.255.255:9 (WOL estándar)

        wol_packet_t* wp = wol_packet_create(sender.vtable());
        if (wp == nullptr) {
            std::fprintf(stderr, "[corewake] error: no se pudo crear wol_packet\n");
            return 1;
        }

        const int rc = core_wake(mac, wp);

        if (rc == WOL_OK) {
            std::printf("[corewake] magic packet enviado a %s\n", mac);
        } else {
            std::fprintf(stderr, "[corewake] error: core_wake(%s) -> %d\n", mac, rc);
        }

        // wp se destruye ANTES que sender (sender sigue vivo hasta cerrar scope)
        wol_packet_destroy(wp);
        return (rc == WOL_OK) ? 0 : 1;
        // sender se destruye aquí, al salir del scope — último en morir ✔
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[corewake] error: %s\n", e.what());
        return 1;
    }
}
