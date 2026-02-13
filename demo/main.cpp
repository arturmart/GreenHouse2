// main.cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

#include "GlobalState.hpp"
#include "Scheduler/Scheduler.hpp"
#include "DataGetter/DataGetter.hpp"
#include "DataGetter/DG_DS18B20.hpp"
#include "API/HttpServer.hpp"

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

static volatile std::sig_atomic_t g_run = 1;
static void onSigInt(int) { g_run = 0; }

int main() {
    std::signal(SIGINT, onSigInt);

    auto& gs = GH_GlobalState::instance();
    if (!gs.loadFromTxt("DG_EXE_CONFIG.txt")) {
        std::cerr << "Failed to load DG_EXE_CONFIG.txt\n";
        return 1;
    }

    dg::DataGetter dg;
    auto& ds = dg.emplace<dg::DG_DS18B20>("temp_ds18b20", "28-030397941733");

    Field<float> tempField("temp");
    ds.initRef(tempField);

    dg::ADataGetterStrategyBase::Ctx ctx;
    dg.init(ctx);

    // ВАЖНО: 2 потока: один под sensor tick, один под HTTP ioc.run()
    auto& sch = Scheduler::instance(2);

    // 1) Периодический сенсор
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

    // 2) HTTP server — стартуем и запускаем run() как “вечную” задачу
    auto httpServer = std::make_shared<GH_HttpServer>(8080);
    httpServer->start();

    sch.addDelayed([httpServer]() {
        std::cout << "HTTP server on http://localhost:8080\n";
        std::cout << "GET /status\n";
        std::cout << "GET /schema/getters\n";
        std::cout << "GET /schema/executors\n";
        std::cout << "GET /getters\n";
        std::cout << "GET /getters/<key>\n";
        std::cout << "GET /executors\n";
        httpServer->run(); // BLOCKING
    }, Scheduler::Ms(0), "HTTP ioc.run()");

    std::cout << "Running. Ctrl+C to stop.\n";
    while (g_run) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // stop http first
    httpServer->stop();

    // stop scheduler
    sch.stop();
    std::cout << "Stopped.\n";
    return 0;
}
