#pragma once

#include <string>

namespace corewake::test {

struct ExpectedEntry {
    const char* alias;
    const char* mac;
};

inline constexpr ExpectedEntry kExpected[] = {
    { "PC",                             "00:11:22:33:44:55" },
    { "TV",                             "aa:bb:cc:dd:ee:ff" },
    { "ROBOT",                          "11:22:33:44:55:66" },
    { "GUEST_1",                        "DE:AD:BE:EF:00:01" },
    { "A234567890123456789012345678901", "0A:0B:0C:0D:0E:0F" }, // (ALIAS_MAX_LEN)
};

inline constexpr int kEntryCount = static_cast<int>(sizeof(kExpected) / sizeof(kExpected[0]));

inline const std::string kFixtureContent =
    "[aliases]\n"
    "PC=00:11:22:33:44:55\n"
    "TV = aa:bb:cc:dd:ee:ff\n"
    "ROBOT=11:22:33:44:55:66\n"
    "GUEST_1=DE:AD:BE:EF:00:01\n"
    "A234567890123456789012345678901=0A:0B:0C:0D:0E:0F\n";

} // namespace corewake::test
