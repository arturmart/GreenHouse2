#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "SerialComm.hpp"
#include "CRC8.hpp"

#ifndef DCM_DEBUG_CRC
#define DCM_DEBUG_CRC 0
#endif

#if DCM_DEBUG_CRC
    #define DCM_CRC_LOG(x) do { std::cout << x << std::endl; } while(0)
#else
    #define DCM_CRC_LOG(x) do {} while(0)
#endif

class DeviceControlModule {
public:
    static constexpr int TABLE_DIGITAL = 68;
    static constexpr int TABLE_PWM     = 80;

    static constexpr std::size_t DIGITAL_COUNT = 8;
    static constexpr std::size_t PWM_COUNT     = 3;
    static constexpr std::size_t MAX_PACKETS_PER_FRAME = 8;

    static constexpr std::uint16_t ERROR_SYNTAX                = 1u << 0;
    static constexpr std::uint16_t ERROR_1L_NO_DATA            = 1u << 1;
    static constexpr std::uint16_t ERROR_1L_TOO_MANY_DATA      = 1u << 2;
    static constexpr std::uint16_t ERROR_INVALID_CRC           = 1u << 3;
    static constexpr std::uint16_t ERROR_NULL_CRC              = 1u << 4;
    static constexpr std::uint16_t ERROR_2L_NO_DATA_PACKETS    = 1u << 5;
    static constexpr std::uint16_t ERROR_2L_TOO_MANY_PACKETS   = 1u << 6;
    static constexpr std::uint16_t ERROR_3L_WRONG_DATA_PACKETS = 1u << 7;
    static constexpr std::uint16_t PACKETS_COUNT_MASK          = 0x0F00;
    static constexpr std::uint16_t GET_KEYWORD                 = 1u << 12;

    struct Packet {
        int tableId = 0;
        int index   = 0;
        int value   = 0;

        std::string toDataString() const {
            return std::to_string(tableId) + "," +
                   std::to_string(index)   + "," +
                   std::to_string(value);
        }
    };

    struct ParsedReply {
        bool okTransport = false;
        bool okCrc       = false;
        bool hasMask     = false;
        bool successMask = false;

        std::string raw;
        std::string payload;
        std::string crcHex;

        std::uint16_t mask = 0;

        int packetsCount() const {
            return static_cast<int>((mask & PACKETS_COUNT_MASK) >> 8);
        }

        bool isKeywordAck() const {
            return (mask & GET_KEYWORD) != 0;
        }
    };

    struct QueueItem {
        std::string dataOnly;
        int retries = 5;
    };

public:
    explicit DeviceControlModule(const std::string& port = "/dev/ttyS3", int baud = 57600)
        : serial_(port, baud) {
    }

    bool open(const std::string& port, int baud) {
        return serial_.open(port, baud);
    }

    void close() {
        serial_.close();
    }

    bool isOpen() const {
        return serial_.isOpen();
    }

    bool isInited() {
        ParsedReply rep = sendImmediate("inited");

        if (!rep.okTransport || !rep.okCrc) {
            return false;
        }

        if (rep.payload == "ok") {
            return true;
        }

        if (rep.hasMask && rep.successMask) {
            return true;
        }

        return false;
    }

    void setRetryCount(int count) {
        retryCount_ = std::max(1, count);
    }

    void setRetryDelay(std::chrono::milliseconds delay) {
        retryDelay_ = delay;
    }

    void setCommandTimeout(std::chrono::milliseconds timeout) {
        commandTimeout_ = timeout;
    }

    void enqueueKeyword(const std::string& keyword) {
        if (keyword.empty()) {
            throw std::runtime_error("DeviceControlModule::enqueueKeyword(): empty keyword");
        }

        queue_.push(QueueItem{keyword, retryCount_});
    }

    void enqueuePacket(const Packet& packet) {
        validatePacket(packet);
        queue_.push(QueueItem{packet.toDataString(), retryCount_});
    }

    void enqueuePackets(const std::vector<Packet>& packets) {
        if (packets.empty()) {
            throw std::runtime_error("DeviceControlModule::enqueuePackets(): empty packet list");
        }

        if (packets.size() > MAX_PACKETS_PER_FRAME) {
            throw std::runtime_error("DeviceControlModule::enqueuePackets(): too many packets (>8)");
        }

        std::ostringstream oss;

        for (std::size_t i = 0; i < packets.size(); ++i) {
            validatePacket(packets[i]);

            if (i > 0) {
                oss << ';';
            }

            oss << packets[i].toDataString();
        }

        queue_.push(QueueItem{oss.str(), retryCount_});
    }

    void enqueueTurnOnAllDigital() {
        enqueuePackets(makeAllDigitalPackets(1));
    }

    void enqueueTurnOffAllDigital() {
        enqueuePackets(makeAllDigitalPackets(0));
    }

    void enqueueSetAllHardwareState(
        const std::array<bool, DIGITAL_COUNT>& digital,
        const std::array<std::uint8_t, PWM_COUNT>& pwm
    ) {
        enqueueKeyword("setAll");

        std::vector<Packet> packets;
        packets.reserve(DIGITAL_COUNT + PWM_COUNT);

        for (int i = 0; i < static_cast<int>(DIGITAL_COUNT); ++i) {
            packets.push_back(Packet{TABLE_DIGITAL, i, digital[i] ? 1 : 0});
        }

        for (int i = 0; i < static_cast<int>(PWM_COUNT); ++i) {
            packets.push_back(Packet{TABLE_PWM, i, static_cast<int>(pwm[i])});
        }

        std::size_t pos = 0;
        while (pos < packets.size()) {
            const std::size_t n = std::min<std::size_t>(
                MAX_PACKETS_PER_FRAME,
                packets.size() - pos
            );

            std::vector<Packet> chunk(
                packets.begin() + static_cast<std::ptrdiff_t>(pos),
                packets.begin() + static_cast<std::ptrdiff_t>(pos + n)
            );

            enqueuePackets(chunk);
            pos += n;
        }

        enqueueKeyword("end");
    }

    bool tick() {
        if (queue_.empty()) {
            return false;
        }

        QueueItem item = queue_.front();

        for (int attempt = 0; attempt < item.retries; ++attempt) {
            ParsedReply rep = sendImmediate(item.dataOnly);

            if (rep.okTransport && rep.okCrc) {
                bool success = false;

                if (rep.hasMask) {
                    success = rep.successMask;
                } else {
                    success = true;
                }

                if (success) {
                    std::cout << "[DCM] OK: " << item.dataOnly
                              << " -> " << rep.payload << "\n";

                    queue_.pop();
                    return true;
                }

                std::cout << "[DCM] bad feedback mask, retry "
                          << (attempt + 1) << "/" << item.retries
                          << " for: " << item.dataOnly << "\n";
            } else {
                std::cout << "[DCM] no/invalid reply, retry "
                          << (attempt + 1) << "/" << item.retries
                          << " for: " << item.dataOnly << "\n";
            }

            std::this_thread::sleep_for(retryDelay_);
        }

        std::cerr << "[DCM] FAILED after retries: " << item.dataOnly << "\n";
        queue_.pop();
        return false;
    }

    void update() {
        while (!queue_.empty()) {
            tick();
        }
    }

    bool empty() const {
        return queue_.empty();
    }

    std::size_t queued() const {
        return queue_.size();
    }

    ParsedReply sendImmediate(const std::string& dataOnly) {
      const std::string frame = buildFrame(dataOnly);

      DCM_CRC_LOG("[DCM CRC] sendImmediate | TX='" << frame << "'");

      const std::string rawReply = serial_.executeCommand(frame, commandTimeout_);

      DCM_CRC_LOG("[DCM CRC] sendImmediate | RX='" << rawReply << "'");

      ParsedReply rep;
      rep.raw = rawReply;
      rep.okTransport = !trimCopy(rawReply).empty();

      if (!rep.okTransport) {
         DCM_CRC_LOG("[DCM CRC] sendImmediate | empty transport");
         return rep;
      }

      if (!parseReply(rawReply, rep)) {
         DCM_CRC_LOG("[DCM CRC] sendImmediate | parseReply FAILED");
         return rep;
      }

      rep.okCrc = true;

      std::uint16_t mask = 0;
      if (tryParseFeedbackMask(rep.payload, mask)) {
         rep.hasMask = true;
         rep.mask = mask;
         rep.successMask = isSuccessMask(mask);
         printMask(mask);
      }

      return rep;
   }

    static std::string buildFrame(const std::string& dataOnly) {
      const std::string crc = proto::CRC8::calcHex(dataOnly);
      const std::string frame = dataOnly + "/" + crc;

      DCM_CRC_LOG("[DCM CRC] buildFrame | data='" << dataOnly
                  << "' crc='" << crc
                  << "' frame='" << frame << "'");

      return frame;
   }

private:
    SerialComm serial_;
    std::queue<QueueItem> queue_;

    int retryCount_ = 5;
    std::chrono::milliseconds retryDelay_{100};
    std::chrono::milliseconds commandTimeout_{7000};

private:
    static void validatePacket(const Packet& packet) {
        if (packet.tableId == TABLE_DIGITAL) {
            if (packet.index < 0 || packet.index >= static_cast<int>(DIGITAL_COUNT)) {
                throw std::runtime_error("DCM digital index out of range");
            }

            if (!(packet.value == 0 || packet.value == 1)) {
                throw std::runtime_error("DCM digital value must be 0 or 1");
            }

            return;
        }

        if (packet.tableId == TABLE_PWM) {
            if (packet.index < 0 || packet.index >= static_cast<int>(PWM_COUNT)) {
                throw std::runtime_error("DCM pwm index out of range");
            }

            if (packet.value < 0 || packet.value > 255) {
                throw std::runtime_error("DCM pwm value must be 0..255");
            }

            return;
        }

        throw std::runtime_error("DCM unknown tableId (must be 68 or 80)");
    }

    static bool parseReply(const std::string& raw, ParsedReply& out) {
      const std::string s = trimCopy(raw);
      const std::size_t slashPos = s.find('/');

      if (slashPos == std::string::npos) {
         out.payload = s;
         out.crcHex.clear();

         DCM_CRC_LOG("[DCM CRC] parseReply | raw='" << raw
                        << "' trimmed='" << s
                        << "' no slash");

         std::uint16_t mask = 0;
         if (tryParseFeedbackMask(s, mask)) {
               out.okCrc = true;
               out.hasMask = true;
               out.mask = mask;
               out.successMask = isSuccessMask(mask);
               return true;
         }

         return false;
      }

      if (s.find('/', slashPos + 1) != std::string::npos) {
         DCM_CRC_LOG("[DCM CRC] parseReply | raw='" << raw
                        << "' invalid: more than one slash");
         return false;
      }

      const std::string payload = s.substr(0, slashPos);
      const std::string crcHex  = s.substr(slashPos + 1);

      if (payload.empty()) {
         DCM_CRC_LOG("[DCM CRC] parseReply | raw='" << raw
                        << "' invalid: empty payload");
         return false;
      }

      if (!isHexByte(crcHex)) {
         DCM_CRC_LOG("[DCM CRC] parseReply | raw='" << raw
                        << "' invalid: crcHex='" << crcHex << "'");
         return false;
      }

      const std::string calc = proto::CRC8::calcHex(payload);

      DCM_CRC_LOG("[DCM CRC] parseReply | payload='" << payload
                  << "' recv_crc='" << crcHex
                  << "' calc_crc='" << calc << "'");

      if (toUpperCopy(calc) != toUpperCopy(crcHex)) {
         DCM_CRC_LOG("[DCM CRC] parseReply | CRC MISMATCH");
         return false;
      }

      out.payload = payload;
      out.crcHex  = toUpperCopy(crcHex);

      DCM_CRC_LOG("[DCM CRC] parseReply | CRC OK");

      return true;
   }

    static bool tryParseFeedbackMask(const std::string& payload, std::uint16_t& outMask) {
        std::string s = trimCopy(payload);

        if (!s.empty() && (s.front() == 'K' || s.front() == 'k')) {
            s.erase(s.begin());
        }

        if (s.empty()) {
            return false;
        }

        for (char c : s) {
            if (c != '0' && c != '1') {
                return false;
            }
        }

        if (s.size() < 13) {
            return false;
        }

        if (s.size() > 13) {
            s = s.substr(s.size() - 13);
        }

        std::uint16_t mask = 0;
        for (char c : s) {
            mask <<= 1;
            if (c == '1') {
                mask |= 1u;
            }
        }

        outMask = mask;
        return true;
    }

    static bool isSuccessMask(std::uint16_t mask) {
        const std::uint16_t fatal =
            ERROR_SYNTAX |
            ERROR_1L_NO_DATA |
            ERROR_1L_TOO_MANY_DATA |
            ERROR_INVALID_CRC |
            ERROR_NULL_CRC |
            ERROR_2L_TOO_MANY_PACKETS |
            ERROR_3L_WRONG_DATA_PACKETS;

        return (mask & fatal) == 0;
    }

    static std::string trimCopy(std::string s) {
        while (!s.empty() &&
               (s.back() == '\n' || s.back() == '\r' ||
                std::isspace(static_cast<unsigned char>(s.back())))) {
            s.pop_back();
        }

        std::size_t pos = 0;
        while (pos < s.size() &&
               std::isspace(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }

        return s.substr(pos);
    }

    static std::string toUpperCopy(std::string s) {
        for (char& c : s) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return s;
    }

    static bool isHexByte(const std::string& s) {
        if (s.size() != 2) {
            return false;
        }

        return std::isxdigit(static_cast<unsigned char>(s[0])) &&
               std::isxdigit(static_cast<unsigned char>(s[1]));
    }

    static void printMask(std::uint16_t mask) {
        if (mask & ERROR_SYNTAX)                 std::cout << "[ER1-SY] ";
        if (mask & ERROR_1L_NO_DATA)             std::cout << "[ER2-1N] ";
        if (mask & ERROR_1L_TOO_MANY_DATA)       std::cout << "[ER3-1M] ";
        if (mask & ERROR_INVALID_CRC)            std::cout << "[ER4-IC] ";
        if (mask & ERROR_NULL_CRC)               std::cout << "[ER5-NC] ";
        if (mask & ERROR_2L_NO_DATA_PACKETS)     std::cout << "[W6-2N] ";
        if (mask & ERROR_2L_TOO_MANY_PACKETS)    std::cout << "[ER7-2M] ";
        if (mask & ERROR_3L_WRONG_DATA_PACKETS)  std::cout << "[ER8-3W] ";
        if (mask & GET_KEYWORD)                  std::cout << "[KW] ";

        std::cout << "[PC-" << ((mask & PACKETS_COUNT_MASK) >> 8) << "]\n";
    }

    static std::vector<Packet> makeAllDigitalPackets(int value) {
        std::vector<Packet> packets;
        packets.reserve(DIGITAL_COUNT);

        for (int i = 0; i < static_cast<int>(DIGITAL_COUNT); ++i) {
            packets.push_back(Packet{TABLE_DIGITAL, i, value});
        }

        return packets;
    }
};