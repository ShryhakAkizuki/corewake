#ifndef LOADER_PORT_H
#define LOADER_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
typedef enum loader_err {
    LOADER_OK            =  0,  // Ok
    LOADER_END           =  1,  // Iteration End
    LOADER_ERR_NULL      = -1,  // Invalid Arg
    LOADER_ERR_NOT_FOUND = -2,  // Missing Alias
    LOADER_ERR_IO        = -3   // System Error
} loader_err_t;

// Signature
typedef struct loader_port loader_port_t;

struct loader_port {
    int (*fetch)(const char* alias, char* mac, loader_port_t* self);
    int (*first)(char* alias, char* mac, loader_port_t* self);
    int (*next)(char* alias, char* mac, loader_port_t* self);
};

#ifdef __cplusplus
}
#endif

#endif // LOADER_PORT_H