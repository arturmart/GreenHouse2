#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <chrono>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class WeatherAPI {
public:
    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::milliseconds;

    WeatherAPI(const std::string& apiKey, double lat, double lon)
        : apiKey_(apiKey)
        , latitude_(lat)
        , longitude_(lon)
    {}

    // ------------------------------------------------------------
    // TTL settings
    // ------------------------------------------------------------
    void setWeatherCacheTtl(Ms ttl) {
        weatherCacheTtl_ = ttl;
    }

    void setForecastCacheTtl(Ms ttl) {
        forecastCacheTtl_ = ttl;
    }

    Ms weatherCacheTtl() const {
        return weatherCacheTtl_;
    }

    Ms forecastCacheTtl() const {
        return forecastCacheTtl_;
    }

    // ------------------------------------------------------------
    // Current weather (cached)
    // ------------------------------------------------------------
    void updateWeather() {
        if (hasFreshWeatherCache_()) {
            return;
        }
        forceUpdateWeather();
    }

    void forceUpdateWeather() {
        const std::string url =
            "https://api.openweathermap.org/data/2.5/weather?lat=" +
            std::to_string(latitude_) +
            "&lon=" + std::to_string(longitude_) +
            "&appid=" + apiKey_ +
            "&units=metric";

        const std::string response = makeRequest(url);

        if (response.empty()) {
            if (weather_.empty()) {
                weather_["error"] = "Empty response";
            }
            return;
        }

        try {
            json jsonData = json::parse(response);

            std::unordered_map<std::string, std::string> weatherData;

            if (jsonData.contains("cod")) {
                if (jsonData["cod"].is_number_integer()) {
                    const int cod = jsonData["cod"].get<int>();
                    if (cod != 200) {
                        weatherData["error"] = "OpenWeather error code: " + std::to_string(cod);
                        if (jsonData.contains("message")) {
                            weatherData["error_message"] = jsonData["message"].get<std::string>();
                        }
                        weather_ = std::move(weatherData);
                        weatherLastUpdate_ = Clock::now();
                        weatherEverUpdated_ = true;
                        return;
                    }
                } else if (jsonData["cod"].is_string()) {
                    const std::string cod = jsonData["cod"].get<std::string>();
                    if (cod != "200") {
                        weatherData["error"] = "OpenWeather error code: " + cod;
                        if (jsonData.contains("message")) {
                            weatherData["error_message"] = jsonData["message"].get<std::string>();
                        }
                        weather_ = std::move(weatherData);
                        weatherLastUpdate_ = Clock::now();
                        weatherEverUpdated_ = true;
                        return;
                    }
                }
            }

            weatherData["temp"] =
                std::to_string(jsonData.at("main").at("temp").get<double>());

            weatherData["humidity"] =
                std::to_string(jsonData.at("main").at("humidity").get<int>());

            weatherData["weather"] =
                jsonData.at("weather").at(0).at("description").get<std::string>();

            weatherData["pressure"] =
                std::to_string(jsonData.at("main").at("pressure").get<int>());

            weatherData["windspeed"] =
                std::to_string(jsonData.at("wind").at("speed").get<double>());

            weather_ = std::move(weatherData);
            weatherLastUpdate_ = Clock::now();
            weatherEverUpdated_ = true;

        } catch (const json::exception& e) {
            if (weather_.empty()) {
                weather_["error"] = std::string("JSON parse error: ") + e.what();
            }
        } catch (const std::exception& e) {
            if (weather_.empty()) {
                weather_["error"] = std::string("Weather update error: ") + e.what();
            }
        }
    }

    // ------------------------------------------------------------
    // Forecast (cached)
    // ------------------------------------------------------------
    void updateForecast() {
        if (hasFreshForecastCache_()) {
            return;
        }
        forceUpdateForecast();
    }

    void forceUpdateForecast() {
        const std::string url =
            "https://api.openweathermap.org/data/2.5/forecast?lat=" +
            std::to_string(latitude_) +
            "&lon=" + std::to_string(longitude_) +
            "&appid=" + apiKey_ +
            "&units=metric";

        const std::string response = makeRequest(url);

        if (response.empty()) {
            if (forecast_.empty()) {
                forecast_.push_back({{"error", "Empty response"}});
            }
            return;
        }

        try {
            json jsonData = json::parse(response);

            std::vector<std::unordered_map<std::string, std::string>> forecastData;

            if (jsonData.contains("cod")) {
                if (jsonData["cod"].is_string()) {
                    const std::string cod = jsonData["cod"].get<std::string>();
                    if (cod != "200") {
                        std::unordered_map<std::string, std::string> err;
                        err["error"] = "OpenWeather forecast error code: " + cod;
                        if (jsonData.contains("message")) {
                            err["error_message"] = jsonData["message"].get<std::string>();
                        }
                        forecastData.push_back(std::move(err));
                        forecast_ = std::move(forecastData);
                        forecastLastUpdate_ = Clock::now();
                        forecastEverUpdated_ = true;
                        return;
                    }
                } else if (jsonData["cod"].is_number_integer()) {
                    const int cod = jsonData["cod"].get<int>();
                    if (cod != 200) {
                        std::unordered_map<std::string, std::string> err;
                        err["error"] = "OpenWeather forecast error code: " + std::to_string(cod);
                        if (jsonData.contains("message")) {
                            err["error_message"] = jsonData["message"].get<std::string>();
                        }
                        forecastData.push_back(std::move(err));
                        forecast_ = std::move(forecastData);
                        forecastLastUpdate_ = Clock::now();
                        forecastEverUpdated_ = true;
                        return;
                    }
                }
            }

            if (jsonData.contains("list") && !jsonData["list"].empty()) {
                for (const auto& forecast : jsonData["list"]) {
                    std::unordered_map<std::string, std::string> entry;

                    entry["date"] =
                        forecast.at("dt_txt").get<std::string>();

                    entry["dateUnix"] =
                        std::to_string(forecast.at("dt").get<int>());

                    entry["temp"] =
                        std::to_string(forecast.at("main").at("temp").get<double>());

                    entry["humidity"] =
                        std::to_string(forecast.at("main").at("humidity").get<int>());

                    entry["weather"] =
                        forecast.at("weather").at(0).at("description").get<std::string>();

                    entry["pressure"] =
                        std::to_string(forecast.at("main").at("pressure").get<int>());

                    entry["windspeed"] =
                        std::to_string(forecast.at("wind").at("speed").get<double>());

                    forecastData.push_back(std::move(entry));
                }
            } else {
                forecastData.push_back({{"error", "Forecast data not found"}});
            }

            forecast_ = std::move(forecastData);
            forecastLastUpdate_ = Clock::now();
            forecastEverUpdated_ = true;

        } catch (const json::exception& e) {
            if (forecast_.empty()) {
                forecast_.push_back({{"error", std::string("JSON parse error: ") + e.what()}});
            }
        } catch (const std::exception& e) {
            if (forecast_.empty()) {
                forecast_.push_back({{"error", std::string("Forecast update error: ") + e.what()}});
            }
        }
    }

    // ------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------
    std::unordered_map<std::string, std::string> getWeather() const {
        return weather_;
    }

    std::vector<std::unordered_map<std::string, std::string>> getForecast() const {
        return forecast_;
    }

    // ------------------------------------------------------------
    // Status helpers
    // ------------------------------------------------------------
    bool isInited() const {
        try {
            const_cast<WeatherAPI*>(this)->updateWeather();

            auto it = weather_.find("error");
            return it == weather_.end();
        } catch (...) {
            return false;
        }
    }

    bool isInternetAvailable() const {
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize CURL\n";
            return false;
        }

        std::string readBuffer;

        curl_easy_setopt(curl, CURLOPT_URL, "https://google.com");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        const CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        return res == CURLE_OK;
    }

private:
    std::string apiKey_;
    double latitude_ = 0.0;
    double longitude_ = 0.0;

    std::unordered_map<std::string, std::string> weather_;
    std::vector<std::unordered_map<std::string, std::string>> forecast_;

    Clock::time_point weatherLastUpdate_{};
    Clock::time_point forecastLastUpdate_{};

    Ms weatherCacheTtl_{Ms(60000)};    // default: 60 sec
    Ms forecastCacheTtl_{Ms(600000)};  // default: 10 min

    bool weatherEverUpdated_ = false;
    bool forecastEverUpdated_ = false;

private:
    bool hasFreshWeatherCache_() const {
        if (!weatherEverUpdated_) return false;
        return (Clock::now() - weatherLastUpdate_) < weatherCacheTtl_;
    }

    bool hasFreshForecastCache_() const {
        if (!forecastEverUpdated_) return false;
        return (Clock::now() - forecastLastUpdate_) < forecastCacheTtl_;
    }

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* buffer) {
        const size_t totalSize = size * nmemb;
        buffer->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    std::string makeRequest(const std::string& url) const {
        CURL* curl = curl_easy_init();
        std::string readBuffer;

        if (!curl) {
            std::cerr << "curl_easy_init() failed\n";
            return "";
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        const CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: "
                      << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
        return readBuffer;
    }
};