#pragma once

#include <any>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../GlobalState.hpp"
#include "../Tools/DateTime.hpp"

namespace logic {

class ArgumentResolver {
public:
    explicit ArgumentResolver(GH_GlobalState& gs)
        : gs_(gs) {}

    // ------------------------------------------------------------
    // Resolve all args for one rule
    // Returns stringified values ready for ConditionContext
    // ------------------------------------------------------------
    std::vector<std::string> resolve(const std::vector<std::string>& args) const {
        std::vector<std::string> out;
        out.reserve(args.size());

        for (const auto& a : args) {
            out.push_back(resolveOne(a));
        }

        return out;
    }

    // ------------------------------------------------------------
    // Resolve one token
    // ------------------------------------------------------------
    std::string resolveOne(const std::string& token) const {
        // 1. special time helpers
        if (isTimeToken(token)) {
            return resolveTimeToken(token);
        }

        // 2. getter from GlobalState
        if (hasGetter(token)) {
            return getterToString(token);
        }

        // 3. literal
        return token;
    }

private:
    GH_GlobalState& gs_;

    // ------------------------------------------------------------
    // Getter existence check
    // ------------------------------------------------------------
    bool hasGetter(const std::string& key) const {
        try {
            (void)gs_.getGetterEntry(key);
            return true;
        } catch (...) {
            return false;
        }
    }

    // ------------------------------------------------------------
    // Convert getter any -> string
    // ------------------------------------------------------------
    std::string getterToString(const std::string& key) const {
        auto e = gs_.getGetterEntry(key);

        if (!e.valid) {
            throw std::runtime_error("Getter invalid: " + key);
        }

        const auto& a = e.value;
        if (!a.has_value()) {
            throw std::runtime_error("Getter has no value: " + key);
        }

        const auto& t = a.type();

        if (t == typeid(bool)) {
            return std::any_cast<bool>(a) ? "true" : "false";
        }

        if (t == typeid(int)) {
            return std::to_string(std::any_cast<int>(a));
        }

        if (t == typeid(double)) {
            std::ostringstream oss;
            oss << std::any_cast<double>(a);
            return oss.str();
        }

        if (t == typeid(std::string)) {
            return std::any_cast<std::string>(a);
        }

        if (t == typeid(tools::UnixMs)) {
            return std::to_string(std::any_cast<tools::UnixMs>(a).value);
        }

        throw std::runtime_error("Unsupported getter type for key: " + key);
    }

    // ------------------------------------------------------------
    // Time helper support
    // ------------------------------------------------------------
    static bool isTimeToken(const std::string& token) {
        return token == "time.unix_ms" ||
               token == "time.hour" ||
               token == "time.minute" ||
               token == "time.second" ||
               token == "time.daily_hhmmss";
    }

    std::string resolveTimeToken(const std::string& token) const {
        const long long now = tools::nowUnixMs();
        const auto dt = tools::fromUnixMs(now);

        if (token == "time.unix_ms") {
            return std::to_string(now);
        }

        if (token == "time.hour") {
            return std::to_string(dt.hour);
        }

        if (token == "time.minute") {
            return std::to_string(dt.minute);
        }

        if (token == "time.second") {
            return std::to_string(dt.second);
        }

        if (token == "time.daily_hhmmss") {
            // e.g. 09:30:05 -> 93005 or with zeros -> 093005 as string
            std::ostringstream oss;
            oss.fill('0');
            oss.width(2);
            oss << dt.hour;
            oss.width(2);
            oss << dt.minute;
            oss.width(2);
            oss << dt.second;
            return oss.str();
        }

        throw std::runtime_error("Unknown time token: " + token);
    }
};

} // namespace logic