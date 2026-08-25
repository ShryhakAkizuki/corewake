#include "wol_packet.h"
#include <stdio.h>
#include <stdlib.h>

// Methods
int build_magic_packet(const uint8_t mac[MAC_ADDRESS_SIZE], uint8_t packet[MAGIC_PACKET_SIZE]) {
    if (mac == NULL || packet == NULL) return WOL_ERR_NULL;

    // Bytes 0-5: FF FF FF FF FF FF
    for (int i = 0; i < BROADCAST_BYTES; i++) {
        packet[i] = 0xFF;
    }

    // Bytes 6-101: MACx16
    for (int i = 0; i < MAC_REPETITIONS; i++) {
        for (int j = 0; j < MAC_ADDRESS_SIZE; j++) {
            packet[BROADCAST_BYTES + (i * MAC_ADDRESS_SIZE) + j] = mac[j];
        }
    }

    return WOL_OK;
}

int parse_mac_string(const char* str, uint8_t mac[MAC_ADDRESS_SIZE]) {
    if (str == NULL || mac == NULL) return WOL_ERR_NULL;

    unsigned int bytes[MAC_ADDRESS_SIZE];
    int pos = 0;
    int count = sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x%n",
                            &bytes[0], &bytes[1], &bytes[2],
                            &bytes[3], &bytes[4], &bytes[5],
                            &pos);

    if (count != MAC_ADDRESS_SIZE) return WOL_ERR_PARSE;

    // Dispose of trash at the end
    const char* tail = str + pos;
    while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n') tail++;
    if (*tail != '\0') return WOL_ERR_PARSE;

    for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
        mac[i] = (uint8_t)bytes[i];
    }

    return WOL_OK;
}

// Main
int core_wake(const char* str, wol_packet_t* self) {
    if (self == NULL || str == NULL) return WOL_ERR_NULL;

    uint8_t mac_temp[MAC_ADDRESS_SIZE];

    if (parse_mac_string(str, mac_temp) != WOL_OK) return WOL_ERR_PARSE;

    if (build_magic_packet(mac_temp, self->packet_buffer) != WOL_OK) return WOL_ERR_BUILD;

    int send = self->sender_vtable->send(self->packet_buffer, (size_t)MAGIC_PACKET_SIZE, self->sender_vtable);

    return (send == 0) ? WOL_OK : WOL_ERR_SEND;
}

// Root
wol_packet_t* wol_packet_create(sender_port_t* sender) {
    if (sender == NULL || sender->send == NULL) return NULL;

    wol_packet_t* self = (wol_packet_t*)calloc(1, sizeof(wol_packet_t));

    if (self == NULL) return NULL; 

    self->sender_vtable = sender;

    return self;
}

void wol_packet_destroy(wol_packet_t* self) {
    if (self == NULL) return;

    free(self);
}
