#pragma once

#include <string>
#include <stdexcept>
#include <cstdint>

#include <sys/statvfs.h>

struct SysDiskInfo
{
    uint64_t totalKB = 0;
    uint64_t freeKB = 0;
    uint64_t availableKB = 0;
};

class SysDisk
{
public:
    static SysDiskInfo read(const std::string& path = "/")
    {
        struct statvfs st;

        if (statvfs(path.c_str(), &st) != 0) {
            throw std::runtime_error("SysDisk: failed to statvfs(" + path + ")");
        }

        const uint64_t blockSize = static_cast<uint64_t>(st.f_frsize);

        SysDiskInfo info;
        info.totalKB     = (static_cast<uint64_t>(st.f_blocks)  * blockSize) / 1024;
        info.freeKB      = (static_cast<uint64_t>(st.f_bfree)   * blockSize) / 1024;
        info.availableKB = (static_cast<uint64_t>(st.f_bavail)  * blockSize) / 1024;

        return info;
    }
};