#include "loader_linux.hpp"

#include <cstddef>
#include <cstring>
#include <new>

#include <limits.h>
#include <unistd.h>

namespace {

constexpr std::size_t kAliasBufSize = 32;  // ALIAS_MAX_LEN + 1
constexpr std::size_t kMacBufSize   = 18;  // MAC_STR_MAX_LEN + 1
constexpr std::size_t kLineSize     = 128;

const char kDefaultDir[]  = "aliases";
const char kDefaultFile[] = "aliases.INI";

} // namespace

LoaderLinux::LoaderLinux()
    :   vtable_{nullptr},
        path_(),
        file_(nullptr) {
}

LoaderLinux* LoaderLinux::create(const char* file_path) {
    LoaderLinux* self = new (std::nothrow) LoaderLinux();

    if (self == nullptr) return nullptr;

    if (self->init(file_path) != LOADER_LINUX_OK) {
        delete self;
        return nullptr;
    }

    return self;
}

int LoaderLinux::init(const char* file_path) {
    if (file_path != nullptr && file_path[0] != '\0') {
        path_ = file_path;
    } else {
        const std::string dir = exe_dir();
        if (dir.empty()) return LOADER_LINUX_ERR_PATH;
        path_ = dir + "/" + kDefaultDir + "/" + kDefaultFile;
    }

    const int rc = validate_file(path_.c_str());
    if (rc != LOADER_LINUX_OK) return rc;

    file_ = std::fopen(path_.c_str(), "r");
    if (file_ == nullptr) return LOADER_LINUX_ERR_OPEN;

    vtable_.fetch = &LoaderLinux::c_fetch;
    vtable_.first = &LoaderLinux::c_first;
    vtable_.next  = &LoaderLinux::c_next;

    return LOADER_LINUX_OK;
}

LoaderLinux::~LoaderLinux() {
    if (file_ != nullptr) std::fclose(file_);
}

int LoaderLinux::validate_file(const char* path) {
    FILE* fp = std::fopen(path, "r");
    if (fp == nullptr) return LOADER_LINUX_ERR_OPEN;

    char line[kLineSize];
    bool in_section = false;

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        char* t = trim(line);
        if (t[0] == '\0' || t[0] == '#' || t[0] == ';') continue;

        if (t[0] == '[') {
            if (in_section || std::strcmp(t, "[aliases]") != 0) {
                std::fclose(fp);
                return LOADER_LINUX_ERR_PARSE;
            }
            in_section = true;
            continue;
        }

        char* eq = std::strchr(t, '=');
        if (eq == nullptr || !in_section) { std::fclose(fp); return LOADER_LINUX_ERR_PARSE; }
        *eq = '\0';

        const std::size_t alen = std::strlen(trim(t));
        if (alen == 0 || alen >= kAliasBufSize) { std::fclose(fp); return LOADER_LINUX_ERR_PARSE; }
        if (!valid_mac(trim(eq + 1)))           { std::fclose(fp); return LOADER_LINUX_ERR_PARSE; }
    }

    if (!in_section) { std::fclose(fp); return LOADER_LINUX_ERR_PARSE; }

    std::fclose(fp);
    return LOADER_LINUX_OK;
}

int LoaderLinux::fetch_alias(const char* alias, char* mac) {
    FILE* fp = std::fopen(path_.c_str(), "r");
    if (fp == nullptr) return LOADER_ERR_IO;

    char line[kLineSize];

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        char* t = trim(line);
        if (t[0] == '\0' || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;

        char* eq = std::strchr(t, '=');
        if (eq == nullptr) continue;
        *eq = '\0';

        if (std::strcmp(trim(t), alias) == 0) {
            std::snprintf(mac, kMacBufSize, "%s", trim(eq + 1));
            std::fclose(fp);
            return LOADER_OK;
        }
    }

    std::fclose(fp);
    return LOADER_ERR_NOT_FOUND;
}

int LoaderLinux::first_alias(char* alias, char* mac) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fseek(file_, 0, SEEK_SET);
    return read_entry(file_, alias, mac);
}

int LoaderLinux::next_alias(char* alias, char* mac) {
    std::lock_guard<std::mutex> lock(mutex_);
    return read_entry(file_, alias, mac);
}

int LoaderLinux::read_entry(FILE* fp, char* alias, char* mac) {
    char line[kLineSize];

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        char* t = trim(line);
        if (t[0] == '\0' || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[') continue;

        char* eq = std::strchr(t, '=');
        if (eq == nullptr) return LOADER_ERR_IO;
        *eq = '\0';

        std::snprintf(alias, kAliasBufSize, "%s", trim(t));
        std::snprintf(mac,   kMacBufSize,   "%s", trim(eq + 1));
        return LOADER_OK;
    }

    return LOADER_END;
}

int LoaderLinux::c_fetch(const char* alias, char* mac, loader_port_t* self) {
    if (alias == nullptr || mac == nullptr || self == nullptr) return LOADER_ERR_NULL;

    LoaderLinux* sl = reinterpret_cast<LoaderLinux*>(self);

    return sl->fetch_alias(alias, mac);
}

int LoaderLinux::c_first(char* alias, char* mac, loader_port_t* self) {
    if (alias == nullptr || mac == nullptr || self == nullptr) return LOADER_ERR_NULL;

    LoaderLinux* sl = reinterpret_cast<LoaderLinux*>(self);

    return sl->first_alias(alias, mac);
}

int LoaderLinux::c_next(char* alias, char* mac, loader_port_t* self) {
    if (alias == nullptr || mac == nullptr || self == nullptr) return LOADER_ERR_NULL;

    LoaderLinux* sl = reinterpret_cast<LoaderLinux*>(self);

    return sl->next_alias(alias, mac);
}

char* LoaderLinux::trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    char* end = s + std::strlen(s);
    while (end > s && (end[-1] == ' '  || end[-1] == '\t' ||
                        end[-1] == '\r' || end[-1] == '\n'))
        --end;
    *end = '\0';
    return s;
}

bool LoaderLinux::is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool LoaderLinux::valid_mac(const char* mac) {
    if (std::strlen(mac) != 17) return false;
    for (int i = 0; i < 17; ++i)
        if ((i % 3) == 2 ? mac[i] != ':' : !is_hex(mac[i]))
            return false;
    return true;
}

std::string LoaderLinux::exe_dir() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    const std::string path(buf);
    const std::size_t pos = path.find_last_of('/');
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}
