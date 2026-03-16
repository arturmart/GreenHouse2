#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace logic {

// ------------------------------------------------------------
// When action should fire
// ------------------------------------------------------------
enum class TriggerMode : uint8_t {
    ON_ENTER = 0,
    ON_EXIT  = 1,
    WHILE_TRUE = 2,
    WHILE_FALSE = 3
};

inline std::string toString(TriggerMode t) {
    switch (t) {
        case TriggerMode::ON_ENTER:   return "on_enter";
        case TriggerMode::ON_EXIT:    return "on_exit";
        case TriggerMode::WHILE_TRUE: return "while_true";
        case TriggerMode::WHILE_FALSE:return "while_false";
    }
    return "unknown";
}

// ------------------------------------------------------------
// What kind of value action writes
// ------------------------------------------------------------
enum class ActionValueType : uint8_t {
    BOOL = 0,
    INT  = 1,
    DOUBLE = 2,
    STRING = 3
};

inline std::string toString(ActionValueType t) {
    switch (t) {
        case ActionValueType::BOOL:   return "bool";
        case ActionValueType::INT:    return "int";
        case ActionValueType::DOUBLE: return "double";
        case ActionValueType::STRING: return "string";
    }
    return "unknown";
}

// ------------------------------------------------------------
// Typed action model
// LogicEngine writes desired executor state through GlobalState
// ------------------------------------------------------------
struct ActionModel {
    std::string target;              // executor name, e.g. "Cooler1"
    ActionValueType valueType{ActionValueType::BOOL};
    std::string value;               // literal as string, parsed later
    TriggerMode trigger{TriggerMode::ON_ENTER};

    bool enabled{true};
};

} // namespace logic