#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace api {

class JsonApi {
public:
    using Json   = nlohmann::json;
    using Getter = std::function<Json()>;
    using Setter = std::function<Json(const Json& body)>;

    struct RouteInfo {
        std::string name;
        bool hasGetter{false};
        bool hasSetter{false};
    };

public:
    // --------------------------------------------------------
    // Register
    // --------------------------------------------------------
    void registerGetter(const std::string& name, Getter getter) {
        if (name.empty()) {
            throw std::runtime_error("JsonApi::registerGetter: empty route name");
        }
        getters_[name] = std::move(getter);
    }

    void registerSetter(const std::string& name, Setter setter) {
        if (name.empty()) {
            throw std::runtime_error("JsonApi::registerSetter: empty route name");
        }
        setters_[name] = std::move(setter);
    }

    // --------------------------------------------------------
    // Query
    // --------------------------------------------------------
    bool hasGetter(const std::string& name) const {
        return getters_.find(name) != getters_.end();
    }

    bool hasSetter(const std::string& name) const {
        return setters_.find(name) != setters_.end();
    }

    // --------------------------------------------------------
    // Execute
    // --------------------------------------------------------
    Json get(const std::string& name) const {
        auto it = getters_.find(name);
        if (it == getters_.end() || !it->second) {
            throw std::runtime_error("JsonApi GET route not found: " + name);
        }
        return it->second();
    }

    Json set(const std::string& name, const Json& body) const {
        auto it = setters_.find(name);
        if (it == setters_.end() || !it->second) {
            throw std::runtime_error("JsonApi POST route not found: " + name);
        }
        return it->second(body);
    }

    // --------------------------------------------------------
    // Debug / discovery
    // --------------------------------------------------------
    std::vector<RouteInfo> listRoutes() const {
        std::unordered_map<std::string, RouteInfo> merged;

        for (const auto& kv : getters_) {
            auto& r = merged[kv.first];
            r.name = kv.first;
            r.hasGetter = true;
        }

        for (const auto& kv : setters_) {
            auto& r = merged[kv.first];
            r.name = kv.first;
            r.hasSetter = true;
        }

        std::vector<RouteInfo> out;
        out.reserve(merged.size());

        for (auto& kv : merged) {
            out.push_back(std::move(kv.second));
        }

        return out;
    }

    Json listRoutesJson() const {
        Json j;
        j["routes"] = Json::array();

        for (const auto& r : listRoutes()) {
            j["routes"].push_back({
                {"name", r.name},
                {"get",  r.hasGetter},
                {"post", r.hasSetter}
            });
        }

        return j;
    }

private:
    std::unordered_map<std::string, Getter> getters_;
    std::unordered_map<std::string, Setter> setters_;
};

} // namespace api