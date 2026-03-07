#pragma once

#include <string>
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <utility>

#include <wiringPi.h>
#include <wiringSerial.h>

class SerialComm {
public:
    using milliseconds = std::chrono::milliseconds;

private:
    int serialPort_ = -1;
    std::string port_;
    int baudRate_ = 0;
    bool wiringInitialized_ = false;

public:
    // -------------------------
    // Constructors / Destructor
    // -------------------------
    SerialComm() = default;

    SerialComm(const std::string& port, int baudRate) {
        if (!open(port, baudRate)) {
            throw std::runtime_error("Failed to open serial port: " + port);
        }
    }

    ~SerialComm() {
        close();
    }

    // -------------------------
    // No copy
    // -------------------------
    SerialComm(const SerialComm&) = delete;
    SerialComm& operator=(const SerialComm&) = delete;

    // -------------------------
    // Move support
    // -------------------------
    SerialComm(SerialComm&& other) noexcept
        : serialPort_(other.serialPort_),
          port_(std::move(other.port_)),
          baudRate_(other.baudRate_),
          wiringInitialized_(other.wiringInitialized_) {
        other.serialPort_ = -1;
        other.baudRate_ = 0;
        other.wiringInitialized_ = false;
    }

    SerialComm& operator=(SerialComm&& other) noexcept {
        if (this != &other) {
            close();

            serialPort_ = other.serialPort_;
            port_ = std::move(other.port_);
            baudRate_ = other.baudRate_;
            wiringInitialized_ = other.wiringInitialized_;

            other.serialPort_ = -1;
            other.baudRate_ = 0;
            other.wiringInitialized_ = false;
        }
        return *this;
    }

public:
    // -------------------------
    // Open / Close
    // -------------------------
    bool open(const std::string& port, int baudRate) {
        close();

        port_ = port;
        baudRate_ = baudRate;

        if (!wiringInitialized_) {
            if (wiringPiSetup() == -1) {
                std::cerr << "[SerialComm::open] Error: wiringPiSetup failed\n";
                return false;
            }
            wiringInitialized_ = true;
        }

        serialPort_ = serialOpen(port_.c_str(), baudRate_);
        if (serialPort_ < 0) {
            std::cerr << "[SerialComm::open] Error: failed to open port " << port_ << "\n";
            serialPort_ = -1;
            return false;
        }

        return true;
    }

    void close() {
        if (serialPort_ >= 0) {
            serialClose(serialPort_);
            serialPort_ = -1;
        }
    }

    bool isOpen() const {
        return serialPort_ >= 0;
    }

    // -------------------------
    // Write
    // -------------------------
    void writeLine(const std::string& str) {
        if (!isOpen()) {
            std::cerr << "[SerialComm::writeLine] Error: serial port is not open\n";
            return;
        }

        if (str.empty()) {
            std::cerr << "[SerialComm::writeLine] Error: empty string\n";
            return;
        }

        serialPrintf(serialPort_, "%s", str.c_str());
    }

    // -------------------------
    // Read without timeout
    // -------------------------
    std::string readLine() {
        if (!isOpen()) {
            std::cerr << "[SerialComm::readLine] Error: serial port is not open\n";
            return "";
        }

        std::string result;

        while (serialDataAvail(serialPort_)) {
            char ch = static_cast<char>(serialGetchar(serialPort_));

            if (ch == '\n' || ch == '\0') {
                break;
            }

            if (ch != '\r') {
                result += ch;
            }
        }

        return result;
    }

    // -------------------------
    // Read with timeout
    // -------------------------
    std::string readLineWithTimeout(milliseconds timeout) {
        if (!isOpen()) {
            std::cerr << "[SerialComm::readLineWithTimeout] Error: serial port is not open\n";
            return "";
        }

        const auto start = std::chrono::steady_clock::now();
        std::string result;

        while (true) {
            while (serialDataAvail(serialPort_)) {
                char ch = static_cast<char>(serialGetchar(serialPort_));

                if (ch == '\n' || ch == '\0') {
                    return result;
                }

                if (ch != '\r') {
                    result += ch;
                }
            }

            if (std::chrono::steady_clock::now() - start >= timeout) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return result;
    }

    // -------------------------
    // Execute command
    // -------------------------
    std::string executeCommand(
        const std::string& str,
        milliseconds timeout = std::chrono::seconds(7)
    ) {
        if (!isOpen()) {
            std::cerr << "[SerialComm::executeCommand] Error: serial port is not open\n";
            return "";
        }

        if (str.empty()) {
            std::cerr << "[SerialComm::executeCommand] Error: empty command\n";
            return "";
        }

        writeLine(str);

        std::string response = readLineWithTimeout(timeout);

        if (response.empty()) {
            std::cerr << "[SerialComm::executeCommand] Warning: empty response or timeout\n";
        }

        return response;
    }

    // -------------------------
    // Getters
    // -------------------------
    int fd() const {
        return serialPort_;
    }

    const std::string& port() const {
        return port_;
    }

    int baudRate() const {
        return baudRate_;
    }
};
