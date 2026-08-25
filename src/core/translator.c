#include "translator.h"
#include <stdlib.h>

// Methods
int resolve_alias(const char* alias, char* mac, translator_t* self) {
    if (alias == NULL || mac == NULL || self == NULL) return TRANSLATOR_ERR_NULL;

    int search = alias_cache_search(alias, mac, self->alias_cache);

    if (search == ALIAS_OK) return TRANSLATOR_OK;
    if (search != ALIAS_ERR_NOT_FOUND) return TRANSLATOR_ERR_CACHE;

    int load = self->loader_vtable->fetch(alias, mac, self->loader_vtable);

    if (load == LOADER_OK) {
        (void)alias_cache_insert(alias, mac, self->alias_cache);
        return TRANSLATOR_OK;
    }

    if (load == LOADER_ERR_NOT_FOUND) return TRANSLATOR_ERR_NOT_FOUND;

    return TRANSLATOR_ERR_LOADER;
}

// Main
int receive_request(const char* alias, translator_t* self) {
    if (self == NULL || alias == NULL) return TRANSLATOR_ERR_NULL;

    char mac_temp[MAC_STR_BUF_SIZE];

    int resolve = resolve_alias(alias, mac_temp, self);

    if (resolve != TRANSLATOR_OK) return resolve;

    if (core_wake(mac_temp, self->wol_packet) != WOL_OK) return TRANSLATOR_ERR_WOL;

    return TRANSLATOR_OK;
}

// Root
translator_t* translator_create(loader_port_t* loader, alias_cache_t* cache, wol_packet_t* wol) {
    if (loader == NULL || cache == NULL || wol == NULL) return NULL;

    translator_t* self = (translator_t*)calloc(1, sizeof(translator_t));

    if (self == NULL) return NULL;

    self->loader_vtable = loader;
    self->alias_cache = cache;
    self->wol_packet = wol;

    return self;
}

void translator_destroy(translator_t* self) {
    if (self == NULL) return;

    free(self);
}

int translator_preload(translator_t* self) {
    if (self == NULL) return TRANSLATOR_ERR_NULL;

    char alias[ALIAS_MAX_LEN + 1];
    char mac[MAC_STR_BUF_SIZE];

    int loader = self->loader_vtable->first(alias, mac, self->loader_vtable);

    while (loader == LOADER_OK) {
        (void)alias_cache_insert(alias, mac, self->alias_cache);
        loader = self->loader_vtable->next(alias, mac, self->loader_vtable);
    }

    if (loader == LOADER_END) return TRANSLATOR_OK;

    return TRANSLATOR_ERR_LOADER;
}
