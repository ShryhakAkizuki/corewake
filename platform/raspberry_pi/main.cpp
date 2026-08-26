// platform/raspberry_pi/main.cpp
//
// Composition root — Raspberry Pi 5 (Linux).
//
// Use: ./corewake_pi [MAC]
//   p.ej.: ./corewake_pi AA:BB:CC:DD:EE:FF

#include <cstdio>

#include "core/wol_packet.h"
#include "adapters/raspberry_pi/sender_linux.hpp"

int main(int argc, char* argv[]) {
    // Test default mac
    const char* mac = (argc > 1) ? argv[1] : "AA:BB:CC:DD:EE:FF";

    // 255.255.255.255:9 (default WOL)
    SenderLinux* sender = SenderLinux::create(nullptr, 9);  

    if (sender == nullptr) {
        std::fprintf(stderr, "[corewake] error: SenderLinux Init [Adapter]\n");
        return 1;
    }

    wol_packet_t* wp = wol_packet_create(sender->vtable());

    if (wp == nullptr) {
        std::fprintf(stderr, "[corewake] error: wol_packet Init [Core]\n");
        delete sender;
        return 1;
    }

    const int rc = core_wake(mac, wp);

    if (rc == WOL_OK) {
        std::printf("[corewake] magic packet sent to %s\n", mac);
    } else {
        std::fprintf(stderr, "[corewake] error: core_wake(%s) -> %d\n", mac, rc);
    }

    wol_packet_destroy(wp);
    delete sender;
    return (rc == WOL_OK) ? 0 : 1;
}
