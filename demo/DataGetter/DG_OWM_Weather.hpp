#pragma once

#include <string>
#include <stdexcept>
#include <chrono>

#include "ADataGetter_Strategy.hpp"
#include "../Tools/WeatherAPI.hpp"

namespace dg {

// OpenWeather numeric getter strategy.
// Примеры fieldKeyInWeather:
//   "temp"
//   "humidity"
//   "pressure"
//   "windspeed"
class DG_OWM_Weather final : public ADataGetterStrategy<double> {
public:
    DG_OWM_Weather(std::string apiKey,
                   double latitude,
                   double longitude,
                   std::string fieldKeyInWeather,
                   long long cacheMs = 60000)
        : api_(apiKey, latitude, longitude)
        , fieldKey_(std::move(fieldKeyInWeather))
    {
        if (cacheMs < 0) {
            throw std::runtime_error("DG_OWM_Weather: cacheMs must be >= 0");
        }

        api_.setWeatherCacheTtl(std::chrono::milliseconds(cacheMs));
    }

    std::string name() const override {
        return "DG_OWM_Weather(" + fieldKey_ + ")";
    }

    void init(const Ctx& /*ctx*/) override {
        // optional checks:
        // if (!api_.isInternetAvailable())
        //     throw std::runtime_error("DG_OWM_Weather: internet unavailable");
        //
        // if (!api_.isInited())
        //     throw std::runtime_error("DG_OWM_Weather: API init failed");
    }

    double getData() override {
        api_.updateWeather();

        const auto weather = api_.getWeather();

        auto err = weather.find("error");
        if (err != weather.end()) {
            throw std::runtime_error("DG_OWM_Weather: " + err->second);
        }

        auto it = weather.find(fieldKey_);
        if (it == weather.end()) {
            throw std::runtime_error("DG_OWM_Weather: field not found: " + fieldKey_);
        }

        try {
            const double value = std::stod(it->second);
            this->sensorValue_ = value;
            return value;
        } catch (const std::exception&) {
            throw std::runtime_error(
                "DG_OWM_Weather: field '" + fieldKey_ +
                "' is not numeric, value='" + it->second + "'"
            );
        }
    }

    bool isInited() const {
        return api_.isInited();
    }

private:
    WeatherAPI api_;
    std::string fieldKey_;
};

} // namespace dg