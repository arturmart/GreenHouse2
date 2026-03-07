#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <memory>
#include <any>
#include <type_traits>
#include <string>

#include "GlobalState.hpp"
#include "Scheduler/Scheduler.hpp"
#include "DataGetter/DataGetter.hpp"
#include "DataGetter/DG_DS18B20.hpp"
#include "API/HttpServer.hpp"

#include "Executor/Executor.hpp"
#include "Executor/EX_DeviceControlModule.hpp"

// ------------------------------------------------------------
// Adapter: Field<T> -> writes into GH_GlobalState (getter map)
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
// Adapter: ExecField<T> -> writes into GH_GlobalState (executor map)
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
    if (!gs.loadFromTxt("DG_EXE_CONFIG.txt")) {
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
    // Executor + DeviceControlModule
    // ------------------------------------------------------------
    exec::Executor executor;

    auto dcm = std::make_shared<DeviceControlModule>("/dev/ttyS3", 115200);

    // Часто после открытия UART/Arduino нужно подождать
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
    // Main-level sync helpers:
    // enqueue to Executor + sync desired state to GlobalState
    // ------------------------------------------------------------
    auto enqueueDigitalAndSync =
        [&](const std::string& execName, int idx, bool value, int priority = 10, GH_MODE mode = GH_MODE::AUTO) {
            try {
                executor.enqueue("DCM", priority, DeviceControlModule::TABLE_DIGITAL, idx, value ? 1 : 0);

                ExecField<bool> f(execName, mode);
                f.set(value);

                std::cout << "[SYNC] DIGITAL "
                          << execName
                          << " <= " << std::boolalpha << value
                          << "  [68," << idx << "," << (value ? 1 : 0) << "]\n";
            } catch (const std::exception& ex) {
                std::cout << "[SYNC] DIGITAL error for " << execName
                          << ": " << ex.what() << "\n";
                try {
                    ExecField<bool> f(execName, mode);
                    f.invalidate();
                } catch (...) {}
            }
        };

    auto enqueuePwmAndSync =
        [&](const std::string& execName, int idx, int pwm, int priority = 10, GH_MODE mode = GH_MODE::AUTO) {
            try {
                executor.enqueue("DCM", priority, DeviceControlModule::TABLE_PWM, idx, pwm);

                ExecField<int> f(execName, mode);
                f.set(pwm);

                std::cout << "[SYNC] PWM "
                          << execName
                          << " <= " << pwm
                          << "  [80," << idx << "," << pwm << "]\n";
            } catch (const std::exception& ex) {
                std::cout << "[SYNC] PWM error for " << execName
                          << ": " << ex.what() << "\n";
                try {
                    ExecField<int> f(execName, mode);
                    f.invalidate();
                } catch (...) {}
            }
        };

    // ------------------------------------------------------------
    // Test commands
    // Здесь mapping живёт в main, а не в стратегии
    // ------------------------------------------------------------
    enqueueDigitalAndSync("Bake",    0, true);
    enqueueDigitalAndSync("Bake",    0, false);

    enqueueDigitalAndSync("Pump",    1, true);
    enqueueDigitalAndSync("Pump",    1, false);

    enqueueDigitalAndSync("Falcon1", 2, true);
    enqueueDigitalAndSync("Falcon1", 2, false);

    enqueueDigitalAndSync("Falcon2", 3, true);
    enqueueDigitalAndSync("Falcon2", 3, false);

    // Если PWM канал у тебя, например, Light1 -> 80,0
    // enqueuePwmAndSync("Light1", 0, 128);

    // keyword-команды можно по-прежнему напрямую:
    // executor.enqueue("DCM", 5, std::string("showall"));

    // ------------------------------------------------------------
    // Scheduler
    // ------------------------------------------------------------
    auto& sch = Scheduler::instance(4);

    // 1) Periodic sensor tick
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

    // 2) Executor queue -> strategies
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

    // 3) Strategies -> DCM/UART
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
    auto httpServer = std::make_shared<GH_HttpServer>(8080);
    httpServer->start();

    std::cout << "HTTP server on http://localhost:8080\n";
    std::cout << "GET /status\n";
    std::cout << "GET /schema/getters\n";
    std::cout << "GET /schema/executors\n";
    std::cout << "GET /getters\n";
    std::cout << "GET /getters/<key>\n";
    std::cout << "GET /executors\n";

    sch.addDelayed([httpServer]() {
        httpServer->run(); // BLOCKING
    }, Scheduler::Ms(0), "HTTP ioc.run()");

    std::cout << "Running. Ctrl+C to stop.\n";

    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    httpServer->stop();
    sch.stop();

    std::cout << "Stopped.\n";
    return 0;
}