#include "Executor.hpp"
#include "Executor_Strategy_SIM.hpp"

#include <chrono>
#include <thread>
#include <iostream>

int main() {
    using namespace exec;
    using namespace std::chrono_literals;

    std::cout << "=== Boot ===\n";

    Executor ex;
    ex.registerCommand("SIM_HEAT", std::make_unique<ExecutorStrategySIM>());

    // Добавим задачи (priority — это не время)
    ex.enqueue("SIM_HEAT", 5,  true);   // приоритет 5
    ex.enqueue("SIM_HEAT", 10, false);  // приоритет 10 — выполнится первой
    ex.enqueue("SIM_HEAT", 7,  true);   // приоритет 7
    ex.enqueue("SIM_HEAT", 1,  false);  // приоритет 1
    ex.enqueue("UNKNOWN_CMD", 3, 123);  // нет такой команды

    std::cout << "[Main] queued() after enqueue = " << ex.queued() << std::endl;

    // Выполняем по одной задаче за тик
    int ticks = 0;
    while (ex.queued() > 0) {
        bool executed = ex.tick(); // должен снять ровно одну задачу
        ++ticks;
        if (executed) {
            std::cout << "[Main] tick #" << ticks
                      << " -> executed, remaining: " << ex.queued() << std::endl;
        } else {
            std::cout << "[Main] tick #" << ticks << " -> nothing executed" << std::endl;
            break;
        }
        std::this_thread::sleep_for(150ms);
    }

    std::cout << "=== Done ===\n";
    return 0;
}
