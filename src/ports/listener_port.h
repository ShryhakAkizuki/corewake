#ifndef LISTENER_PORT_H
#define LISTENER_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

// Signature
typedef int (*listener_receive_t)(const char* alias, void* callback_context);

#ifdef __cplusplus
}
#endif

#endif // LISTENER_PORT_H