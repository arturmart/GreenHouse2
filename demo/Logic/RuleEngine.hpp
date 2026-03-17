#pragma once

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../GlobalState.hpp"
#include "RuleTree.hpp"
#include "ConditionContext.hpp"
#include "ArgumentResolver.hpp"
#include "ActionModel.hpp"

namespace logic {

class RuleEngine {
public:
    RuleEngine(GH_GlobalState& gs, RuleTree& tree)
        : gs_(gs),
          tree_(tree),
          conditions_(),
          resolver_(gs) {}

    RuleTree& tree() { return tree_; }
    const RuleTree& tree() const { return tree_; }

    // ------------------------------------------------------------
    // Force rules to re-apply their AUTO desired state
    // Useful when switching executor back to AUTO mode
    // ------------------------------------------------------------
    void requestRefresh() {
        forceRefresh_ = true;
    }

    void tick() {
        auto* root = tree_.root();
        if (!root) return;

        evaluateNode(root, true);

        forceRefresh_ = false;
    }

private:
    GH_GlobalState& gs_;
    RuleTree& tree_;
    ConditionContext conditions_;
    ArgumentResolver resolver_;
    bool forceRefresh_{false};

private:
    static uint64_t nowMs() {
        return GH_GlobalState::nowMs();
    }

    void evaluateNode(RuleNode* node, bool parentEffective) {
        if (!node) return;

        auto& rt = node->runtime();
        rt.prevEffectiveResult = rt.effectiveResult;
        rt.lastError.clear();
        rt.resolvedArgs.clear();
        rt.lastEvalMs = nowMs();

        try {
            const auto resolved = resolver_.resolve(node->args());
            rt.resolvedArgs = resolved;

            rt.localResult = conditions_.check(node->condition(), resolved);
            rt.effectiveResult = parentEffective && rt.localResult;

            processActions(*node);

        } catch (const std::exception& ex) {
            rt.localResult = false;
            rt.effectiveResult = false;
            rt.lastError = ex.what();
        } catch (...) {
            rt.localResult = false;
            rt.effectiveResult = false;
            rt.lastError = "unknown logic error";
        }

        for (auto& ch : node->children()) {
            evaluateNode(ch.get(), node->runtime().effectiveResult);
        }
    }

    void processActions(RuleNode& node) {
        auto& rt = node.runtime();

        const bool entered = (!rt.prevEffectiveResult && rt.effectiveResult);
        const bool exited  = (rt.prevEffectiveResult && !rt.effectiveResult);

        for (const auto& action : node.actions()) {
            if (!action.enabled) continue;

            bool shouldFire = false;

            switch (action.trigger) {
                case TriggerMode::ON_ENTER:
                    shouldFire = entered || (forceRefresh_ && rt.effectiveResult);
                    break;

                case TriggerMode::ON_EXIT:
                    shouldFire = exited || (forceRefresh_ && !rt.effectiveResult);
                    break;

                case TriggerMode::WHILE_TRUE:
                    shouldFire = rt.effectiveResult;
                    break;

                case TriggerMode::WHILE_FALSE:
                    shouldFire = !rt.effectiveResult;
                    break;
            }

            if (!shouldFire) continue;

            applyAction(action);
            rt.lastFireMs = nowMs();
        }
    }

    void applyAction(const ActionModel& action) {
        const int id = gs_.execIdByName(action.target);

        // --------------------------------------------------------
        // IMPORTANT:
        // If executor is in MANUAL mode, logic must not overwrite it.
        // --------------------------------------------------------
        {
            const auto actual = gs_.getExecActualEntry(id);
            const auto desired = gs_.getExecDesiredEntry(id);

            if (actual.mode == GH_MODE::MANUAL || desired.mode == GH_MODE::MANUAL) {
                return;
            }
        }

        switch (action.valueType) {
            case ActionValueType::BOOL: {
                const bool v = parseBool(action.value);
                gs_.setExecDesired(id, v, GH_MODE::AUTO, "logic", true);
                return;
            }

            case ActionValueType::INT: {
                const int v = parseInt(action.value);
                gs_.setExecDesired(id, v, GH_MODE::AUTO, "logic", true);
                return;
            }

            case ActionValueType::DOUBLE: {
                const double v = parseDouble(action.value);
                gs_.setExecDesired(id, v, GH_MODE::AUTO, "logic", true);
                return;
            }

            case ActionValueType::STRING: {
                gs_.setExecDesired(id, action.value, GH_MODE::AUTO, "logic", true);
                return;
            }
        }

        throw std::runtime_error("Unsupported ActionValueType");
    }

    static bool parseBool(const std::string& s) {
        if (s == "true" || s == "1" || s == "TRUE") return true;
        if (s == "false" || s == "0" || s == "FALSE") return false;
        throw std::runtime_error("Invalid bool literal: " + s);
    }

    static int parseInt(const std::string& s) {
        size_t pos = 0;
        const int v = std::stoi(s, &pos);
        if (pos != s.size()) {
            throw std::runtime_error("Invalid int literal: " + s);
        }
        return v;
    }

    static double parseDouble(const std::string& s) {
        size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (pos != s.size()) {
            throw std::runtime_error("Invalid double literal: " + s);
        }
        return v;
    }
};

} // namespace logic