#include "wol_engine.h"
#include <stdio.h>
#include <string.h>

int build_magic_packet(const uint8_t* mac, uint8_t* packet) {
    if (mac == NULL || packet == NULL) return -1;

    // Bytes 0-5: FF FF FF FF FF FF
    for (int i = 0; i < 6; i++) {
        packet[i] = 0xFF;
    }

    // Bytes 6-101: MACx16
    for (int i = 0; i < MAC_REPETITIONS; i++) {
        for (int j = 0; j < MAC_ADDRESS_SIZE; j++) {
            packet[6 + (i * MAC_ADDRESS_SIZE) + j] = mac[j];
        }
    }

    return MAGIC_PACKET_SIZE;
}

int parse_mac_string(const char* str, uint8_t* mac) {
    if (str == NULL || mac == NULL) return -1;

    unsigned int bytes[6];
    int count = sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x",
                            &bytes[0], &bytes[1], &bytes[2],
                            &bytes[3], &bytes[4], &bytes[5]);

    if (count != 6) return -1;

    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)bytes[i];
    }

    return 0;
}
