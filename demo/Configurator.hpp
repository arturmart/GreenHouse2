#pragma once

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "GlobalState.hpp"

class GH_Configurator {
public:
    using ValueType  = GH_GlobalState::ValueType;
    using DcmBinding = GH_GlobalState::DcmBinding;

    struct GetterBinding {
        std::string strategy;
        std::vector<std::string> args;
    };

    using GetterBindingMap = std::unordered_map<std::string, GetterBinding>;

    bool loadFromTxt(const std::string& path, GH_GlobalState& gs) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        getter_bindings_.clear();

        enum class Section {
            NONE,
            SCHEMA_GETTERS,
            SCHEMA_EXECUTORS,
            EXECUTORS,
            GETTERS,
            DCM_MAP,
            GETTER_BINDINGS
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
            if (isSection(line, "getter_bindings"))  { sec = Section::GETTER_BINDINGS;  continue; }

            switch (sec) {
                case Section::SCHEMA_GETTERS:   parseSchemaGetterLine(line, gs);   break;
                case Section::SCHEMA_EXECUTORS: parseSchemaExecutorLine(line, gs); break;
                case Section::EXECUTORS:        parseExecutorLine(line, gs);       break;
                case Section::GETTERS:          parseGetterLine(line, gs);         break;
                case Section::DCM_MAP:          parseDcmMapLine(line, gs);         break;
                case Section::GETTER_BINDINGS:  parseGetterBindingLine(line);      break;
                default: break;
            }
        }

        return true;
    }

    const GetterBindingMap& getterBindings() const {
        return getter_bindings_;
    }

    GetterBinding getGetterBinding(const std::string& key) const {
        auto it = getter_bindings_.find(key);
        if (it == getter_bindings_.end()) {
            throw std::runtime_error("Getter binding not found: " + key);
        }
        return it->second;
    }

private:
    GetterBindingMap getter_bindings_;

private:
    void parseSchemaGetterLine(const std::string& line, GH_GlobalState& gs) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("schema_getters line must be: key=type : " + line);
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string t   = trim(line.substr(eq + 1));

        gs.setGetterSchema(key, parseValueType(t));
    }

    void parseSchemaExecutorLine(const std::string& line, GH_GlobalState& gs) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("schema_executors line must be: name=type : " + line);
        }

        const std::string name = trim(line.substr(0, eq));
        const std::string t    = trim(line.substr(eq + 1));

        gs.setExecSchemaByName(name, parseValueType(t));
    }

    void parseExecutorLine(const std::string& line, GH_GlobalState& gs) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("executors line must contain '=': " + line);
        }

        const std::string name = trim(line.substr(0, eq));
        const std::string rhs  = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 4) {
            throw std::runtime_error("executors line must be: Name=ID,type,value,mode : " + line);
        }

        const int id = parseInt(trim(parts[0]));
        const ValueType vt = parseValueType(trim(parts[1]));
        const std::string valueStr = trim(parts[2]);
        const GH_MODE mode = parseMode(trim(parts[3]));

        gs.registerExecNameToId(name, id);
        gs.setExec(id, parseAny(vt, valueStr), mode);
    }

    void parseGetterLine(const std::string& line, GH_GlobalState& gs) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("getters line must contain '=': " + line);
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string rhs = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 2) {
            throw std::runtime_error("getters line must be: key=type,value : " + line);
        }

        const ValueType vt = parseValueType(parts[0]);
        const std::string valueStr = trim(joinRest(parts, 1, parts.size() - 1));

        gs.setGetter(key, parseAny(vt, valueStr));
    }

    void parseDcmMapLine(const std::string& line, GH_GlobalState& gs) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("dcm_map line must contain '=': " + line);
        }

        const std::string name = trim(line.substr(0, eq));
        const std::string rhs  = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.size() < 3) {
            throw std::runtime_error("dcm_map line must be: Name=tableId,index,type : " + line);
        }

        const int tableId = parseInt(trim(parts[0]));
        const int index   = parseInt(trim(parts[1]));
        const ValueType vt = parseValueType(trim(parts[2]));

        if (tableId != 68 && tableId != 80) {
            throw std::runtime_error("dcm_map tableId must be 68 or 80: " + line);
        }

        gs.setDcmBindingByName(name, DcmBinding{tableId, index, vt});
    }

    void parseGetterBindingLine(const std::string& line) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("getter_bindings line must contain '=': " + line);
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string rhs = trim(line.substr(eq + 1));

        auto parts = split(rhs, ',');
        if (parts.empty()) {
            throw std::runtime_error("getter_bindings line must be: key=strategy,arg1,arg2,... : " + line);
        }

        GetterBinding gb;
        gb.strategy = trim(parts[0]);

        for (size_t i = 1; i < parts.size(); ++i) {
            gb.args.push_back(trim(parts[i]));
        }

        getter_bindings_[key] = std::move(gb);
    }

private:
    static ValueType parseValueType(std::string s) {
        s = toLower(trim(std::move(s)));
        if (s == "bool")   return ValueType::BOOL;
        if (s == "int")    return ValueType::INT;
        if (s == "double") return ValueType::DOUBLE;
        if (s == "string") return ValueType::STRING;
        throw std::runtime_error("Unsupported type (bool/int/double/string): " + s);
    }

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