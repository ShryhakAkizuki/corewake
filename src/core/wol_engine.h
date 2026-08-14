#ifndef WOL_ENGINE_H
#define WOL_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAGIC_PACKET_SIZE 102
#define MAC_ADDRESS_SIZE  6
#define MAC_REPETITIONS   16

int build_magic_packet(const uint8_t* mac, uint8_t* packet);
int parse_mac_string(const char* str, uint8_t* mac);

#ifdef __cplusplus
}
#endif

#endif // WOL_ENGINE_H
