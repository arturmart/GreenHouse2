// main_SIM.cpp
#include "SIM.hpp"
#include "SIM_monitor.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>

int main() {
    using namespace std::chrono_literals;

    // === Сетка и базовые параметры ===
    HeatGrid grid(12, 12);
    grid.setAmbient(-5.0);                  // улица

    // Все ячейки — воздух (старт)
    for (int r = 0; r < grid.rows(); ++r)
        for (int c = 0; c < grid.cols(); ++c)
            grid.makeAir(r, c, /*C*/900.0, /*k*/5.0);

    // === Наружные ячейки по периметру ===
    for (int c = 0; c < grid.cols(); ++c) {
        grid.makeExternal(0, c);                        // верх
        grid.makeExternal(grid.rows()-1, c);            // низ
    }
    for (int r = 0; r < grid.rows(); ++r) {
        grid.makeExternal(r, 0);                        // лево
        grid.makeExternal(r, grid.cols()-1);            // право
    }

    // === Стены теплицы (внутренний контур) ===
    for (int c = 1; c < grid.cols()-1; ++c) {
        grid.makeWall(1, c);                            // верхняя стена
        grid.makeWall(grid.rows()-2, c);                // нижняя стена
    }
    for (int r = 1; r < grid.rows()-1; ++r) {
        grid.makeWall(r, 1);                            // левая
        grid.makeWall(r, grid.cols()-2);                // правая
    }

    // === Печь и датчик ===
    const int rH = grid.rows()/2;
    const int cH = grid.cols()/2;

    grid.makeHeater(rH, cH, /*C*/1500.0, /*k*/4.0, /*Pmax*/1500.0);
    int hid = grid.registerHeater(rH, cH, "Heater1");
    grid.heaterSetPower(hid, 1200.0);                   // мощнее, для быстрого прогрева

    grid.makeSensor(rH, cH + 3);
    int sid = grid.registerSensor(rH, cH + 3, "Sensor1");

    // === Ускоряем динамику (меньше инерция, быстрее теплообмен) ===
    for (int r = 0; r < grid.rows(); ++r) {
        for (int c = 0; c < grid.cols(); ++c) {
            auto& cell = grid.at(r, c);
            // воздух и датчики — реагируют быстрее
            if (cell.kind == CellKind::Air || cell.kind == CellKind::Sensor) {
                cell.C  *= 0.5;   // в 2 раза меньше тепловая масса
                cell.kN *= 2.0;   // в 2 раза выше проводимость к соседям
            }
            // внешние — быстрее «тянут» температуру к улице (быстрее остывание)
            if (cell.kind == CellKind::External) {
                cell.h_ext *= 1.8;   // усилить внешние потери
                cell.kN    *= 1.5;   // немного поднять проводимость границы
            }
            // стены оставим более «тяжёлыми», чтобы фильтровали
            if (cell.kind == CellKind::Wall) {
                cell.C  *= 1.3;      // чуть тяжелее
                cell.kN *= 0.7;      // хуже проводят
            }
            // печь: чуть быстрее отдаёт тепло в соседей
            if (cell.kind == CellKind::Heater) {
                cell.kN *= 1.5;
            }
        }
    }

    // === Шаг интегрирования и цикл ===
    const double dt = 0.1;   // при сильном увеличении kN можно снизить до 0.05
    double sim_time = 0.0;

    simmon::ansi_clear();
    simmon::ansi_hidecur();

    for (;;) {
        grid.step(dt);
        sim_time += dt;

        // Компактная отрисовка: холодное — синий, горячее — красный
        simmon::render_matrix_compact(grid, /*Tmin*/-10.0, /*Tmax*/60.0,
                                      /*cellw*/4, /*legend*/false, /*clear*/true);

        std::cout << "t = " << std::fixed << std::setprecision(1) << sim_time
                  << " s | Sensor: " << grid.readSensor(sid) << " °C"
                  << " | Heater: 1200 W\n";

        std::this_thread::sleep_for(120ms); // обновление побыстрее
    }

    simmon::ansi_showcur();
    return 0;
}
