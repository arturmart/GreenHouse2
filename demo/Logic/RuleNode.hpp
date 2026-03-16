#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include "ActionModel.hpp"

namespace logic {

// ------------------------------------------------------------
// Runtime state for debugging and web
// ------------------------------------------------------------
struct RuleRuntimeState {
    bool localResult{false};         // result of this node's own condition
    bool effectiveResult{false};     // localResult AND parent effective
    bool prevEffectiveResult{false}; // previous tick effective state

    uint64_t lastEvalMs{0};
    uint64_t lastFireMs{0};

    std::string lastError;
    std::vector<std::string> resolvedArgs;
};

// ------------------------------------------------------------
// One rule node in tree
// ------------------------------------------------------------
class RuleNode {
public:
    using Ptr = std::unique_ptr<RuleNode>;

    RuleNode() = default;

    RuleNode(std::string title,
             std::string condition,
             std::vector<std::string> args = {},
             std::vector<ActionModel> actions = {})
        : title_(std::move(title)),
          condition_(std::move(condition)),
          args_(std::move(args)),
          actions_(std::move(actions)) {}

    RuleNode* parent() const { return parent_; }

    void setParent(RuleNode* p) { parent_ = p; }

    void addChild(Ptr child) {
        child->setParent(this);
        children_.push_back(std::move(child));
    }

    const std::vector<Ptr>& children() const { return children_; }
    std::vector<Ptr>& children() { return children_; }

    const std::string& title() const { return title_; }
    const std::string& condition() const { return condition_; }
    const std::vector<std::string>& args() const { return args_; }
    const std::vector<ActionModel>& actions() const { return actions_; }

    std::vector<std::string>& args() { return args_; }
    std::vector<ActionModel>& actions() { return actions_; }

    RuleRuntimeState& runtime() { return runtime_; }
    const RuleRuntimeState& runtime() const { return runtime_; }

private:
    std::string title_;
    std::string condition_;
    std::vector<std::string> args_;
    std::vector<ActionModel> actions_;

    std::vector<Ptr> children_;
    RuleNode* parent_{nullptr};

    RuleRuntimeState runtime_;
};

} // namespace logic