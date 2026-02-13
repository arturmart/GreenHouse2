// main.cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

#include "GlobalState.hpp"
#include "Scheduler/Scheduler.hpp"
#include "DataGetter/DataGetter.hpp"
#include "DataGetter/DG_DS18B20.hpp"

// ------------------------------------------------------------
// Adapter: Field<T> -> writes into GH_GlobalState (getter map)
// ------------------------------------------------------------
template<typename T>
struct Field {
    explicit Field(std::string key) : key_(std::move(key)) {}

    void set(const T& v) {
        // DS18B20 отдаёт float, но у тебя в schema temp=temp2=tempOut -> DOUBLE
        // Поэтому кладём как double, чтобы не словить mismatch.
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

static volatile std::sig_atomic_t g_run = 1;

static void onSigInt(int) {
    g_run = 0;
}

int main() {
    std::signal(SIGINT, onSigInt);

    // 1) GlobalState already has defaults in ctor (executors/getters/schema).
    auto& gs = GH_GlobalState::instance();

    // 2) DataGetter + стратегия DS18B20
    dg::DataGetter dg;

    // !!! ВПИШИ СВОЙ ID ДАТЧИКА (пример):
    // ls /sys/bus/w1/devices/  -> увидишь что-то типа 28-00000abcdef0
    const std::string sensorId = "28-030397941733";

    auto& ds = dg.emplace<dg::DG_DS18B20>("temp_ds18b20", sensorId);

    // 3) Привязка стратегии к "Field<float>" (который будет писать в GlobalState key="temp")
    Field<float> tempField("temp");
    ds.initRef(tempField);

    // 4) init() всем стратегиям (если нужно через ctx — дополни)
    dg::ADataGetterStrategyBase::Ctx ctx;
    dg.init(ctx);

    // 5) Scheduler: 1 поток в пуле (как ты просил)
    auto& sch = Scheduler::instance(1);

    // 6) Периодическая задача: читаем датчик -> сохраняем -> печатаем
    sch.addPeriodic([&]() {
        try {
            dg.tick(); // ds.tick() -> getDataRef() -> Field<float>::set() -> GlobalState.setGetter("temp", double)

            auto e = gs.getGetterEntry("temp");
            double t = std::any_cast<double>(e.value);

            std::cout << "[DG] temp=" << t
                      << " valid=" << std::boolalpha << e.valid
                      << " stampMs=" << e.stampMs
                      << "\n";
        } catch (const std::exception& ex) {
            // Мягкая ошибка сенсора: помечаем invalid и печатаем причину
            gs.setGetterInvalid("temp");
            auto e = gs.getGetterEntry("temp");

            std::cout << "[DG] temp=INVALID"
                      << " valid=" << std::boolalpha << e.valid
                      << " stampMs=" << e.stampMs
                      << " err=" << ex.what()
                      << "\n";
        }
    }, Scheduler::Ms(1000), "DG_DS18B20->GlobalState(temp)");

    std::cout << "Running. Press Ctrl+C to stop.\n";

    // 7) main thread просто живёт, пока Ctrl+C
    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 8) graceful stop
    sch.stop();
    std::cout << "Stopped.\n";
    return 0;
}
