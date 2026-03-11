#pragma once

#include <fstream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <cstdint>

class SysCPU
{
public:

    struct CpuTimes
    {
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t softirq = 0;
        uint64_t steal = 0;
    };

    static CpuTimes readTimes()
    {
        std::ifstream file("/proc/stat");

        if (!file.is_open())
            throw std::runtime_error("SysCPU: cannot open /proc/stat");

        std::string line;
        std::getline(file, line);

        std::stringstream ss(line);

        std::string cpu;
        CpuTimes t;

        ss >> cpu;
        ss >> t.user
           >> t.nice
           >> t.system
           >> t.idle
           >> t.iowait
           >> t.irq
           >> t.softirq
           >> t.steal;

        return t;
    }

    static double calcUsage(const CpuTimes& prev, const CpuTimes& curr)
    {
        uint64_t prevIdle = prev.idle + prev.iowait;
        uint64_t currIdle = curr.idle + curr.iowait;

        uint64_t prevNonIdle =
            prev.user + prev.nice + prev.system +
            prev.irq + prev.softirq + prev.steal;

        uint64_t currNonIdle =
            curr.user + curr.nice + curr.system +
            curr.irq + curr.softirq + curr.steal;

        uint64_t prevTotal = prevIdle + prevNonIdle;
        uint64_t currTotal = currIdle + currNonIdle;

        uint64_t totald = currTotal - prevTotal;
        uint64_t idled = currIdle - prevIdle;

        if (totald == 0)
            return 0.0;

        return (double)(totald - idled) / (double)totald * 100.0;
    }
};