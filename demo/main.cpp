#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <memory>
#include <any>
#include <type_traits>
#include <string>
#include <stdexcept>

#include "GlobalState.hpp"
#include "Configurator.hpp"
#include "Scheduler/Scheduler.hpp"
#include "DataGetter/DataGetter.hpp"
#include "DataGetter/DG_DS18B20.hpp"
#include "API/HttpServer.hpp"

#include "Executor/Executor.hpp"
#include "Executor/EX_DeviceControlModule.hpp"

// ------------------------------------------------------------
// Adapter: Field<T> -> GH_GlobalState getter map
// ------------------------------------------------------------
template<typename T>
struct Field {
    explicit Field(std::string key) : key_(std::move(key)) {}

    void set(const T& v) {
        if constexpr (std::is_same_v<T, float>) {
            GH_GlobalState::instance().setGetter(key_, static_cast<double>(v));
        } else {
            GH_GlobalState::instance().setGetter(key_, v);
        }
    }

private:
    std::string key_;
};

// ------------------------------------------------------------
// Adapter: ExecField<T> -> GH_GlobalState executor map
// ------------------------------------------------------------
template<typename T>
struct ExecField {
    explicit ExecField(std::string execName, GH_MODE mode = GH_MODE::AUTO)
        : execName_(std::move(execName)), mode_(mode) {}

    void set(const T& v) {
        auto& gs = GH_GlobalState::instance();
        const int id = gs.execIdByName(execName_);

        if constexpr (std::is_same_v<T, float>) {
            gs.setExec(id, static_cast<double>(v), mode_);
        } else {
            gs.setExec(id, v, mode_);
        }
    }

    void invalidate() {
        auto& gs = GH_GlobalState::instance();
        const int id = gs.execIdByName(execName_);
        gs.setExecInvalid(id);
    }

private:
    std::string execName_;
    GH_MODE mode_;
};

static volatile std::sig_atomic_t g_run = 1;
static void onSigInt(int) { g_run = 0; }

int main() {
    std::signal(SIGINT, onSigInt);

    auto& gs = GH_GlobalState::instance();
    GH_Configurator cfg;

    if (!cfg.loadFromTxt("DG_EXE_CONFIG.txt", gs)) {
        std::cerr << "Failed to load DG_EXE_CONFIG.txt\n";
        return 1;
    }

    // ------------------------------------------------------------
    // DataGetter
    // ------------------------------------------------------------
    dg::DataGetter dg;
    auto& ds = dg.emplace<dg::DG_DS18B20>("temp_ds18b20", "28-030397941733");

    Field<float> tempField("temp");
    ds.initRef(tempField);

    dg::ADataGetterStrategyBase::Ctx dgCtx;
    dg.init(dgCtx);

    // ------------------------------------------------------------
    // Executor + DCM
    // ------------------------------------------------------------
    exec::Executor executor;

    auto dcm = std::make_shared<DeviceControlModule>("/dev/ttyS3", 115200);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    executor.registerCommand(
        "DCM",
        std::make_unique<exec::EX_DeviceControlModule>()
    );

    executor.initCommandKV(
        "DCM",
        "dcm", dcm.get(),
        "flush_all_on_tick", false
    );

    // ------------------------------------------------------------
    // Generic DCM dispatch by config mapping
    // ------------------------------------------------------------
    auto enqueueMappedAndSync =
        [&](const std::string& execName,
            const std::any& rawValue,
            int priority = 10,
            GH_MODE mode = GH_MODE::AUTO) {
            try {
                auto bind = gs.getDcmBindingByName(execName);

                if (bind.tableId == DeviceControlModule::TABLE_DIGITAL) {
                    bool v = false;

                    if (rawValue.type() == typeid(bool)) {
                        v = std::any_cast<bool>(rawValue);
                    } else if (rawValue.type() == typeid(int)) {
                        v = (std::any_cast<int>(rawValue) != 0);
                    } else {
                        throw std::runtime_error("Digital binding expects bool/int for " + execName);
                    }

                    executor.enqueue(
                        "DCM",
                        priority,
                        bind.tableId,
                        bind.index,
                        v ? 1 : 0
                    );

                    ExecField<bool> f(execName, mode);
                    f.set(v);

                    std::cout << "[SYNC] DIGITAL "
                              << execName
                              << " <= " << std::boolalpha << v
                              << " [" << bind.tableId << "," << bind.index << "," << (v ? 1 : 0) << "]\n";
                    return;
                }

                if (bind.tableId == DeviceControlModule::TABLE_PWM) {
                    int pwm = 0;

                    if (rawValue.type() == typeid(int)) {
                        pwm = std::any_cast<int>(rawValue);
                    } else if (rawValue.type() == typeid(bool)) {
                        pwm = std::any_cast<bool>(rawValue) ? 255 : 0;
                    } else {
                        throw std::runtime_error("PWM binding expects int/bool for " + execName);
                    }

                    if (pwm < 0 || pwm > 255) {
                        throw std::runtime_error("PWM out of range 0..255 for " + execName);
                    }

                    executor.enqueue(
                        "DCM",
                        priority,
                        bind.tableId,
                        bind.index,
                        pwm
                    );

                    ExecField<int> f(execName, mode);
                    f.set(pwm);

                    std::cout << "[SYNC] PWM "
                              << execName
                              << " <= " << pwm
                              << " [" << bind.tableId << "," << bind.index << "," << pwm << "]\n";
                    return;
                }

                throw std::runtime_error("Unsupported tableId in DCM binding for " + execName);
            } catch (const std::exception& ex) {
                std::cout << "[SYNC] error for " << execName << ": " << ex.what() << "\n";
                try {
                    auto bind = gs.getDcmBindingByName(execName);
                    if (bind.tableId == DeviceControlModule::TABLE_DIGITAL) {
                        ExecField<bool> f(execName, mode);
                        f.invalidate();
                    } else {
                        ExecField<int> f(execName, mode);
                        f.invalidate();
                    }
                } catch (...) {}
            }
        };

    auto setExecModeByName =
        [&](const std::string& execName, GH_MODE mode) {
            const int id = gs.execIdByName(execName);
            gs.setExecMode(id, mode);
        };

    // ------------------------------------------------------------
    // HTTP command handler
    // ------------------------------------------------------------
    auto commandHandler =
        [&](const std::string& name,
            const std::string& action,
            const std::string& value) -> std::string {

            if (action == "mode") {
                GH_MODE m;
                if (value == "manual" || value == "MANUAL" || value == "0") {
                    m = GH_MODE::MANUAL;
                } else if (value == "auto" || value == "AUTO" || value == "1") {
                    m = GH_MODE::AUTO;
                } else {
                    throw std::runtime_error("mode must be manual/auto");
                }

                setExecModeByName(name, m);

                return std::string("{\"ok\":true,\"name\":\"") + name +
                       "\",\"action\":\"mode\",\"value\":\"" + toString(m) + "\"}";
            }

            if (action == "on" || action == "off") {
                const int id = gs.execIdByName(name);
                auto e = gs.getExecEntry(id);

                if (e.mode != GH_MODE::MANUAL) {
                    throw std::runtime_error("Executor is not in MANUAL mode: " + name);
                }

                const bool v = (action == "on");
                enqueueMappedAndSync(name, std::any(v), 10, GH_MODE::MANUAL);

                return std::string("{\"ok\":true,\"name\":\"") + name +
                       "\",\"action\":\"" + action + "\"}";
            }

            if (action == "set") {
                const int id = gs.execIdByName(name);
                auto e = gs.getExecEntry(id);

                if (e.mode != GH_MODE::MANUAL) {
                    throw std::runtime_error("Executor is not in MANUAL mode: " + name);
                }

                const int iv = std::stoi(value);
                enqueueMappedAndSync(name, std::any(iv), 10, GH_MODE::MANUAL);

                return std::string("{\"ok\":true,\"name\":\"") + name +
                       "\",\"action\":\"set\",\"value\":" + std::to_string(iv) + "}";
            }

            throw std::runtime_error("Unsupported action: " + action);
        };

    // ------------------------------------------------------------
    // Optional startup tests from config-mapped names
    // ------------------------------------------------------------
    try {
        enqueueMappedAndSync("LOW_DCM_D_0", std::any(true));
        enqueueMappedAndSync("LOW_DCM_D_0", std::any(false));
        enqueueMappedAndSync("LOW_DCM_D_1", std::any(true));
        enqueueMappedAndSync("LOW_DCM_D_1", std::any(false));
        enqueueMappedAndSync("LOW_DCM_D_2", std::any(true));
        enqueueMappedAndSync("LOW_DCM_D_2", std::any(false));
    } catch (...) {
        std::cout << "[BOOT] startup demo commands skipped or partially failed\n";
    }

    // ------------------------------------------------------------
    // Scheduler
    // ------------------------------------------------------------
    auto& sch = Scheduler::instance(4);

    sch.addPeriodic([&]() {
        try {
            dg.tick();

            auto e = gs.getGetterEntry("temp");
            double t = std::any_cast<double>(e.value);

            std::cout << "[DG] temp=" << t
                      << " valid=" << std::boolalpha << e.valid
                      << " stampMs=" << e.stampMs << "\n";
        } catch (const std::exception& ex) {
            gs.setGetterInvalid("temp");

            auto e = gs.getGetterEntry("temp");
            std::cout << "[DG] temp=INVALID"
                      << " valid=" << std::boolalpha << e.valid
                      << " stampMs=" << e.stampMs
                      << " err=" << ex.what() << "\n";
        }
    }, Scheduler::Ms(1000), "DG_DS18B20->GlobalState(temp)");

    sch.addPeriodic([&]() {
        try {
            const bool did = executor.tick();
            if (did) {
                std::cout << "[EXEC] moved one task from Executor queue\n";
            }
        } catch (const std::exception& ex) {
            std::cout << "[EXEC] tick() error: " << ex.what() << "\n";
        } catch (...) {
            std::cout << "[EXEC] tick() unknown error\n";
        }
    }, Scheduler::Ms(100), "Executor.tick()");

    sch.addPeriodic([&]() {
        try {
            executor.tickStrategies();
        } catch (const std::exception& ex) {
            std::cout << "[EXEC] tickStrategies() error: " << ex.what() << "\n";
        } catch (...) {
            std::cout << "[EXEC] tickStrategies() unknown error\n";
        }
    }, Scheduler::Ms(300), "Executor.tickStrategies()->DCM");

    // ------------------------------------------------------------
    // HTTP server
    // ------------------------------------------------------------
    auto httpServer = std::make_shared<GH_HttpServer>(8080, commandHandler);
    httpServer->start();

    sch.addDelayed([httpServer]() {
        httpServer->run(); // BLOCKING
    }, Scheduler::Ms(0), "HTTP ioc.run()");

    std::cout << "HTTP server on http://localhost:8080\n";
    std::cout << "Running. Ctrl+C to stop.\n";

    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    httpServer->stop();
    sch.stop();

    std::cout << "Stopped.\n";
    return 0;
}