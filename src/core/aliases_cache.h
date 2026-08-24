#ifndef ALIASES_CACHE_H
#define ALIASES_CACHE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIAS_CACHE_MAX_ENTRIES  16
#define ALIAS_MAX_LEN            31
#define MAC_STR_MAX_LEN          17
#define MAC_STR_BUF_SIZE         (MAC_STR_MAX_LEN + 1)

#define ALIAS_CACHE_POOL_SIZE    (ALIAS_CACHE_MAX_ENTRIES * (ALIAS_MAX_LEN + 1 + MAC_STR_MAX_LEN + 1))

// Error codes
typedef enum alias_err {
    ALIAS_OK              =  0,  // success
    ALIAS_ERR_NULL        = -1,  // null pointer
    ALIAS_ERR_INVALID_LEN = -2,  // Alias or MAC address is too long
    ALIAS_ERR_DUPLICATE   = -3,  // The alias already exists
    ALIAS_ERR_FULL        = -4,  // No entries available
    ALIAS_ERR_POOL_FULL   = -5,  // No space in the pool
    ALIAS_ERR_NOT_FOUND   = -6   // The alias does not exist
} alias_err_t;

// Data
typedef struct {
    char* alias;
    char* mac;
} alias_entry_t;

typedef struct {    
    // Array
    alias_entry_t entries[ALIAS_CACHE_MAX_ENTRIES];
    size_t count;

    // Pool
    char pool[ALIAS_CACHE_POOL_SIZE];
    size_t pool_used;
} alias_cache_t;

// Root
alias_cache_t* alias_cache_init(void);
void alias_cache_destroy(alias_cache_t* self);

// Helper
char* pool_alloc(const char* data, alias_cache_t* self);
size_t find_insert_pos(const char* alias,  alias_cache_t* self);
alias_entry_t* binary_search(const char* alias, alias_cache_t* self);

// Methods
int alias_cache_search(const char* alias, char mac[MAC_STR_BUF_SIZE], alias_cache_t* self);
int alias_cache_insert(const char* alias, const char* mac, alias_cache_t* self);
void alias_cache_clear(alias_cache_t* self);

#ifdef __cplusplus
}
#endif

#endif // ALIASES_CACHE_H
