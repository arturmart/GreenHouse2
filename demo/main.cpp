#include "SIM/HeatGrid.hpp"
#include "SIM/HeatSimAdapter.hpp"
#include "SIM/sim_scene.hpp"
#include "SIM/SIM_monitor.hpp"            // для ansi_* (если нужно)

#include "Executor/Executor.hpp"
#include "Executor/Executor_Strategy_SIM.hpp"

#include "Scheduler/Scheduler.hpp"        // твой планировщик
#include "Scheduler/Scheduler_monitor.hpp"// TerminalMonitor (монитор шедулера)

#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <mutex>

using namespace std::chrono_literals;
using namespace exec;

static std::atomic<bool> g_stop{false};
static void install_signal_handlers(){
    std::signal(SIGINT,  [](int){ g_stop = true; });
    std::signal(SIGTERM, [](int){ g_stop = true; });
}

int main(){
    install_signal_handlers();

    // 1) Конфиг сцены
    SceneConfig cfg;
    cfg.rows = 16; cfg.cols = 16;
    cfg.refresh_ms = 80; cfg.cellw = 4; cfg.Tmin = 0.0; cfg.Tmax = 80.0;
    cfg.substeps = 6; cfg.dt_base = 0.1; cfg.speed_mult = 3.0;

    cfg.part_col = cfg.cols/3; cfg.part_r0 = 3; cfg.part_r1 = cfg.rows-4;
    cfg.sensor_r = cfg.rows/2; cfg.sensor_c = cfg.cols - cfg.cols/4;
    { int hr = cfg.rows/2, hc = cfg.cols/3 + 2;
      cfg.heaters = {{hr,hc},{hr,hc+1},{hr,hc-1}}; }

    // 2) Сетка + сцена
    HeatGrid grid(cfg.rows, cfg.cols);
    SceneState st = build_scene(grid, cfg);

    // 3) ISimControl + Executor
    HeatSimAdapter heater(250.0);
    Executor ex;
    ex.registerCommand("SIM_HEAT", std::make_unique<ExecutorStrategySIM>());
    AExecutorStrategy::Ctx ctx; ctx["sim"] = (ISimControl*)&heater; ex.initAll(ctx);

    // 4) Scheduler
    auto& sched = Scheduler::instance(4);     // 1 поток — меньше конфликтов вывода
    std::mutex sim_mtx;                       // пригодится, если увеличишь потоки

    
    constexpr int TOP_ROWS = 10;

    // Старый конструктор: 3 аргумента
    TerminalMonitor mon(200ms, /*historyCols=*/40, /*colWidth=*/3);
    // mon.setWindow(1, TOP_ROWS);  // <-- УДАЛИ/закомментируй ЭТУ СТРОКУ
    mon.start(sched);

    const int SIM_ROW_OFF = TOP_ROWS + 1; // можешь оставить как есть для SIM


    

    // 6) Периодические задачи

    // Toggle печи раз в 1500 мс
    auto idToggle = sched.addPeriodic([&]{
        static bool state = false;
        ex.enqueue("SIM_HEAT", 0, state);
        state = !state;
        std::this_thread::sleep_for(50ms); // For SIM
    }, 1500ms, "ToggleHeater");

    // Executor::tick
    auto idExec = sched.addPeriodic([&]{
        ex.tick();
        std::this_thread::sleep_for(50ms); // For SIM
    }, 100ms, "ExecutorTick");

    

    simmon::ansi_hidecur();
    simmon::clear_box(1, TOP_ROWS + 1 + cfg.rows + 2); // верх+нижняя область

   
    // Задача физики + рендер:
    auto idPhysRender = sched.addPeriodic([&]{
        std::this_thread::sleep_for(50ms); // For SIM
        for (int k=0; k<cfg.substeps; ++k){
            sim_substep(grid, st, heater.isEnabled(), heater.getPower());
        }
        draw_hud_at(grid, cfg, st, heater.isEnabled(), heater.getPower(), /*row_off=*/SIM_ROW_OFF);
        draw_grid_at(grid, cfg, /*row_off=*/SIM_ROW_OFF);
    }, std::chrono::milliseconds(cfg.refresh_ms), "PhysAndRender");

    // 7) Главный поток ждёт сигнал
    while (!g_stop.load()) std::this_thread::sleep_for(100ms);

    // 8) Завершение
    mon.stop();
    sched.cancel(idToggle);
    sched.cancel(idExec);
    sched.cancel(idPhysRender);
    sched.stop();

    simmon::ansi_showcur();
    simmon::ansi_reset();
    std::cout << "\n[SIM] stopped.\n";
    return 0;
}
