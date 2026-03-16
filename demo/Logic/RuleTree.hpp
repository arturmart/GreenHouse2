#pragma once

#include <memory>
#include <vector>
#include <queue>

#include "RuleNode.hpp"

namespace logic {

class RuleTree {
public:
    using Node = RuleNode;
    using Ptr  = std::unique_ptr<Node>;

    RuleTree() = default;
    explicit RuleTree(Ptr root) : root_(std::move(root)) {}

    Node* root() const { return root_.get(); }

    void setRoot(Ptr root) {
        root_ = std::move(root);
        if (root_) {
            root_->setParent(nullptr);
        }
    }

    std::vector<Node*> levelOrder() const {
        std::vector<Node*> out;
        if (!root_) return out;

        std::queue<Node*> q;
        q.push(root_.get());

        while (!q.empty()) {
            Node* cur = q.front();
            q.pop();

            out.push_back(cur);

            for (const auto& ch : cur->children()) {
                q.push(ch.get());
            }
        }

        return out;
    }

private:
    Ptr root_;
};

} // namespace logic