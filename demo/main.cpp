#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <memory>
#include <any>
#include <type_traits>
#include <string>
#include <stdexcept>
#include <vector>

#include "GlobalState.hpp"
#include "Configurator.hpp"
#include "Scheduler/Scheduler.hpp"
#include "DataGetter/DataGetter.hpp"
#include "DataGetter/DG_DS18B20.hpp"
#include "DataGetter/DG_OWM_Weather.hpp"
#include "DataGetter/DG_SYS_MEM.hpp"
#include "DataGetter/DG_SYS_CPU.hpp"
#include "DataGetter/DG_SYS_DISK.hpp"
#include "DataGetter/DG_SYS_TIME.hpp"
#include "API/HttpServer.hpp"

#include "Executor/Executor.hpp"
#include "Executor/EX_DeviceControlModule.hpp"
#include "Executor/ExecutorStateBridge.hpp"

#include "Logic/ActionModel.hpp"
#include "Logic/RuleNode.hpp"
#include "Logic/RuleTree.hpp"
#include "Logic/RuleEngine.hpp"
#include "API/JsonAPI.hpp"
#include "Logic/LogicDebugJson.hpp"

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
// Desired executor field
// ------------------------------------------------------------
template<typename T>
struct ExecDesiredField {
    explicit ExecDesiredField(std::string execName,
                              GH_MODE mode = GH_MODE::AUTO,
                              std::string writer = "unknown")
        : execName_(std::move(execName)),
          mode_(mode),
          writer_(std::move(writer)) {}

    void set(const T& v) {
        auto& gs = GH_GlobalState::instance();
        const int id = gs.execIdByName(execName_);

        if constexpr (std::is_same_v<T, float>) {
            gs.setExecDesired(id, static_cast<double>(v), mode_, writer_, true);
        } else {
            gs.setExecDesired(id, v, mode_, writer_, true);
        }
    }

    void invalidate() {
        auto& gs = GH_GlobalState::instance();
        const int id = gs.execIdByName(execName_);
        gs.setExecDesiredInvalid(id, writer_, true);
    }

private:
    std::string execName_;
    GH_MODE mode_;
    std::string writer_;
};

// ------------------------------------------------------------
// Actual executor field
// ------------------------------------------------------------
template<typename T>
struct ExecActualField {
    explicit ExecActualField(std::string execName,
                             GH_MODE mode = GH_MODE::AUTO)
        : execName_(std::move(execName)),
          mode_(mode) {}

    void set(const T& v) {
        auto& gs = GH_GlobalState::instance();
        const int id = gs.execIdByName(execName_);

        if constexpr (std::is_same_v<T, float>) {
            gs.setExecActual(id, static_cast<double>(v), mode_, false);
        } else {
            gs.setExecActual(id, v, mode_, false);
        }
    }

    void invalidate(const std::string& err = {}) {
        auto& gs = GH_GlobalState::instance();
        const int id = gs.execIdByName(execName_);
        gs.setExecActualInvalid(id, err);
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
    dg::ADataGetterStrategyBase::Ctx dgCtx;

    // IMPORTANT:
    // strategies keep pointers to Field<T>, so fields must live
    // for the whole program lifetime.
    std::vector<std::unique_ptr<Field<float>>> getterFieldsFloat;
    std::vector<std::unique_ptr<Field<double>>> getterFieldsDouble;
    std::vector<std::unique_ptr<Field<int>>> getterFieldsInt;
    std::vector<std::unique_ptr<Field<bool>>> getterFieldsBool;
    std::vector<std::unique_ptr<Field<std::string>>> getterFieldsString;
    std::vector<std::unique_ptr<Field<tools::UnixMs>>> getterFieldsTime;

    // ------------------------------------------------------------
    // Create getter strategies from config
    // ------------------------------------------------------------
    try {
        for (const auto& kv : cfg.getterBindings()) {
            const std::string& getterKey = kv.first;
            const auto& bind = kv.second;

            if (bind.strategy == "DG_DS18B20") {
                if (bind.args.size() < 1) {
                    throw std::runtime_error(
                        "DG_DS18B20 requires sensor id for getter: " + getterKey
                    );
                }

                auto& strat = dg.emplace<dg::DG_DS18B20>(
                    "dg_" + getterKey,
                    bind.args[0]
                );

                getterFieldsFloat.push_back(
                    std::make_unique<Field<float>>(getterKey)
                );

                strat.initRef(*getterFieldsFloat.back());

                std::cout << "[CFG] getter " << getterKey
                          << " -> DG_DS18B20(" << bind.args[0] << ")\n";
                continue;
            }

            if (bind.strategy == "STATIC_STRING") {
                std::cout << "[CFG] getter " << getterKey
                          << " uses STATIC_STRING (not instantiated in this main)\n";
                continue;
            }

            if (bind.strategy == "DG_OWM_WEATHER") {
                if (bind.args.size() < 5) {
                    throw std::runtime_error(
                        "DG_OWM_WEATHER requires: apiKey, lat, lon, fieldKey, cacheMs for getter: " + getterKey
                    );
                }

                const std::string apiKey = bind.args[0];
                const double lat = std::stod(bind.args[1]);
                const double lon = std::stod(bind.args[2]);
                const std::string fieldKey = bind.args[3];
                const long long cacheMs = std::stoll(bind.args[4]);

                auto& strat = dg.emplace<dg::DG_OWM_Weather>(
                    "dg_" + getterKey,
                    apiKey,
                    lat,
                    lon,
                    fieldKey,
                    cacheMs
                );

                getterFieldsDouble.push_back(
                    std::make_unique<Field<double>>(getterKey)
                );

                strat.initRef(*getterFieldsDouble.back());

                std::cout << "[CFG] getter " << getterKey
                          << " -> DG_OWM_WEATHER(" << fieldKey
                          << ", cacheMs=" << cacheMs << ")\n";
                continue;
            }

            if (bind.strategy == "DG_SYS_MEM") {
                if (bind.args.size() < 1) {
                    throw std::runtime_error(
                        "DG_SYS_MEM requires field argument for getter: " + getterKey
                    );
                }

                const std::string field = bind.args[0];
                dg::DG_SYS_MEM::Field memField;

                if (field == "total") {
                    memField = dg::DG_SYS_MEM::Field::MEM_TOTAL;
                } else if (field == "free") {
                    memField = dg::DG_SYS_MEM::Field::MEM_FREE;
                } else if (field == "available") {
                    memField = dg::DG_SYS_MEM::Field::MEM_AVAILABLE;
                } else if (field == "process") {
                    memField = dg::DG_SYS_MEM::Field::MEM_PROCESS;
                } else {
                    throw std::runtime_error(
                        "DG_SYS_MEM unknown field '" + field +
                        "' for getter: " + getterKey
                    );
                }

                auto& strat = dg.emplace<dg::DG_SYS_MEM>(
                    "dg_" + getterKey,
                    memField
                );

                getterFieldsDouble.push_back(
                    std::make_unique<Field<double>>(getterKey)
                );

                strat.initRef(*getterFieldsDouble.back());

                std::cout << "[CFG] getter " << getterKey
                          << " -> DG_SYS_MEM(" << field << ")\n";
                continue;
            }

            if (bind.strategy == "DG_SYS_CPU") {
                auto& strat = dg.emplace<dg::DG_SYS_CPU>("dg_" + getterKey);

                getterFieldsDouble.push_back(
                    std::make_unique<Field<double>>(getterKey)
                );

                strat.initRef(*getterFieldsDouble.back());

                std::cout << "[CFG] getter " << getterKey
                          << " -> DG_SYS_CPU\n";
                continue;
            }

            if (bind.strategy == "DG_SYS_DISK") {
                if (bind.args.size() < 1) {
                    throw std::runtime_error(
                        "DG_SYS_DISK requires field argument for getter: " + getterKey
                    );
                }

                const std::string field = bind.args[0];
                const std::string path = (bind.args.size() >= 2) ? bind.args[1] : "/";

                dg::DG_SYS_DISK::Field diskField;

                if (field == "total") {
                    diskField = dg::DG_SYS_DISK::Field::TOTAL;
                } else if (field == "free") {
                    diskField = dg::DG_SYS_DISK::Field::FREE;
                } else if (field == "available") {
                    diskField = dg::DG_SYS_DISK::Field::AVAILABLE;
                } else {
                    throw std::runtime_error(
                        "DG_SYS_DISK unknown field '" + field +
                        "' for getter: " + getterKey
                    );
                }

                auto& strat = dg.emplace<dg::DG_SYS_DISK>(
                    "dg_" + getterKey,
                    diskField,
                    path
                );

                getterFieldsDouble.push_back(
                    std::make_unique<Field<double>>(getterKey)
                );

                strat.initRef(*getterFieldsDouble.back());

                std::cout << "[CFG] getter " << getterKey
                          << " -> DG_SYS_DISK(" << field
                          << ", path=" << path << ")\n";
                continue;
            }

            std::cout << "[CFG] warning: unsupported getter strategy for "
                      << getterKey << ": " << bind.strategy << "\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "[CFG] getter binding init error: " << ex.what() << "\n";
        return 1;
    }

    // ------------------------------------------------------------
    // Manual DataGetter strategies (without Configurator)
    // ------------------------------------------------------------
    {
        auto& strat = dg.emplace<dg::DG_TIME>("dg_time");

        getterFieldsTime.push_back(
            std::make_unique<Field<tools::UnixMs>>("time")
        );

        strat.initRef(*getterFieldsTime.back());

        std::cout << "[MAIN] getter time -> DG_TIME\n";
    }

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
    control::ExecutorStateBridge execBridge(gs, executor);
    // ------------------------------------------------------------
    // LogicEngine manual test tree
    // ------------------------------------------------------------
    logic::RuleTree logicTree;

    {
        using logic::ActionModel;
        using logic::ActionValueType;
        using logic::RuleNode;
        using logic::TriggerMode;

        auto root = std::make_unique<RuleNode>(
            "main",
            "always",
            std::vector<std::string>{},
            std::vector<ActionModel>{}
        );

        // temp > 26  -> LOW_DCM_D_0 = true
        {
            std::vector<ActionModel> actions;
            actions.push_back(ActionModel{
                "LOW_DCM_D_0",
                ActionValueType::BOOL,
                "true",
                TriggerMode::ON_ENTER,
                true
            });

            root->addChild(std::make_unique<RuleNode>(
                "Cooler1On",
                "gt",
                std::vector<std::string>{"temp", "26"},
                actions
            ));
        }

        // temp < 24  -> LOW_DCM_D_0 = false
        {
            std::vector<ActionModel> actions;
            actions.push_back(ActionModel{
                "LOW_DCM_D_0",
                ActionValueType::BOOL,
                "false",
                TriggerMode::ON_ENTER,
                true
            });

            root->addChild(std::make_unique<RuleNode>(
                "Cooler1Off",
                "lt",
                std::vector<std::string>{"temp", "24"},
                actions
            ));
        }

        logicTree.setRoot(std::move(root));
    }

    logic::RuleEngine logicEngine(gs, logicTree);

    api::JsonApi jsonApi;

    jsonApi.registerGetter("logic/tree", [&]() {
        return logic::treeStructureToJson(logicTree);
    });

    jsonApi.registerGetter("logic/runtime", [&]() {
        return logic::treeRuntimeToJson(logicTree);
    });

    jsonApi.registerGetter("logic/full", [&]() {
        return logic::treeToJson(logicTree);
    });
    

    // ------------------------------------------------------------
    // Desired-state API helpers
    // ------------------------------------------------------------
    auto setExecDesiredModeByName =
        [&](const std::string& execName, GH_MODE mode, const std::string& writer = "api") {
            const int id = gs.execIdByName(execName);
            gs.setExecDesiredMode(id, mode, writer, true);
        };

    // ------------------------------------------------------------
    // HTTP command handler
    // API now writes DESIRED state only.
    // ------------------------------------------------------------
    auto commandHandler =
        [&](const std::string& name,
            const std::string& action,
            const std::string& value) -> std::string {

            const int id = gs.execIdByName(name);

            if (action == "mode") {
                GH_MODE m;

                if (value == "manual" || value == "MANUAL" || value == "0") {
                    m = GH_MODE::MANUAL;
                }
                else if (value == "auto" || value == "AUTO" || value == "1") {
                    m = GH_MODE::AUTO;
                }
                else {
                    throw std::runtime_error("mode must be manual/auto");
                }

                setExecDesiredModeByName(name, m, "api");

                // actual mode switches immediately
                gs.setExecActualMode(id, m);

                // IMPORTANT: when returning to AUTO force rule refresh
                if (m == GH_MODE::AUTO) {
                    logicEngine.requestRefresh();
                }

                return std::string("{\"ok\":true,\"name\":\"") + name +
                    "\",\"action\":\"mode\",\"value\":\"" + toString(m) + "\"}";
            }

            auto actual = gs.getExecActualEntry(id);
            GH_MODE effectiveMode = actual.mode;

            if (action == "on" || action == "off") {
                if (effectiveMode != GH_MODE::MANUAL) {
                    throw std::runtime_error("Executor is not in MANUAL mode: " + name);
                }

                const bool v = (action == "on");
                gs.setExecDesired(id, v, GH_MODE::MANUAL, "api", true);

                return std::string("{\"ok\":true,\"name\":\"") + name +
                       "\",\"action\":\"" + action + "\"}";
            }

            if (action == "set") {
                if (effectiveMode != GH_MODE::MANUAL) {
                    throw std::runtime_error("Executor is not in MANUAL mode: " + name);
                }

                const int iv = std::stoi(value);
                gs.setExecDesired(id, iv, GH_MODE::MANUAL, "api", true);

                return std::string("{\"ok\":true,\"name\":\"") + name +
                       "\",\"action\":\"set\",\"value\":" + std::to_string(iv) + "}";
            }

            throw std::runtime_error("Unsupported action: " + action);
        };

    // ------------------------------------------------------------
    // Boot desired-state demo
    // ------------------------------------------------------------
    try {
        {
            const int id = gs.execIdByName("LOW_DCM_D_0");
            gs.setExecDesired(id, true,  GH_MODE::AUTO, "boot", true);
            gs.setExecDesired(id, false, GH_MODE::AUTO, "boot", true);
        }
        {
            const int id = gs.execIdByName("LOW_DCM_D_1");
            gs.setExecDesired(id, true,  GH_MODE::AUTO, "boot", true);
            gs.setExecDesired(id, false, GH_MODE::AUTO, "boot", true);
        }
        {
            const int id = gs.execIdByName("LOW_DCM_D_2");
            gs.setExecDesired(id, true,  GH_MODE::AUTO, "boot", true);
            gs.setExecDesired(id, false, GH_MODE::AUTO, "boot", true);
        }
    } catch (...) {
        std::cout << "[BOOT] startup desired-state demo skipped or partially failed\n";
    }

    // ------------------------------------------------------------
    // Scheduler
    // ------------------------------------------------------------
    auto& sch = Scheduler::instance(4);

    sch.addPeriodic([&]() {
        try {
            dg.tick();
        } catch (const std::exception& ex) {
            std::cout << "[DG] error: " << ex.what() << "\n";
        }
    }, Scheduler::Ms(1000), "DG tick -> GlobalState");

    sch.addPeriodic([&]() {
        try {
            logicEngine.tick();
        } catch (const std::exception& ex) {
            std::cout << "[LOGIC] tick error: " << ex.what() << "\n";
        } catch (...) {
            std::cout << "[LOGIC] tick unknown error\n";
        }
    }, Scheduler::Ms(500), "Logic.tick()");

    sch.addPeriodic([&]() {
        execBridge.tick();
    }, Scheduler::Ms(150), "DesiredState bridge -> Executor");

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
    auto httpServer = std::make_shared<GH_HttpServer>(8080, commandHandler, &jsonApi);
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