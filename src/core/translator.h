#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>

#define REGISTER_RELATIVE_PATH  "config/"
#define FILE_NAME  "aliases.conf"

int resolve_alias(FILE* fp, const char* alias, const char* mac);
int load_file(const char* executable_path, FILE** file);
int get_executable_dir(char* path, size_t size);

#ifdef __cplusplus
}
#endif

#endif // TRANSLATOR_H
