#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>
#include <any>
#include <vector>

// ----- Базовая стратегия -----
class AExecutorStrategy {
public:
    virtual ~AExecutorStrategy() = default;
    virtual void tick() = 0;
    virtual void execute(const std::vector<std::any>& args) = 0;
};

// ----- Разные реализации -----
class PrintInt : public AExecutorStrategy {
public:
    void tick() override {}
    void execute(const std::vector<std::any>& args) override {
        if (!args.empty() && args[0].type() == typeid(int)) {
            int value = std::any_cast<int>(args[0]);
            std::cout << "Execute with int=" << value << "\n";
        }
    }
};

class PrintFloat : public AExecutorStrategy {
public:
    void tick() override {}
    void execute(const std::vector<std::any>& args) override {
        if (!args.empty() && args[0].type() == typeid(float)) {
            float value = std::any_cast<float>(args[0]);
            std::cout << "Execute with float=" << value << "\n";
        }
    }
};

class PrintBoolBool : public AExecutorStrategy {
public:
    void tick() override {}
    void execute(const std::vector<std::any>& args) override {
        if (args.size() == 2 &&
            args[0].type() == typeid(bool) &&
            args[1].type() == typeid(bool)) {
            bool a = std::any_cast<bool>(args[0]);
            bool b = std::any_cast<bool>(args[1]);
            std::cout << "Execute with bool=" << a << " bool=" << b << "\n";
        }
    }
};

// ----- Executor -----
class Executor {
    std::unordered_map<std::string, std::unique_ptr<AExecutorStrategy>> commands;
public:
    Executor() {
        init();
    }

    void init() {
        commands["i"]  = std::make_unique<PrintInt>();
        commands["f"]  = std::make_unique<PrintFloat>();
        commands["bb"] = std::make_unique<PrintBoolBool>();
    }

    void run(const std::string& key, const std::vector<std::any>& args) {
        auto it = commands.find(key);
        if (it != commands.end()) {
            it->second->execute(args);
        } else {
            std::cout << "Command not found: " << key << "\n";
        }
    }
};

// ----- main -----
int main() {
    Executor ex;

    ex.run("i", {42});
    ex.run("f", {3.14f});
    ex.run("bb", {true, false});
    ex.run("x", {123}); // нет такой команды
}
