#pragma once
#include <fstream>
#include <string>
#include <stdexcept>

struct SysMemInfo
{
    uint64_t memTotalKB = 0;
    uint64_t memFreeKB = 0;
    uint64_t memAvailableKB = 0;
};

class SysMem
{
public:
   static uint64_t readProcessRamKB()
   {
      std::ifstream file("/proc/self/status");
      if (!file.is_open()) {
         throw std::runtime_error("SysMem: failed to open /proc/self/status");
      }

      std::string line;
      while (std::getline(file, line))
      {
         if (line.rfind("VmRSS:", 0) == 0)
         {
               // убираем "VmRSS:"
               std::string rest = line.substr(6);

               // trim left
               auto first = rest.find_first_not_of(" \t");
               if (first == std::string::npos) {
                  throw std::runtime_error("SysMem: VmRSS line has no value");
               }
               rest = rest.substr(first);

               // читаем число
               std::stringstream ss(rest);
               uint64_t value = 0;
               ss >> value;

               if (ss.fail()) {
                  throw std::runtime_error("SysMem: failed to parse VmRSS value");
               }

               return value; // already in kB
         }
      }

      throw std::runtime_error("SysMem: VmRSS not found");
   }
    static SysMemInfo read()
    {
        SysMemInfo info;

        std::ifstream file("/proc/meminfo");
        if (!file.is_open())
            throw std::runtime_error("SysMem: failed to open /proc/meminfo");

        std::string key;
        uint64_t value;
        std::string unit;

        while (file >> key >> value >> unit)
        {
            if (key == "MemTotal:")
                info.memTotalKB = value;

            else if (key == "MemFree:")
                info.memFreeKB = value;

            else if (key == "MemAvailable:")
                info.memAvailableKB = value;
        }

        return info;
    }
};

