#pragma once

#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "RuleTree.hpp"
#include "RuleNode.hpp"
#include "RuleEngine.hpp"
#include "ActionModel.hpp"
#include "LogicDebugJson.hpp"

namespace logic {

class LogicJsonController {
public:
    using json = nlohmann::json;

    LogicJsonController(RuleTree& tree,
                        RuleEngine& engine,
                        std::string filePath = "logic.json")
        : tree_(tree),
          engine_(engine),
          filePath_(std::move(filePath)) {}

    // --------------------------------------------------------
    // File operations
    // --------------------------------------------------------
    void loadFromFile() {
        std::lock_guard<std::mutex> lock(mutex_);
        RuleTree newTree = loadTreeFromFileUnlocked(filePath_);
        tree_ = std::move(newTree);
        engine_.requestRefresh();
    }

    void reloadFromFile() {
        loadFromFile();
    }

    void saveToFile(const json& j) {
        std::lock_guard<std::mutex> lock(mutex_);
        saveJsonToFileUnlocked(filePath_, j);
    }

    void uploadJson(const json& j) {
        std::lock_guard<std::mutex> lock(mutex_);
        RuleTree newTree = loadTreeFromJsonUnlocked(j);
        saveJsonToFileUnlocked(filePath_, j);
        tree_ = std::move(newTree);
        engine_.requestRefresh();
    }

    // --------------------------------------------------------
    // API helpers
    // --------------------------------------------------------
    json getTreeJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return treeStructureToJson(tree_);
    }

    json getRuntimeJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return treeRuntimeToJson(tree_);
    }

    json getFullJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return treeToJson(tree_);
    }

    json apiReload(const json& body = json::object()) {
        (void)body;
        std::lock_guard<std::mutex> lock(mutex_);

        RuleTree newTree = loadTreeFromFileUnlocked(filePath_);
        tree_ = std::move(newTree);
        engine_.requestRefresh();

        json res;
        res["ok"] = true;
        res["message"] = "logic reloaded from file";
        res["file"] = filePath_;
        return res;
    }

    json apiUpload(const json& body) {
        std::lock_guard<std::mutex> lock(mutex_);

        RuleTree newTree = loadTreeFromJsonUnlocked(body);
        saveJsonToFileUnlocked(filePath_, body);
        tree_ = std::move(newTree);
        engine_.requestRefresh();

        json res;
        res["ok"] = true;
        res["message"] = "logic uploaded and applied";
        res["file"] = filePath_;
        return res;
    }

    // --------------------------------------------------------
    // External synchronization for scheduler if needed
    // --------------------------------------------------------
    std::mutex& mutex() { return mutex_; }
    const std::string& filePath() const { return filePath_; }

private:
    RuleTree& tree_;
    RuleEngine& engine_;
    std::string filePath_;
    mutable std::mutex mutex_;

private:
    static TriggerMode parseTriggerMode(const std::string& s) {
        if (s == "on_enter")   return TriggerMode::ON_ENTER;
        if (s == "on_exit")    return TriggerMode::ON_EXIT;
        if (s == "while_true") return TriggerMode::WHILE_TRUE;
        if (s == "while_false")return TriggerMode::WHILE_FALSE;
        throw std::runtime_error("Unknown trigger mode: " + s);
    }

    static ActionValueType parseActionValueType(const std::string& s) {
        if (s == "bool")   return ActionValueType::BOOL;
        if (s == "int")    return ActionValueType::INT;
        if (s == "double") return ActionValueType::DOUBLE;
        if (s == "string") return ActionValueType::STRING;
        throw std::runtime_error("Unknown action value type: " + s);
    }

    static std::vector<std::string> parseStringArray(const json& j, const char* key) {
        std::vector<std::string> out;

        if (!j.contains(key)) {
            return out;
        }

        if (!j.at(key).is_array()) {
            throw std::runtime_error(std::string("Field '") + key + "' must be array");
        }

        for (const auto& v : j.at(key)) {
            if (!v.is_string()) {
                throw std::runtime_error(std::string("Field '") + key + "' must contain strings");
            }
            out.push_back(v.get<std::string>());
        }

        return out;
    }

    static std::vector<ActionModel> parseActions(const json& j) {
        std::vector<ActionModel> actions;

        if (!j.contains("actions")) {
            return actions;
        }

        if (!j.at("actions").is_array()) {
            throw std::runtime_error("Field 'actions' must be array");
        }

        for (const auto& a : j.at("actions")) {
            ActionModel action;

            action.target = a.at("target").get<std::string>();
            action.valueType = parseActionValueType(a.at("valueType").get<std::string>());
            action.value = a.at("value").get<std::string>();
            action.trigger = parseTriggerMode(a.value("trigger", "on_enter"));
            action.enabled = a.value("enabled", true);

            actions.push_back(std::move(action));
        }

        return actions;
    }

    static std::unique_ptr<RuleNode> parseRuleNode(const json& j) {
        const std::string title = j.value("title", "unnamed");
        const std::string condition = j.value("condition", "always");
        const auto args = parseStringArray(j, "args");
        const auto actions = parseActions(j);

        auto node = std::make_unique<RuleNode>(
            title,
            condition,
            args,
            actions
        );

        if (j.contains("children")) {
            if (!j.at("children").is_array()) {
                throw std::runtime_error("Field 'children' must be array");
            }

            for (const auto& ch : j.at("children")) {
                node->addChild(parseRuleNode(ch));
            }
        }

        return node;
    }

    static RuleTree loadTreeFromJsonUnlocked(const json& j) {
        if (!j.contains("root")) {
            throw std::runtime_error("Logic JSON must contain 'root'");
        }

        RuleTree tree;
        tree.setRoot(parseRuleNode(j.at("root")));
        return tree;
    }

    static RuleTree loadTreeFromFileUnlocked(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) {
            throw std::runtime_error("Failed to open logic file: " + path);
        }

        json j;
        in >> j;
        return loadTreeFromJsonUnlocked(j);
    }

    static void saveJsonToFileUnlocked(const std::string& path, const json& j) {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            throw std::runtime_error("Failed to open file for write: " + path);
        }

        out << j.dump(2);
        out.flush();

        if (!out) {
            throw std::runtime_error("Failed to write JSON file: " + path);
        }
    }
};

} // namespace logic