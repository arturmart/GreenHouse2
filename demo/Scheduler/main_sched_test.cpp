
#include "scheduler_monitor.hpp"
#include <csignal>
#include <pthread.h>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;

// Пример задач
void taskFast() { 
    std::this_thread::sleep_for(100ms);
    std::cout << "[B] fast [5Hz]\n"; 
}
void taskSlow() { 
    std::this_thread::sleep_for(900ms);
    std::cout << "[C] slow [1Hz]-\n"; 
}
void taskOneShot() { 
    std::this_thread::sleep_for(500ms);
    std::cout << "[A] one-shot\n"; 
}

int main() {
    // 1) перехват сигналов (корректное завершение)
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    // блокируем сигналы в главном потоке (и все дочерние унаследуют маску)
    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    // 2) старт планировщика и задач
    auto& sched = Scheduler::instance(4);
    sched.addDelayed(taskOneShot, 4s, "OneShot A");
    auto idFast = sched.addPeriodic(taskFast, 200ms,  "Fast");
    auto idSlow = sched.addPeriodic(taskSlow, 1000ms, "Slow");

    // 3) монитор в отдельном модуле/потоке
    TerminalMonitor mon(100ms, 60, 3);
    mon.start(sched);

    std::cout << "[Main] Running. Press Ctrl+C to exit...\n";

    // 4) отдельный поток ждет сигнал и завершает всё
    std::atomic<bool> stop{false};
    std::thread sigthr([&]{
        int sig = 0;
        sigwait(&set, &sig);
        stop = true;
    });

    // 5) главный поток просто ждёт флаг остановки
    while (!stop) std::this_thread::sleep_for(50ms);

    // 6) корректная остановка
    mon.stop();
    sched.cancel(idFast);
    sched.cancel(idSlow);
    sched.stop();

    if (sigthr.joinable()) sigthr.join();
    std::cout << "[Main] Done.\n";
    return 0;
}
