#pragma once

#include <any>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../GlobalState.hpp"
#include "Executor.hpp"
#include "EX_DeviceControlModule.hpp"

namespace control {

class ExecutorStateBridge {
public:
    ExecutorStateBridge(GH_GlobalState& gs, exec::Executor& executor)
        : gs_(gs), executor_(executor) {}

    void applyMappedActualAndQueue(const std::string& execName,
                                   const std::any& rawValue,
                                   int priority = 10,
                                   GH_MODE mode = GH_MODE::AUTO) {
        try {
            const auto bind = gs_.getDcmBindingByName(execName);
            const int id = gs_.execIdByName(execName);

            if (bind.tableId == DeviceControlModule::TABLE_DIGITAL) {
                bool v = false;

                if (rawValue.type() == typeid(bool)) {
                    v = std::any_cast<bool>(rawValue);
                } else if (rawValue.type() == typeid(int)) {
                    v = (std::any_cast<int>(rawValue) != 0);
                } else {
                    throw std::runtime_error(
                        "Digital binding expects bool/int for " + execName
                    );
                }

                executor_.enqueue(
                    "DCM",
                    priority,
                    bind.tableId,
                    bind.index,
                    v ? 1 : 0
                );

                gs_.setExecPending(id, true);
                gs_.setExecActual(id, v, mode, false);
                gs_.setExecPending(id, false);
                gs_.clearExecApplyError(id);

                std::cout << "[APPLY] DIGITAL "
                          << execName
                          << " <= " << std::boolalpha << v
                          << " [" << bind.tableId << "," << bind.index
                          << "," << (v ? 1 : 0) << "]\n";
                return;
            }

            if (bind.tableId == DeviceControlModule::TABLE_PWM) {
                int pwm = 0;

                if (rawValue.type() == typeid(int)) {
                    pwm = std::any_cast<int>(rawValue);
                } else if (rawValue.type() == typeid(bool)) {
                    pwm = std::any_cast<bool>(rawValue) ? 255 : 0;
                } else {
                    throw std::runtime_error(
                        "PWM binding expects int/bool for " + execName
                    );
                }

                if (pwm < 0 || pwm > 255) {
                    throw std::runtime_error(
                        "PWM out of range 0..255 for " + execName
                    );
                }

                executor_.enqueue(
                    "DCM",
                    priority,
                    bind.tableId,
                    bind.index,
                    pwm
                );

                gs_.setExecPending(id, true);
                gs_.setExecActual(id, pwm, mode, false);
                gs_.setExecPending(id, false);
                gs_.clearExecApplyError(id);

                std::cout << "[APPLY] PWM "
                          << execName
                          << " <= " << pwm
                          << " [" << bind.tableId << "," << bind.index
                          << "," << pwm << "]\n";
                return;
            }

            throw std::runtime_error(
                "Unsupported tableId in DCM binding for " + execName
            );
        } catch (const std::exception& ex) {
            std::cout << "[APPLY] error for " << execName
                      << ": " << ex.what() << "\n";

            try {
                const int id = gs_.execIdByName(execName);
                gs_.setExecApplyError(id, ex.what());
                gs_.setExecActualInvalid(id, ex.what());
            } catch (...) {
            }
        }
    }

    void tick() {
        try {
            auto execs = gs_.snapshotExecutors();

            for (const auto& e : execs) {
                if (!e.desired.dirty) {
                    continue;
                }

                const std::string& execName = e.name;
                const int id = e.id;

                try {
                    // 1. mode sync
                    if (e.actual.mode != e.desired.mode) {
                        gs_.setExecActualMode(id, e.desired.mode);
                    }

                    // 2. desired invalid
                    if (!e.desired.valid) {
                        gs_.setExecActualInvalid(id, "desired invalid");
                        gs_.markExecDirty(id, false);
                        continue;
                    }

                    // 3. dedup actual vs desired
                    bool sameType = (e.actual.value.type() == e.desired.value.type());
                    bool sameValue = false;

                    if (sameType) {
                        if (e.desired.value.type() == typeid(bool)) {
                            sameValue =
                                std::any_cast<bool>(e.actual.value) ==
                                std::any_cast<bool>(e.desired.value);
                        } else if (e.desired.value.type() == typeid(int)) {
                            sameValue =
                                std::any_cast<int>(e.actual.value) ==
                                std::any_cast<int>(e.desired.value);
                        } else if (e.desired.value.type() == typeid(double)) {
                            sameValue =
                                std::any_cast<double>(e.actual.value) ==
                                std::any_cast<double>(e.desired.value);
                        }
                    }

                    if (e.actual.valid &&
                        sameType &&
                        sameValue &&
                        e.actual.mode == e.desired.mode) {
                        gs_.markExecDirty(id, false);
                        continue;
                    }

                    // 4. apply
                    applyMappedActualAndQueue(execName, e.desired.value, 10, e.desired.mode);

                    // 5. clear dirty after success
                    gs_.markExecDirty(id, false);
                } catch (const std::exception& ex) {
                    gs_.setExecApplyError(id, ex.what());
                } catch (...) {
                    gs_.setExecApplyError(id, "unknown bridge error");
                }
            }
        } catch (const std::exception& ex) {
            std::cout << "[BRIDGE] error: " << ex.what() << "\n";
        } catch (...) {
            std::cout << "[BRIDGE] unknown error\n";
        }
    }

private:
    GH_GlobalState& gs_;
    exec::Executor& executor_;
};

} // namespace control