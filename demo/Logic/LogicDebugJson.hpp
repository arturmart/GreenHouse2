#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "RuleTree.hpp"

namespace logic {

using json = nlohmann::json;

// ------------------------------------------------------------
// Runtime -> json
// ------------------------------------------------------------
inline json runtimeToJson(const RuleRuntimeState& rt)
{
    json j;

    j["localResult"] = rt.localResult;
    j["effectiveResult"] = rt.effectiveResult;
    j["prevEffectiveResult"] = rt.prevEffectiveResult;

    j["lastEvalMs"] = rt.lastEvalMs;
    j["lastFireMs"] = rt.lastFireMs;

    j["lastError"] = rt.lastError;

    j["resolvedArgs"] = json::array();
    for (const auto& a : rt.resolvedArgs)
        j["resolvedArgs"].push_back(a);

    return j;
}

// ------------------------------------------------------------
// Action -> json
// ------------------------------------------------------------
inline json actionToJson(const ActionModel& a)
{
    json j;

    j["target"] = a.target;
    j["valueType"] = toString(a.valueType);
    j["value"] = a.value;
    j["trigger"] = toString(a.trigger);
    j["enabled"] = a.enabled;

    return j;
}

// ------------------------------------------------------------
// Node structure (no runtime)
// ------------------------------------------------------------
inline json nodeStructureToJson(const RuleNode* node)
{
    json j;

    j["title"] = node->title();
    j["condition"] = node->condition();

    j["args"] = json::array();
    for (const auto& a : node->args())
        j["args"].push_back(a);

    j["actions"] = json::array();
    for (const auto& a : node->actions())
        j["actions"].push_back(actionToJson(a));

    j["children"] = json::array();
    for (const auto& ch : node->children())
        j["children"].push_back(nodeStructureToJson(ch.get()));

    return j;
}

// ------------------------------------------------------------
// Node runtime (no structure)
// ------------------------------------------------------------
inline json nodeRuntimeToJson(const RuleNode* node)
{
    json j;

    j["title"] = node->title();
    j["runtime"] = runtimeToJson(node->runtime());

    j["children"] = json::array();
    for (const auto& ch : node->children())
        j["children"].push_back(nodeRuntimeToJson(ch.get()));

    return j;
}

// ------------------------------------------------------------
// Full node (structure + runtime)
// ------------------------------------------------------------
inline json nodeFullToJson(const RuleNode* node)
{
    json j;

    j["title"] = node->title();
    j["condition"] = node->condition();

    j["args"] = json::array();
    for (const auto& a : node->args())
        j["args"].push_back(a);

    j["runtime"] = runtimeToJson(node->runtime());

    j["actions"] = json::array();
    for (const auto& a : node->actions())
        j["actions"].push_back(actionToJson(a));

    j["children"] = json::array();
    for (const auto& ch : node->children())
        j["children"].push_back(nodeFullToJson(ch.get()));

    return j;
}

// ------------------------------------------------------------
// Tree -> json
// ------------------------------------------------------------
inline json treeStructureToJson(const RuleTree& tree)
{
    json j;

    if (!tree.root())
        return j;

    j["root"] = nodeStructureToJson(tree.root());

    return j;
}

inline json treeRuntimeToJson(const RuleTree& tree)
{
    json j;

    if (!tree.root())
        return j;

    j["root"] = nodeRuntimeToJson(tree.root());

    return j;
}

inline json treeToJson(const RuleTree& tree)
{
    json j;

    if (!tree.root())
        return j;

    j["root"] = nodeFullToJson(tree.root());

    return j;
}

} // namespace logic