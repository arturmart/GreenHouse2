#pragma once

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

enum class GH_MODE : uint8_t { MANUAL = 0, AUTO = 1 };

inline GH_MODE toMode(int v) {
    switch (v) {
        case 0: return GH_MODE::MANUAL;
        case 1: return GH_MODE::AUTO;
        default: throw std::runtime_error("Invalid GH_MODE value: " + std::to_string(v));
    }
}

inline std::string toString(GH_MODE m) {
    return (m == GH_MODE::MANUAL) ? "MANUAL" : "AUTO";
}

// -----------------------------
// Example config format (.txt)
// -----------------------------
// # comments allowed
// [executors]
// Bake=1,bool,true,auto
// Pump=2,int,0,manual
// Light1=10,double,0.75,auto
//
// [getters]
// date=string,2026-02-11 12:30:00
// temp=double,23.4
// inBake=bool,true
// tempOut=double,-2.1
//
// Types supported: bool, int, double, string
// mode supported: manual/auto or 0/1
//
// -----------------------------

class GH_GlobalState final {
public:
    using ExecValue  = std::any;
    using ExecTuple  = std::tuple<ExecValue, GH_MODE>;
    using ExecMap    = std::unordered_map<int, ExecTuple>;
    using NameToId   = std::unordered_map<std::string, int>;
    using GetterMap  = std::unordered_map<std::string, std::any>;

    static GH_GlobalState& instance() {
        static GH_GlobalState inst;
        return inst;
    }

    GH_GlobalState(const GH_GlobalState&) = delete;
    GH_GlobalState& operator=(const GH_GlobalState&) = delete;

    // -------------------------
    // Accessors
    // -------------------------
    ExecMap& executors() { return executor_status_; }
    const ExecMap& executors() const { return executor_status_; }

    GetterMap& getters() { return getter_status_; }
    const GetterMap& getters() const { return getter_status_; }

    NameToId& execNameToId() { return exec_name_to_id_; }
    const NameToId& execNameToId() const { return exec_name_to_id_; }

    // -------------------------
    // Typed getter helpers
    // -------------------------
    template <class T>
    T getGetterAs(const std::string& key) const {
        auto it = getter_status_.find(key);
        if (it == getter_status_.end())
            throw std::runtime_error("Getter key not found: " + key);
        return std::any_cast<T>(it->second);
    }

    template <class T>
    T getExecValueAs(int id) const {
        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found: " + std::to_string(id));
        return std::any_cast<T>(std::get<0>(it->second));
    }

    GH_MODE getExecMode(int id) const {
        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found: " + std::to_string(id));
        return std::get<1>(it->second);
    }

    void setExec(int id, std::any value, GH_MODE mode) {
        executor_status_[id] = std::make_tuple(std::move(value), mode);
    }

    void setGetter(const std::string& key, std::any value) {
        getter_status_[key] = std::move(value);
    }

    // -------------------------
    // Load from txt config
    // -------------------------
    // Returns true if loaded, false if file can't be opened.
    bool loadFromTxt(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        enum class Section { NONE, EXECUTORS, GETTERS };
        Section sec = Section::NONE;

        std::string line;
        while (std::getline(in, line)) {
            line = trim(stripComment(line));
            if (line.empty()) continue;

            if (isSection(line, "executors")) { sec = Section::EXECUTORS; continue; }
            if (isSection(line, "getters"))   { sec = Section::GETTERS;   continue; }

            switch (sec) {
                case Section::EXECUTORS:
                    parseExecutorLine(line);
                    break;
                case Section::GETTERS:
                    parseGetterLine(line);
                    break;
                default:
                    // ignore lines outside sections
                    break;
            }
        }

        return true;
    }

private:
    GH_GlobalState() {
        // Optional: pre-register names/IDs you listed (can be overwritten by config).
        // You can change IDs as you want.
        registerDefaultExecutors_();
        registerDefaultGetters_();
    }

    // -------------------------
    // Internal storage
    // -------------------------
    ExecMap   executor_status_;                // id -> (any value, mode)
    NameToId  exec_name_to_id_;                // "Bake" -> 1
    GetterMap getter_status_;                  // "temp" -> any

    // -------------------------
    // Parsing
    // -------------------------
    // executors line:
    // Name=ID,type,value,mode
    // Example: Bake=1,bool,true,auto
    void parseExecutorLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Executors line must contain '=': " + line);

        std::string name = trim(line.substr(0, eq));
        std::string rhs  = trim(line.substr(eq + 1));

        // Split rhs by ',': ID, type, value, mode
        auto parts = split(rhs, ',');
        if (parts.size() < 4)
            throw std::runtime_error("Executors line must be: Name=ID,type,value,mode : " + line);

        int id = parseInt(trim(parts[0]));
        std::string type = toLower(trim(parts[1]));
        std::string valueStr = trim(joinRest(parts, 2, parts.size() - 2)); // keep value with commas if any
        // But we used joinRest to allow string values with commas (rare). Then last token is mode.
        // Because of joinRest, we need to re-split smarter:
        // Simpler: assume no commas in value. If you need commas in string, we can add quotes support later.
        // For now: interpret parts[2] as value, parts[3] as mode.
        valueStr = trim(parts[2]);
        std::string modeStr = toLower(trim(parts[3]));

        GH_MODE mode = parseMode(modeStr);

        std::any val = parseAny(type, valueStr);

        exec_name_to_id_[name] = id;
        executor_status_[id] = std::make_tuple(std::move(val), mode);
    }

    // getters line:
    // key=type,value
    // Example: temp=double,23.4
    void parseGetterLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Getters line must contain '=': " + line);

        std::string key = trim(line.substr(0, eq));
        std::string rhs = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 2)
            throw std::runtime_error("Getters line must be: key=type,value : " + line);

        std::string type = toLower(trim(parts[0]));
        std::string valueStr = trim(joinRest(parts, 1, parts.size() - 1));
        // (same note: if you need commas in string, we can add quotes later)

        std::any val = parseAny(type, valueStr);
        getter_status_[key] = std::move(val);
    }

    static GH_MODE parseMode(const std::string& s) {
        if (s == "manual" || s == "0") return GH_MODE::MANUAL;
        if (s == "auto"   || s == "1") return GH_MODE::AUTO;
        throw std::runtime_error("Invalid mode (manual/auto or 0/1): " + s);
    }

    static std::any parseAny(const std::string& type, const std::string& value) {
        if (type == "bool")   return std::any(parseBool(value));
        if (type == "int")    return std::any(parseInt(value));
        if (type == "double") return std::any(parseDouble(value));
        if (type == "string") return std::any(value);
        throw std::runtime_error("Unsupported type (bool/int/double/string): " + type);
    }

    // -------------------------
    // Defaults (optional)
    // -------------------------
    void registerDefaultExecutors_() {
        // IDs are examples. Change to your real mapping.
        const std::pair<const char*, int> defs[] = {
            {"Bake", 1},
            {"Pump", 2},
            {"Falcon1", 3},
            {"Falcon2", 4},
            {"Falcon3", 5},
            {"Falcon4", 6},
            {"IR1", 7},
            {"IR2", 8},
            {"Cooler1", 9},
            {"Cooler2", 10},
            {"Light1", 11},
        };

        for (auto& p : defs) {
            exec_name_to_id_[p.first] = p.second;
            // default value=false, mode=MANUAL
            executor_status_[p.second] = std::make_tuple(std::any(false), GH_MODE::MANUAL);
        }
    }

    void registerDefaultGetters_() {
        const char* keys[] = {
            "date", "dateDaily", "temp", "temp2",
            "inBake", "outBake", "tempOut"
        };

        for (auto* k : keys) {
            // default empty string for date, 0.0 for temps, false for in/out flags.
            std::string ks(k);
            if (ks == "date" || ks == "dateDaily")
                getter_status_[ks] = std::any(std::string{});
            else if (ks.rfind("temp", 0) == 0)
                getter_status_[ks] = std::any(0.0);
            else
                getter_status_[ks] = std::any(false);
        }
    }

    // -------------------------
    // Utils
    // -------------------------
    static std::string stripComment(const std::string& s) {
        auto pos = s.find('#');
        return (pos == std::string::npos) ? s : s.substr(0, pos);
    }

    static bool isSection(const std::string& line, const std::string& nameLower) {
        std::string l = toLower(trim(line));
        return l == ("[" + nameLower + "]");
    }

    static std::string trim(std::string s) {
        auto notSpace = [](unsigned char c){ return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        return s;
    }

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) out.push_back(item);
        return out;
    }

    static std::string joinRest(const std::vector<std::string>& parts, size_t start, size_t count) {
        // joins parts[start .. start+count-1] with ','
        std::string out;
        for (size_t i = 0; i < count; ++i) {
            if (i) out += ",";
            out += parts[start + i];
        }
        return out;
    }

    static bool parseBool(std::string s) {
        s = toLower(trim(s));
        if (s == "1" || s == "true"  || s == "on"  || s == "yes") return true;
        if (s == "0" || s == "false" || s == "off" || s == "no")  return false;
        throw std::runtime_error("Invalid bool: " + s);
    }

    static int parseInt(std::string s) {
        s = trim(s);
        size_t idx = 0;
        int v = std::stoi(s, &idx, 10);
        if (idx != s.size()) throw std::runtime_error("Invalid int: " + s);
        return v;
    }

    static double parseDouble(std::string s) {
        s = trim(s);
        size_t idx = 0;
        double v = std::stod(s, &idx);
        if (idx != s.size()) throw std::runtime_error("Invalid double: " + s);
        return v;
    }
};
