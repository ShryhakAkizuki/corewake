#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "aliases_cache.h"
#include "wol_packet.h"

#include "ports/listener_port.h"
#include "ports/loader_port.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
typedef enum translator_err {
    TRANSLATOR_OK            =  0,  // Ok
    TRANSLATOR_ERR_NULL      = -1,  // Invalid Arg
    TRANSLATOR_ERR_CACHE     = -2,  // Cache Internal Error
    TRANSLATOR_ERR_NOT_FOUND = -3,  // Missing Alias
    TRANSLATOR_ERR_LOADER    = -4,  // System Error
    TRANSLATOR_ERR_WOL       = -5   // Wake Failure
} translator_err_t;

// Data
typedef struct translator {
    loader_port_t* loader_vtable;
    alias_cache_t* alias_cache;
    wol_packet_t* wol_packet;
} translator_t;

// Root
translator_t* translator_create(loader_port_t* loader, alias_cache_t* cache, wol_packet_t* wol);
void translator_destroy(translator_t* self);
int translator_preload(translator_t* self);

// Main
int receive_request(const char* alias, translator_t* self);

// Methods
int resolve_alias(const char* alias, char* mac, translator_t* self);

#ifdef __cplusplus
}
#endif

#endif // TRANSLATOR_H
