#include "translator.h"

#include <string.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
#else 
    #error "Unsupported platform"
#endif

int load_file(const char* executable_path, FILE** file) {

}

int get_executable_dir(char* path, size_t size) {
    if (path == NULL || size == 0) return -1;

    #if defined(_WIN32) || defined(_WIN64)
        DWORD len = GetModuleFileNameA(NULL, path, (DWORD)size);
        if (len == 0 || len == size) return -1;

    #elif defined(__linux__)
        ssize_t len = readlink("/proc/self/exe", path, size - 1);
        
        if (len == -1) return -2;

        path[len] = '\0';
    #else 
        #error "Unsupported platform"
    #endif
    return 0;
}