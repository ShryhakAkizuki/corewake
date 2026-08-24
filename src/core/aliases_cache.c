#include "aliases_cache.h"
#include <stdlib.h>
#include <string.h>

// Methods
int alias_cache_search(const char* alias, char mac[MAC_STR_BUF_SIZE], alias_cache_t* self) {
    if (alias == NULL || mac == NULL || self == NULL) return ALIAS_ERR_NULL;

    const alias_entry_t* found = binary_search(alias, self);

    if (found == NULL) return ALIAS_ERR_NOT_FOUND;

    for (size_t i = 0; ;i++){
        mac[i] = found->mac[i];
        if (found->mac[i] == '\0') break;
    }

    return ALIAS_OK;
}

int alias_cache_insert(const char* alias, const char* mac, alias_cache_t* self) {
    if (alias == NULL || mac == NULL || self == NULL) return ALIAS_ERR_NULL;

    if (alias[0] == '\0') return ALIAS_ERR_INVALID_LEN;
    if (strlen(alias) > ALIAS_MAX_LEN || strlen(mac) > MAC_STR_MAX_LEN) return ALIAS_ERR_INVALID_LEN;

    if (binary_search(alias, self) != NULL) return ALIAS_ERR_DUPLICATE;

    if (self->count >= ALIAS_CACHE_MAX_ENTRIES) return ALIAS_ERR_FULL;

    size_t data_len = strlen(alias) + 1 + strlen(mac) + 1;
    if (self->pool_used + data_len > ALIAS_CACHE_POOL_SIZE) return ALIAS_ERR_POOL_FULL;

    char* pool_alias = pool_alloc(alias, self);
    char* pool_mac = (pool_alias != NULL) ? pool_alloc(mac, self) : NULL;
    if (pool_alias == NULL || pool_mac == NULL) return ALIAS_ERR_POOL_FULL;

    size_t pos = find_insert_pos(alias, self);

    for (size_t i = self->count; i > pos; i--) {
        self->entries[i] = self->entries[i-1];
    }

    self->entries[pos].alias = pool_alias;
    self->entries[pos].mac = pool_mac;
    self->count++;

    return ALIAS_OK;
}

void alias_cache_clear(alias_cache_t* self) {
    if (self == NULL) return;

    self->count = 0;
    self->pool_used = 0;
}

// Helper
char* pool_alloc(const char* data, alias_cache_t* self) {
    size_t len = strlen(data) + 1;

    if (self->pool_used + len > ALIAS_CACHE_POOL_SIZE) return NULL;

    char* offset = self->pool + self->pool_used;

    for (size_t i = 0; i < len; i++) {
        offset[i] = data[i];
    }

    self->pool_used += len;

    return offset;
}

size_t find_insert_pos(const char* alias, alias_cache_t* self) {
    size_t low = 0;
    size_t high = self->count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (strcmp(self->entries[mid].alias, alias) < 0) low = mid + 1;
        else high = mid;
    }

    return low;
}

alias_entry_t* binary_search(const char* alias, alias_cache_t* self) {
    size_t low = 0;
    size_t high = self->count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int comparison = strcmp(self->entries[mid].alias, alias);

        if (comparison == 0) return &self->entries[mid];
        if (comparison < 0) low = mid + 1;
        else high = mid;
    }

    return NULL;
}

// Root
alias_cache_t* alias_cache_init(void) {
    alias_cache_t* self = (alias_cache_t*)calloc(1, sizeof(alias_cache_t));

    return self;
}

void alias_cache_destroy(alias_cache_t* self) {
    if (self == NULL) return;

    free(self);
}
