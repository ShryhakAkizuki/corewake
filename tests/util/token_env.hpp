#pragma once

#include <cstdlib>
#include <string>

namespace corewake::test {

inline constexpr const char* kTokenEnvVar = "COREWAKE_TOKEN";

struct TokenEnv {
    bool        had_previous = false;
    std::string previous;

    explicit TokenEnv(const char* value) {
        if (const char* cur = std::getenv(kTokenEnvVar)) {
            previous     = cur;
            had_previous = true;
        }
        if (value == nullptr) {
            ::unsetenv(kTokenEnvVar);
        } else {
            ::setenv(kTokenEnvVar, value, /*overwrite=*/1);
        }
    }

    ~TokenEnv() {
        if (had_previous) {
            ::setenv(kTokenEnvVar, previous.c_str(), /*overwrite=*/1);
        } else {
            ::unsetenv(kTokenEnvVar);
        }
    }

    TokenEnv(const TokenEnv&) = delete;
    TokenEnv& operator=(const TokenEnv&) = delete;
};

} // namespace corewake::test
