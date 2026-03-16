#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "RuleTree.hpp"
#include "ActionModel.hpp"

namespace logic {

// ------------------------------------------------------------
// JSON escape
// ------------------------------------------------------------
inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);

    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }

    return out;
}

// ------------------------------------------------------------
// Enum -> string helpers
// ------------------------------------------------------------
inline std::string toStringJson(TriggerMode t) {
    return toString(t);
}

inline std::string toStringJson(ActionValueType t) {
    return toString(t);
}

// ------------------------------------------------------------
// Action -> JSON
// ------------------------------------------------------------
inline std::string actionToJson(const ActionModel& a) {
    std::string out = "{";
    out += "\"target\":\"" + jsonEscape(a.target) + "\"";
    out += ",\"valueType\":\"" + jsonEscape(toStringJson(a.valueType)) + "\"";
    out += ",\"value\":\"" + jsonEscape(a.value) + "\"";
    out += ",\"trigger\":\"" + jsonEscape(toStringJson(a.trigger)) + "\"";
    out += ",\"enabled\":" + std::string(a.enabled ? "true" : "false");
    out += "}";
    return out;
}

// ------------------------------------------------------------
// vector<string> -> JSON
// ------------------------------------------------------------
inline std::string stringVectorToJson(const std::vector<std::string>& arr) {
    std::string out = "[";
    bool first = true;

    for (const auto& s : arr) {
        if (!first) out += ",";
        first = false;
        out += "\"" + jsonEscape(s) + "\"";
    }

    out += "]";
    return out;
}

// ------------------------------------------------------------
// actions -> JSON
// ------------------------------------------------------------
inline std::string actionsToJson(const std::vector<ActionModel>& actions) {
    std::string out = "[";
    bool first = true;

    for (const auto& a : actions) {
        if (!first) out += ",";
        first = false;
        out += actionToJson(a);
    }

    out += "]";
    return out;
}

// ------------------------------------------------------------
// runtime -> JSON
// ------------------------------------------------------------
inline std::string runtimeToJson(const RuleRuntimeState& rt) {
    std::string out = "{";
    out += "\"localResult\":" + std::string(rt.localResult ? "true" : "false");
    out += ",\"effectiveResult\":" + std::string(rt.effectiveResult ? "true" : "false");
    out += ",\"prevEffectiveResult\":" + std::string(rt.prevEffectiveResult ? "true" : "false");
    out += ",\"lastEvalMs\":" + std::to_string(rt.lastEvalMs);
    out += ",\"lastFireMs\":" + std::to_string(rt.lastFireMs);
    out += ",\"lastError\":\"" + jsonEscape(rt.lastError) + "\"";
    out += ",\"resolvedArgs\":" + stringVectorToJson(rt.resolvedArgs);
    out += "}";
    return out;
}

// ------------------------------------------------------------
// one node -> full JSON
// ------------------------------------------------------------
inline std::string nodeToJson(const RuleNode& node) {
    std::string out = "{";

    out += "\"title\":\"" + jsonEscape(node.title()) + "\"";
    out += ",\"condition\":\"" + jsonEscape(node.condition()) + "\"";
    out += ",\"args\":" + stringVectorToJson(node.args());
    out += ",\"runtime\":" + runtimeToJson(node.runtime());
    out += ",\"actions\":" + actionsToJson(node.actions());

    out += ",\"children\":[";
    bool first = true;
    for (const auto& ch : node.children()) {
        if (!first) out += ",";
        first = false;
        out += nodeToJson(*ch);
    }
    out += "]";

    out += "}";
    return out;
}

// ------------------------------------------------------------
// tree -> full JSON
// ------------------------------------------------------------
inline std::string treeToJson(const RuleTree& tree) {
    auto* root = tree.root();
    if (!root) {
        return "{\"root\":null}";
    }

    return std::string("{\"root\":") + nodeToJson(*root) + "}";
}

// ------------------------------------------------------------
// runtime-only node JSON
// ------------------------------------------------------------
inline std::string nodeRuntimeToJson(const RuleNode& node) {
    std::string out = "{";

    out += "\"title\":\"" + jsonEscape(node.title()) + "\"";
    out += ",\"runtime\":" + runtimeToJson(node.runtime());

    out += ",\"children\":[";
    bool first = true;
    for (const auto& ch : node.children()) {
        if (!first) out += ",";
        first = false;
        out += nodeRuntimeToJson(*ch);
    }
    out += "]";

    out += "}";

    return out;
}

// ------------------------------------------------------------
// tree runtime JSON
// ------------------------------------------------------------
inline std::string treeRuntimeToJson(const RuleTree& tree) {
    auto* root = tree.root();
    if (!root) {
        return "{\"root\":null}";
    }

    return std::string("{\"root\":") + nodeRuntimeToJson(*root) + "}";
}

// ------------------------------------------------------------
// structure-only node JSON
// ------------------------------------------------------------
inline std::string nodeStructureToJson(const RuleNode& node) {
    std::string out = "{";

    out += "\"title\":\"" + jsonEscape(node.title()) + "\"";
    out += ",\"condition\":\"" + jsonEscape(node.condition()) + "\"";
    out += ",\"args\":" + stringVectorToJson(node.args());
    out += ",\"actions\":" + actionsToJson(node.actions());

    out += ",\"children\":[";
    bool first = true;
    for (const auto& ch : node.children()) {
        if (!first) out += ",";
        first = false;
        out += nodeStructureToJson(*ch);
    }
    out += "]";

    out += "}";

    return out;
}

// ------------------------------------------------------------
// tree structure JSON
// ------------------------------------------------------------
inline std::string treeStructureToJson(const RuleTree& tree) {
    auto* root = tree.root();
    if (!root) {
        return "{\"root\":null}";
    }

    return std::string("{\"root\":") + nodeStructureToJson(*root) + "}";
}

} // namespace logic