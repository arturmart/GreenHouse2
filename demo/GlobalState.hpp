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
#include <vector>
#include <shared_mutex>   // shared_mutex
#include <chrono>

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
    // -------------------------
    // Type schema
    // -------------------------
    enum class ValueType : uint8_t { BOOL, INT, DOUBLE, STRING };

    static ValueType parseValueType(std::string s) {
        s = toLower(trim(std::move(s)));
        if (s == "bool")   return ValueType::BOOL;
        if (s == "int")    return ValueType::INT;
        if (s == "double") return ValueType::DOUBLE;
        if (s == "string") return ValueType::STRING;
        throw std::runtime_error("Unsupported type (bool/int/double/string): " + s);
    }

    // -------------------------
    // Storage
    // -------------------------
    struct GetterEntry {
        std::any  value;
        bool      valid{false};
        uint64_t  stampMs{0};   // когда обновили (ms)
    };

    struct ExecEntry {
        std::any  value;
        GH_MODE   mode{GH_MODE::MANUAL};
        bool      valid{true};   // для executor обычно true, но можно использовать
        uint64_t  stampMs{0};
    };

    using ExecMap    = std::unordered_map<int, ExecEntry>;
    using NameToId   = std::unordered_map<std::string, int>;
    using GetterMap  = std::unordered_map<std::string, GetterEntry>;

    using GetterSchema = std::unordered_map<std::string, ValueType>;
    using ExecSchemaByName = std::unordered_map<std::string, ValueType>; // Bake -> BOOL, Pump -> INT ...

    static GH_GlobalState& instance() {
        static GH_GlobalState inst;
        return inst;
    }

    GH_GlobalState(const GH_GlobalState&) = delete;
    GH_GlobalState& operator=(const GH_GlobalState&) = delete;

    // -------------------------
    // Time helper
    // -------------------------
    static uint64_t nowMs() {
        using namespace std::chrono;
        return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // -------------------------
    // ctx integration (передавать в стратегии)
    // -------------------------
    using Ctx = std::unordered_map<std::string, std::any>;

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

    // -------------------------
    // Read helpers (thread-safe)
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
        return it->second; // копия
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
    // -------------------------
    // Snapshots for API (thread-safe copies)
    // -------------------------
    GetterSchema snapshotGetterSchema() const {
        std::shared_lock lk(schema_mtx_);
        return getter_schema_;
    }

    ExecSchemaByName snapshotExecSchemaByName() const {
        std::shared_lock lk(schema_mtx_);
        return exec_schema_by_name_;
    }

    GetterMap snapshotGetters() const {
        std::shared_lock lk(getter_mtx_);
        return getter_status_;
    }

    struct ExecApiEntry {
        int id;
        std::string name;   // может быть пустым, если не найдено
        ExecEntry entry;
    };

    std::vector<ExecApiEntry> snapshotExecutors() const {
        std::shared_lock lk(exec_mtx_);

        // id -> name
        std::unordered_map<int, std::string> id2name;
        id2name.reserve(exec_name_to_id_.size());
        for (const auto& kv : exec_name_to_id_) {
            id2name[kv.second] = kv.first;
        }

        std::vector<ExecApiEntry> out;
        out.reserve(executor_status_.size());

        for (const auto& kv : executor_status_) {
            int id = kv.first;
            ExecApiEntry e;
            e.id = id;
            auto it = id2name.find(id);
            if (it != id2name.end()) e.name = it->second;
            e.entry = kv.second; // копия
            out.push_back(std::move(e));
        }
        return out;
    }

    // -------------------------
    // Write helpers (thread-safe)
    // -------------------------
    void setGetter(const std::string& key, std::any value) {
        // мягкие ошибки сенсора: setGetterInvalid отдельным методом
        validateGetterTypeOrThrow_(key, value);

        std::unique_lock lk(getter_mtx_);
        auto& e = getter_status_[key];
        e.value   = std::move(value);
        e.valid   = true;
        e.stampMs = nowMs();
    }

    void setGetterInvalid(const std::string& key) {
        std::unique_lock lk(getter_mtx_);
        auto& e = getter_status_[key];
        e.valid   = false;
        e.stampMs = nowMs();
    }

    void setExec(int id, std::any value, GH_MODE mode) {
        // executors обычно приходят от executor логики -> тоже можно проверять по schema
        // но schema по имени (Bake/Pump). Если есть id->name — можно расширить.
        std::unique_lock lk(exec_mtx_);
        auto& e = executor_status_[id];
        e.value   = std::move(value);
        e.mode    = mode;
        e.valid   = true;
        e.stampMs = nowMs();
    }

    void setExecInvalid(int id) {
        std::unique_lock lk(exec_mtx_);
        auto& e = executor_status_[id];
        e.valid   = false;
        e.stampMs = nowMs();
    }

    // -------------------------
    // Load from txt config (config errors = фатально)
    // -------------------------
    bool loadFromTxt(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        enum class Section { NONE, SCHEMA_GETTERS, SCHEMA_EXECUTORS, EXECUTORS, GETTERS };
        Section sec = Section::NONE;

        std::string line;
        while (std::getline(in, line)) {
            line = trim(stripComment(line));
            if (line.empty()) continue;

            if (isSection(line, "schema_getters"))   { sec = Section::SCHEMA_GETTERS;   continue; }
            if (isSection(line, "schema_executors")) { sec = Section::SCHEMA_EXECUTORS; continue; }
            if (isSection(line, "executors"))        { sec = Section::EXECUTORS;        continue; }
            if (isSection(line, "getters"))          { sec = Section::GETTERS;          continue; }

            switch (sec) {
                case Section::SCHEMA_GETTERS:   parseSchemaGetterLine(line);   break;
                case Section::SCHEMA_EXECUTORS: parseSchemaExecutorLine(line); break;
                case Section::EXECUTORS:        parseExecutorLine(line);       break;
                case Section::GETTERS:          parseGetterLine(line);         break;
                default: break;
            }
        }
        return true;
    }


private:
    GH_GlobalState() = default;

    // -------------------------
    // Internal storage + mutexes
    // -------------------------
    mutable std::shared_mutex exec_mtx_;
    mutable std::shared_mutex getter_mtx_;
    mutable std::shared_mutex schema_mtx_;

    ExecMap   executor_status_;   // id -> ExecEntry
    NameToId  exec_name_to_id_;   // name -> id
    GetterMap getter_status_;     // key -> GetterEntry

    GetterSchema     getter_schema_;
    ExecSchemaByName exec_schema_by_name_;

    // -------------------------
    // Parsing
    // -------------------------
    void parseSchemaGetterLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("schema_getters line must be: key=type : " + line);

        std::string key = trim(line.substr(0, eq));
        std::string t   = trim(line.substr(eq + 1));

        ValueType vt = parseValueType(t);
        setGetterSchema(key, vt);
    }

    void parseSchemaExecutorLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("schema_executors line must be: name=type : " + line);

        std::string name = trim(line.substr(0, eq));
        std::string t    = trim(line.substr(eq + 1));

        ValueType vt = parseValueType(t);
        setExecSchemaByName(name, vt);
    }

    void parseExecutorLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Executors line must contain '=': " + line);

        std::string name = trim(line.substr(0, eq));
        std::string rhs  = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 4)
            throw std::runtime_error("Executors line must be: Name=ID,type,value,mode : " + line);

        int id = parseInt(trim(parts[0]));
        std::string typeStr  = toLower(trim(parts[1]));
        std::string valueStr = trim(parts[2]);
        std::string modeStr  = toLower(trim(parts[3]));

        GH_MODE mode = parseMode(modeStr);
        ValueType vt = parseValueType(typeStr);

        // schema check: если schema задана — проверяем
        validateExecSchemaOrThrow_(name, vt);

        std::any val = parseAny(vt, valueStr);

        {
            std::unique_lock lk(exec_mtx_);
            exec_name_to_id_[name] = id;
            executor_status_[id] = ExecEntry{ std::move(val), mode, true, nowMs() };
        }
    }

    void parseGetterLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Getters line must contain '=': " + line);

        std::string key = trim(line.substr(0, eq));
        std::string rhs = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 2)
            throw std::runtime_error("Getters line must be: key=type,value : " + line);

        ValueType vt = parseValueType(parts[0]);
        std::string valueStr = trim(joinRest(parts, 1, parts.size() - 1));

        validateGetterSchemaOrThrow_(key, vt);

        std::any val = parseAny(vt, valueStr);

        {
            std::unique_lock lk(getter_mtx_);
            getter_status_[key] = GetterEntry{ std::move(val), true, nowMs() };
        }
    }

    static GH_MODE parseMode(const std::string& s) {
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

    // -------------------------
    // Defaults + schema
    // -------------------------
    void registerDefaultExecutors_() {
        const std::pair<const char*, int> defs[] = {
            {"Bake", 1}, {"Pump", 2},
            {"Falcon1", 3}, {"Falcon2", 4}, {"Falcon3", 5}, {"Falcon4", 6},
            {"IR1", 7}, {"IR2", 8},
            {"Cooler1", 9}, {"Cooler2", 10},
            {"Light1", 11},
        };

        std::unique_lock lk(exec_mtx_);
        for (auto& p : defs) {
            exec_name_to_id_[p.first] = p.second;
            executor_status_[p.second] = ExecEntry{ std::any(false), GH_MODE::MANUAL, true, nowMs() };
        }
    }

    void registerDefaultGetters_() {
        const char* keys[] = { "date","dateDaily","temp","temp2","inBake","outBake","tempOut" };

        std::unique_lock lk(getter_mtx_);
        for (auto* k : keys) {
            std::string ks(k);
            GetterEntry e;
            e.valid = false; // важно: до первого чтения сенсора считаем invalid
            e.stampMs = nowMs();

            if (ks == "date" || ks == "dateDaily") e.value = std::string{};
            else if (ks.rfind("temp", 0) == 0)     e.value = 0.0;
            else                                   e.value = false;

            getter_status_[ks] = std::move(e);
        }
    }

    void registerDefaultSchema_() {
        // getters schema
        std::unique_lock lk(schema_mtx_);
        getter_schema_["date"]      = ValueType::STRING;
        getter_schema_["dateDaily"] = ValueType::STRING;
        getter_schema_["temp"]      = ValueType::DOUBLE;
        getter_schema_["temp2"]     = ValueType::DOUBLE;
        getter_schema_["tempOut"]   = ValueType::DOUBLE;
        getter_schema_["inBake"]    = ValueType::BOOL;
        getter_schema_["outBake"]   = ValueType::BOOL;

        // executors schema (пример, ты сам задаёшь)
        exec_schema_by_name_["Bake"]    = ValueType::BOOL;
        exec_schema_by_name_["Pump"]    = ValueType::INT;    // или BOOL — как у тебя реально
        exec_schema_by_name_["Light1"]  = ValueType::DOUBLE; // например pwm
        // Остальные добавишь по мере надобности
    }

    // -------------------------
    // Schema validation
    // -------------------------
    void validateGetterSchemaOrThrow_(const std::string& key, ValueType got) const {
        std::shared_lock lk(schema_mtx_);
        auto it = getter_schema_.find(key);
        if (it == getter_schema_.end()) return; // если нет в схеме — разрешаем (можно сделать строго)
        if (it->second != got) {
            throw std::runtime_error("Getter schema type mismatch for key=" + key);
        }
    }

    void validateExecSchemaOrThrow_(const std::string& name, ValueType got) const {
        std::shared_lock lk(schema_mtx_);
        auto it = exec_schema_by_name_.find(name);
        if (it == exec_schema_by_name_.end()) return;
        if (it->second != got) {
            throw std::runtime_error("Executor schema type mismatch for name=" + name);
        }
    }

    void validateGetterTypeOrThrow_(const std::string& key, const std::any& v) const {
        // Это runtime check по any::type() (мягко). Схема строже через ValueType.
        std::shared_lock lk(schema_mtx_);
        auto it = getter_schema_.find(key);
        if (it == getter_schema_.end()) return;

        const std::type_info& ti = v.type();
        switch (it->second) {
            case ValueType::BOOL:   if (ti != typeid(bool))   throw std::runtime_error("Getter type mismatch: " + key); break;
            case ValueType::INT:    if (ti != typeid(int))    throw std::runtime_error("Getter type mismatch: " + key); break;
            case ValueType::DOUBLE: if (ti != typeid(double)) throw std::runtime_error("Getter type mismatch: " + key); break;
            case ValueType::STRING: if (ti != typeid(std::string)) throw std::runtime_error("Getter type mismatch: " + key); break;
        }
    }

    // -------------------------
    // Utils (как у тебя)
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
