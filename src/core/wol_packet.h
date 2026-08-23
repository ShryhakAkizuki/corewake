#ifndef WOL_PACKET_H
#define WOL_PACKET_H

#include <stdint.h>
#include <stddef.h>
#include "ports/sender_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BROADCAST_BYTES   6
#define MAC_ADDRESS_SIZE  6
#define MAC_REPETITIONS   16
#define MAGIC_PACKET_SIZE (BROADCAST_BYTES + MAC_REPETITIONS * MAC_ADDRESS_SIZE)

// Códigos de error
typedef enum wol_err {
    WOL_OK         =  0,  // success
    WOL_ERR_NULL   = -1,  // null pointer
    WOL_ERR_PARSE  = -2,  // invalid MAC format
    WOL_ERR_BUILD  = -3,  // failure to build the packet
    WOL_ERR_SEND   = -4   // failure to send (reported by the port)
} wol_err_t;

// Data
typedef struct wol_packet {
    uint8_t packet_buffer[MAGIC_PACKET_SIZE];
    sender_port_t* sender_vtable;
} wol_packet_t;

// Root
wol_packet_t* wol_packet_create(sender_port_t* sender);
void wol_packet_destroy(wol_packet_t* self);

// Main
int wake(wol_packet_t* self, const char* str);

// Methods
int build_magic_packet(const uint8_t mac[MAC_ADDRESS_SIZE], uint8_t packet[MAGIC_PACKET_SIZE]);
int parse_mac_string(const char* str, uint8_t mac[MAC_ADDRESS_SIZE]);

#ifdef __cplusplus
}
#endif

#endif // WOL_PACKET_H
