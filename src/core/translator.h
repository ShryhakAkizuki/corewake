#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#define REGISTER_RELATIVE_PATH  "/config/"
#define FILE_NAME  "aliases.conf"

int resolve_alias(const char* alias, const char* mac);
int search_file(const char* path);

#ifdef __cplusplus
}
#endif

#endif // TRANSLATOR_H
