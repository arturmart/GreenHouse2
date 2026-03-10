#pragma once

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <mutex>

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

class GH_GlobalState final {
public:
    enum class ValueType : uint8_t { BOOL, INT, DOUBLE, STRING };

    struct GetterEntry {
        std::any value;
        bool valid{false};
        uint64_t stampMs{0};
    };

    struct ExecEntry {
        std::any value;
        GH_MODE mode{GH_MODE::MANUAL};
        bool valid{true};
        uint64_t stampMs{0};
    };

    struct DcmBinding {
        int tableId{0};          // 68 or 80
        int index{0};            // channel index
        ValueType type{ValueType::BOOL};
    };

    using ExecMap          = std::unordered_map<int, ExecEntry>;
    using NameToId         = std::unordered_map<std::string, int>;
    using GetterMap        = std::unordered_map<std::string, GetterEntry>;
    using GetterSchema     = std::unordered_map<std::string, ValueType>;
    using ExecSchemaByName = std::unordered_map<std::string, ValueType>;
    using DcmBindingMap    = std::unordered_map<std::string, DcmBinding>;
    using Ctx             = std::unordered_map<std::string, std::any>;

    static GH_GlobalState& instance() {
        static GH_GlobalState inst;
        return inst;
    }

    GH_GlobalState(const GH_GlobalState&) = delete;
    GH_GlobalState& operator=(const GH_GlobalState&) = delete;

    static uint64_t nowMs() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
        );
    }

    static ValueType parseValueType(std::string s) {
        s = toLower(trim(std::move(s)));
        if (s == "bool")   return ValueType::BOOL;
        if (s == "int")    return ValueType::INT;
        if (s == "double") return ValueType::DOUBLE;
        if (s == "string") return ValueType::STRING;
        throw std::runtime_error("Unsupported type (bool/int/double/string): " + s);
    }

    static std::string valueTypeToString(ValueType vt) {
        switch (vt) {
            case ValueType::BOOL:   return "bool";
            case ValueType::INT:    return "int";
            case ValueType::DOUBLE: return "double";
            case ValueType::STRING: return "string";
        }
        return "unknown";
    }

    // -------------------------
    // Schema setup
    // -------------------------
    void setGetterSchema(const std::string& key, ValueType t) {
        std::unique_lock lk(schema_mtx_);
        getter_schema_[key] = t;
    }

    void setExecSchemaByName(const std::string& name, ValueType t) {
        std::unique_lock lk(schema_mtx_);
        exec_schema_by_name_[name] = t;
    }

    void setDcmBindingByName(const std::string& name, DcmBinding b) {
        std::unique_lock lk(schema_mtx_);
        dcm_bindings_[name] = std::move(b);
    }

    // -------------------------
    // Read helpers
    // -------------------------
    template<class T>
    T getGetterAs(const std::string& key) const {
        std::shared_lock lk(getter_mtx_);
        auto it = getter_status_.find(key);
        if (it == getter_status_.end())
            throw std::runtime_error("Getter key not found: " + key);
        if (!it->second.valid)
            throw std::runtime_error("Getter key invalid: " + key);
        return std::any_cast<T>(it->second.value);
    }

    GetterEntry getGetterEntry(const std::string& key) const {
        std::shared_lock lk(getter_mtx_);
        auto it = getter_status_.find(key);
        if (it == getter_status_.end())
            throw std::runtime_error("Getter key not found: " + key);
        return it->second;
    }

    ExecEntry getExecEntry(int id) const {
        std::shared_lock lk(exec_mtx_);
        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found: " + std::to_string(id));
        return it->second;
    }

    template<class T>
    T getExecValueAs(int id) const {
        std::shared_lock lk(exec_mtx_);
        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found: " + std::to_string(id));
        if (!it->second.valid)
            throw std::runtime_error("Executor id invalid: " + std::to_string(id));
        return std::any_cast<T>(it->second.value);
    }

    GH_MODE getExecMode(int id) const {
        std::shared_lock lk(exec_mtx_);
        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found: " + std::to_string(id));
        return it->second.mode;
    }

    int execIdByName(const std::string& name) const {
        std::shared_lock lk(exec_mtx_);
        auto it = exec_name_to_id_.find(name);
        if (it == exec_name_to_id_.end())
            throw std::runtime_error("Executor name not found: " + name);
        return it->second;
    }

    DcmBinding getDcmBindingByName(const std::string& name) const {
        std::shared_lock lk(schema_mtx_);
        auto it = dcm_bindings_.find(name);
        if (it == dcm_bindings_.end())
            throw std::runtime_error("DCM binding not found for executor: " + name);
        return it->second;
    }

    // -------------------------
    // Snapshots
    // -------------------------
    GetterSchema snapshotGetterSchema() const {
        std::shared_lock lk(schema_mtx_);
        return getter_schema_;
    }

    ExecSchemaByName snapshotExecSchemaByName() const {
        std::shared_lock lk(schema_mtx_);
        return exec_schema_by_name_;
    }

    DcmBindingMap snapshotDcmBindings() const {
        std::shared_lock lk(schema_mtx_);
        return dcm_bindings_;
    }

    GetterMap snapshotGetters() const {
        std::shared_lock lk(getter_mtx_);
        return getter_status_;
    }

    struct ExecApiEntry {
        int id;
        std::string name;
        ExecEntry entry;
    };

    std::vector<ExecApiEntry> snapshotExecutors() const {
        std::shared_lock lk(exec_mtx_);

        std::unordered_map<int, std::string> id2name;
        id2name.reserve(exec_name_to_id_.size());
        for (const auto& kv : exec_name_to_id_) {
            id2name[kv.second] = kv.first;
        }

        std::vector<ExecApiEntry> out;
        out.reserve(executor_status_.size());

        for (const auto& kv : executor_status_) {
            ExecApiEntry e;
            e.id = kv.first;
            auto it = id2name.find(kv.first);
            if (it != id2name.end()) e.name = it->second;
            e.entry = kv.second;
            out.push_back(std::move(e));
        }
        return out;
    }

    // -------------------------
    // Write helpers
    // -------------------------
    void setGetter(const std::string& key, std::any value) {
        validateGetterTypeOrThrow_(key, value);

        std::unique_lock lk(getter_mtx_);
        auto& e = getter_status_[key];
        e.value = std::move(value);
        e.valid = true;
        e.stampMs = nowMs();
    }

    void setGetterInvalid(const std::string& key) {
        std::unique_lock lk(getter_mtx_);
        auto& e = getter_status_[key];
        e.valid = false;
        e.stampMs = nowMs();
    }

    void setExec(int id, std::any value, GH_MODE mode) {
        std::unique_lock lk(exec_mtx_);
        auto& e = executor_status_[id];
        e.value = std::move(value);
        e.mode = mode;
        e.valid = true;
        e.stampMs = nowMs();
    }

    void setExecInvalid(int id) {
        std::unique_lock lk(exec_mtx_);
        auto& e = executor_status_[id];
        e.valid = false;
        e.stampMs = nowMs();
    }

    void setExecMode(int id, GH_MODE mode) {
        std::unique_lock lk(exec_mtx_);
        auto& e = executor_status_[id];
        e.mode = mode;
        e.stampMs = nowMs();
    }

    // -------------------------
    // Config load
    // -------------------------
    bool loadFromTxt(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        enum class Section {
            NONE,
            SCHEMA_GETTERS,
            SCHEMA_EXECUTORS,
            EXECUTORS,
            GETTERS,
            DCM_MAP
        };

        Section sec = Section::NONE;
        std::string line;

        while (std::getline(in, line)) {
            line = trim(stripComment(line));
            if (line.empty()) continue;

            if (isSection(line, "schema_getters"))   { sec = Section::SCHEMA_GETTERS;   continue; }
            if (isSection(line, "schema_executors")) { sec = Section::SCHEMA_EXECUTORS; continue; }
            if (isSection(line, "executors"))        { sec = Section::EXECUTORS;        continue; }
            if (isSection(line, "getters"))          { sec = Section::GETTERS;          continue; }
            if (isSection(line, "dcm_map"))          { sec = Section::DCM_MAP;          continue; }

            switch (sec) {
                case Section::SCHEMA_GETTERS:   parseSchemaGetterLine(line);   break;
                case Section::SCHEMA_EXECUTORS: parseSchemaExecutorLine(line); break;
                case Section::EXECUTORS:        parseExecutorLine(line);       break;
                case Section::GETTERS:          parseGetterLine(line);         break;
                case Section::DCM_MAP:          parseDcmMapLine(line);         break;
                default: break;
            }
        }

        return true;
    }

private:
    GH_GlobalState() = default;

    mutable std::shared_mutex exec_mtx_;
    mutable std::shared_mutex getter_mtx_;
    mutable std::shared_mutex schema_mtx_;

    ExecMap executor_status_;
    NameToId exec_name_to_id_;
    GetterMap getter_status_;

    GetterSchema getter_schema_;
    ExecSchemaByName exec_schema_by_name_;
    DcmBindingMap dcm_bindings_;

    // -------------------------
    // Parsing
    // -------------------------
    void parseSchemaGetterLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("schema_getters line must be: key=type : " + line);

        std::string key = trim(line.substr(0, eq));
        std::string t   = trim(line.substr(eq + 1));

        setGetterSchema(key, parseValueType(t));
    }

    void parseSchemaExecutorLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("schema_executors line must be: name=type : " + line);

        std::string name = trim(line.substr(0, eq));
        std::string t    = trim(line.substr(eq + 1));

        setExecSchemaByName(name, parseValueType(t));
    }

    void parseExecutorLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("executors line must contain '=': " + line);

        std::string name = trim(line.substr(0, eq));
        std::string rhs  = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 4)
            throw std::runtime_error("executors line must be: Name=ID,type,value,mode : " + line);

        int id = parseInt(trim(parts[0]));
        ValueType vt = parseValueType(trim(parts[1]));
        std::string valueStr = trim(parts[2]);
        GH_MODE mode = parseMode(trim(parts[3]));

        validateExecSchemaOrThrow_(name, vt);
        std::any val = parseAny(vt, valueStr);

        std::unique_lock lk(exec_mtx_);
        exec_name_to_id_[name] = id;
        executor_status_[id] = ExecEntry{std::move(val), mode, true, nowMs()};
    }

    void parseGetterLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("getters line must contain '=': " + line);

        std::string key = trim(line.substr(0, eq));
        std::string rhs = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 2)
            throw std::runtime_error("getters line must be: key=type,value : " + line);

        ValueType vt = parseValueType(parts[0]);
        std::string valueStr = trim(joinRest(parts, 1, parts.size() - 1));

        validateGetterSchemaOrThrow_(key, vt);
        std::any val = parseAny(vt, valueStr);

        std::unique_lock lk(getter_mtx_);
        getter_status_[key] = GetterEntry{std::move(val), true, nowMs()};
    }

    void parseDcmMapLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("dcm_map line must contain '=': " + line);

        std::string name = trim(line.substr(0, eq));
        std::string rhs  = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 3)
            throw std::runtime_error("dcm_map line must be: Name=tableId,index,type : " + line);

        int tableId = parseInt(trim(parts[0]));
        int index   = parseInt(trim(parts[1]));
        ValueType vt = parseValueType(trim(parts[2]));

        if (tableId != 68 && tableId != 80)
            throw std::runtime_error("dcm_map tableId must be 68 or 80: " + line);

        setDcmBindingByName(name, DcmBinding{tableId, index, vt});
    }

    // -------------------------
    // Validation
    // -------------------------
    void validateGetterSchemaOrThrow_(const std::string& key, ValueType got) const {
        std::shared_lock lk(schema_mtx_);
        auto it = getter_schema_.find(key);
        if (it == getter_schema_.end()) return;
        if (it->second != got)
            throw std::runtime_error("Getter schema type mismatch for key=" + key);
    }

    void validateExecSchemaOrThrow_(const std::string& name, ValueType got) const {
        std::shared_lock lk(schema_mtx_);
        auto it = exec_schema_by_name_.find(name);
        if (it == exec_schema_by_name_.end()) return;
        if (it->second != got)
            throw std::runtime_error("Executor schema type mismatch for name=" + name);
    }

    void validateGetterTypeOrThrow_(const std::string& key, const std::any& v) const {
        std::shared_lock lk(schema_mtx_);
        auto it = getter_schema_.find(key);
        if (it == getter_schema_.end()) return;

        const std::type_info& ti = v.type();
        switch (it->second) {
            case ValueType::BOOL:
                if (ti != typeid(bool)) throw std::runtime_error("Getter type mismatch: " + key);
                break;
            case ValueType::INT:
                if (ti != typeid(int)) throw std::runtime_error("Getter type mismatch: " + key);
                break;
            case ValueType::DOUBLE:
                if (ti != typeid(double)) throw std::runtime_error("Getter type mismatch: " + key);
                break;
            case ValueType::STRING:
                if (ti != typeid(std::string)) throw std::runtime_error("Getter type mismatch: " + key);
                break;
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
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
        std::string out;
        for (size_t i = 0; i < count; ++i) {
            if (i) out += ",";
            out += parts[start + i];
        }
        return out;
    }

    static bool parseBool(std::string s) {
        s = toLower(trim(s));
        if (s == "1" || s == "true" || s == "on" || s == "yes") return true;
        if (s == "0" || s == "false" || s == "off" || s == "no") return false;
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

    static GH_MODE parseMode(const std::string& sRaw) {
        std::string s = toLower(trim(sRaw));
        if (s == "manual" || s == "0") return GH_MODE::MANUAL;
        if (s == "auto"   || s == "1") return GH_MODE::AUTO;
        throw std::runtime_error("Invalid mode (manual/auto or 0/1): " + s);
    }

    static std::any parseAny(ValueType t, const std::string& value) {
        switch (t) {
            case ValueType::BOOL:   return std::any(parseBool(value));
            case ValueType::INT:    return std::any(parseInt(value));
            case ValueType::DOUBLE: return std::any(parseDouble(value));
            case ValueType::STRING: return std::any(value);
        }
        throw std::runtime_error("parseAny: unreachable");
    }
};