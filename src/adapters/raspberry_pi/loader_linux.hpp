#ifndef LOADER_LINUX_HPP
#define LOADER_LINUX_HPP

#include <cstdio>
#include <mutex>
#include <string>

#include "ports/loader_port.h"

// Error codes
typedef enum loader_linux_err {
    LOADER_LINUX_OK      =  0,  // success
    LOADER_LINUX_ERR_PATH = -1, // invalid file Path
    LOADER_LINUX_ERR_OPEN = -2, // error opening the file
    LOADER_LINUX_ERR_PARSE = -3 // invalid file format
} loader_linux_err_t;

// file_path == nullptr -> <exe_dir>/aliases/aliases.INI
class LoaderLinux {
public:

    static LoaderLinux* create(const char* file_path = nullptr);
    ~LoaderLinux();

    LoaderLinux(const LoaderLinux&) = delete;
    LoaderLinux& operator=(const LoaderLinux&) = delete;
    LoaderLinux(LoaderLinux&&) = delete;
    LoaderLinux& operator=(LoaderLinux&&) = delete;

    loader_port_t* vtable() { return &vtable_; }

private:

    LoaderLinux();

    int init(const char* file_path);

    static int c_fetch(const char* alias, char* mac, loader_port_t* self);
    static int c_first(char* alias, char* mac, loader_port_t* self);
    static int c_next(char* alias, char* mac, loader_port_t* self);

    int fetch_alias(const char* alias, char* mac);
    int first_alias(char* alias, char* mac);
    int next_alias(char* alias, char* mac);

    int validate_file(const char* path);
    int read_entry(FILE* fp, char* alias, char* mac);

    static char* trim(char* s);
    static bool is_hex(char c);
    static bool valid_mac(const char* mac);
    static std::string exe_dir();

    loader_port_t vtable_;

    std::string path_;
    FILE* file_;
    std::mutex mutex_;
};

#endif // LOADER_LINUX_HPP
