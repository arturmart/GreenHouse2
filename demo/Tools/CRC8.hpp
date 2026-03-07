#pragma once

#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace proto {

class CRC8 {
public:
    static constexpr std::uint8_t POLY = 0x07;
    static constexpr std::uint8_t INIT = 0x00;

    static std::uint8_t calc(const std::uint8_t* data, std::size_t len) {
        std::uint8_t crc = INIT;

        for (std::size_t i = 0; i < len; ++i) {
            crc ^= data[i];

            for (int b = 0; b < 8; ++b) {
                if (crc & 0x80) {
                    crc = static_cast<std::uint8_t>((crc << 1) ^ POLY);
                } else {
                    crc = static_cast<std::uint8_t>(crc << 1);
                }
            }
        }

        return crc;
    }

    static std::uint8_t calc(const std::string& s) {
        return calc(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    static std::string calcHex(const std::string& s) {
        std::ostringstream oss;
        oss << std::uppercase
            << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(calc(s));
        return oss.str();
    }
};

} // namespace proto