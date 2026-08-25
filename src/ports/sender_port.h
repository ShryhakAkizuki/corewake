#ifndef SENDER_PORT_H
#define SENDER_PORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Signature
typedef struct sender_port sender_port_t;

struct sender_port {
    int (*send)(const uint8_t* packet, size_t len, sender_port_t* self);
};

#ifdef __cplusplus
}
#endif

#endif // SENDER_PORT_H