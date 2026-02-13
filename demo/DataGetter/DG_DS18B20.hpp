#pragma once
#include <fstream>
#include <string>
#include <stdexcept>
#include <sys/stat.h>

#include "ADataGetter_Strategy.hpp"   // твой базовый ADataGetterStrategy<T>

namespace dg {

// Стратегия DataGetter для DS18B20 (1-Wire, Linux sysfs).
// Выдаёт float (°C) и пишет в привязанный Field<float> через getDataRef().
class DG_DS18B20 final : public ADataGetterStrategy<float> {
public:
    explicit DG_DS18B20(std::string sensorID,
                        std::string basePath = "/sys/bus/w1/devices/")
        : sensorID_(std::move(sensorID))
        , basePath_(std::move(basePath))
    {}

    std::string name() const override {
        return "DG_DS18B20(" + sensorID_ + ")";
    }

    // Можно дернуть из init(), если хочешь валидировать сразу
    void init(const Ctx& /*ctx*/) override {
        // ничего обязательного
        // но можно сразу проверить наличие файла:
        // if (!isInited()) throw std::runtime_error("DS18B20 not inited: " + sensorID_);
    }

    // Основное чтение датчика
    float getData() override {
        const std::string path = sensorPath() + "/w1_slave";
        std::ifstream file(path);
        if (!file.is_open()) {
            // В твоём старом коде было -1. Тут лучше исключение (или верни NAN).
            throw std::runtime_error("DG_DS18B20: failed to open " + path);
        }

        std::string line1, line2;
        std::getline(file, line1);
        std::getline(file, line2);

        // CRC
        if (line1.find("YES") == std::string::npos) {
            throw std::runtime_error("DG_DS18B20: CRC check failed for " + sensorID_);
        }

        // t=xxxxx
        const auto pos = line2.find("t=");
        if (pos == std::string::npos) {
            throw std::runtime_error("DG_DS18B20: no temperature token for " + sensorID_);
        }

        const std::string tempStr = line2.substr(pos + 2);
        const float temperature = std::stof(tempStr) / 1000.0f;

        // сохраним в sensorValue_ (полезно для отладки)
        this->sensorValue_ = temperature;
        return temperature;
    }

    bool isInited() const {
        struct stat buffer;
        const std::string path = sensorPath() + "/w1_slave";
        return (stat(path.c_str(), &buffer) == 0);
    }

private:
    std::string sensorPath() const {
        return basePath_ + sensorID_;
    }

private:
    std::string sensorID_;
    std::string basePath_;
};

} // namespace dg
