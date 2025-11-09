#pragma once
#include "AExecutor_Strategy.hpp"
#include "../SIM/ISimControl.hpp"
#include <iostream>

namespace exec {

class ExecutorStrategySIM final : public AExecutorStrategy {
public:
    ExecutorStrategySIM() = default;
    explicit ExecutorStrategySIM(ISimControl* sim) : sim_(sim) {}
    explicit ExecutorStrategySIM(ISimControl& sim) : sim_(&sim) {}

    // Унифицированная инициализация:
    // ожидает ctx["sim"] как ISimControl*
    void init(const Ctx& ctx) override {
        auto it = ctx.find("sim");
        if (it != ctx.end() && it->second.type() == typeid(ISimControl*)) {
            sim_ = std::any_cast<ISimControl*>(it->second);
        }
    }

    // Явный сеттер тоже оставим (удобно при прямом связывании)
    void setSim(ISimControl* sim) { sim_ = sim; }

    void execute(const Args& args) override {
        if (!sim_) {
            std::cout << "[SIM] no simulator bound\n";
            return;
        }

        // ожидаем: [0] bool enable (опц.)
        bool enable = false;
        if (args.size() >= 1 && args[0].type() == typeid(bool)) {
            enable = std::any_cast<bool>(args[0]);
            sim_->setEnabled(enable);
        }

        std::cout << "[ExecutorStrategySIM] enable=" << std::boolalpha << enable << "\n";
    }

    std::string name() const override { return "SIM_HEAT"; }

private:
    ISimControl* sim_ = nullptr; // не владеем
};

} // namespace exec
