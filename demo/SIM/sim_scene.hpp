#pragma once
#include "SIM/HeatGrid.hpp"
#include "SIM/SIM_monitor.hpp"
#include <vector>
#include <utility>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iostream>

struct SceneConfig {
    int rows{16}, cols{16};

    // визуал
    int  refresh_ms{80};
    int  cellw{4};
    double Tmin{0.0}, Tmax{80.0};

    // физика
    int    substeps{6};
    double dt_base{0.1};
    double speed_mult{3.0};

    // перегородка
    int part_col{-1};      // -1 = нет, иначе колонка
    int part_r0{0}, part_r1{-1};

    // сенсор
    int sensor_r{-1}, sensor_c{-1};

    // печи
    std::vector<std::pair<int,int>> heaters; // список клеток печи
};

enum class CoolingProfile { None, External5C, Wall15C, Partition15C };

struct SceneState {
    // предвычисленные наборы ячеек
    std::vector<std::pair<int,int>> contour_external;
    std::vector<std::pair<int,int>> contour_wall;
    std::vector<std::pair<int,int>> partition;
    // сенсор
    int sensor_r{-1}, sensor_c{-1};
    // печи
    std::vector<std::pair<int,int>> heaters;

    // кэш dt для подшага
    double dt_phys{0.1};

    // cooling переключатели
    bool cool_external{true};
    bool cool_wall{true};
    bool cool_partition{true};
};

inline std::vector<std::pair<int,int>> rect_frame(int r0,int c0,int r1,int c1){
    std::vector<std::pair<int,int>> v;
    if (r0>r1 || c0>c1) return v;
    v.reserve(2*((r1-r0+1)+(c1-c0+1)));
    for (int c=c0;c<=c1;++c){ v.emplace_back(r0,c); v.emplace_back(r1,c); }
    for (int r=r0;r<=r1;++r){ v.emplace_back(r,c0); v.emplace_back(r,c1); }
    return v;
}

inline void set_kind(HeatGrid& g, const std::vector<std::pair<int,int>>& cells, CellKind k){
    for (auto [r,c]: cells){
        if (r<0||c<0||r>=g.rows()||c>=g.cols()) continue;
        g.at(r,c).kind = k;
    }
}

inline void relax_to(HeatGrid& g, const std::vector<std::pair<int,int>>& cells, double Ttgt, double a){
    for (auto [r,c]: cells){
        if (r<0||c<0||r>=g.rows()||c>=g.cols()) continue;
        auto& x = g.at(r,c);
        x.T = (1.0-a)*x.T + a*Ttgt;
    }
}

inline void inject_heat(HeatGrid& g, const std::vector<std::pair<int,int>>& hs, double dT){
    for (auto [r,c]: hs){
        if (r<0||c<0||r>=g.rows()||c>=g.cols()) continue;
        auto& x = g.at(r,c);
        x.T += dT;
        x.kind = CellKind::Heater;
    }
}

inline void place_sensor(HeatGrid& g, int r,int c){
    if (r<0||c<0||r>=g.rows()||c>=g.cols()) return;
    g.at(r,c).kind = CellKind::Sensor;
}

inline SceneState build_scene(HeatGrid& grid, const SceneConfig& cfg){
    SceneState st{};
    // внешний контур External
    st.contour_external = rect_frame(0,0,cfg.rows-1,cfg.cols-1);
    set_kind(grid, st.contour_external, CellKind::External);
    // внутренний контур Wall (на 1 клетку внутрь)
    if (cfg.rows>=3 && cfg.cols>=3){
        st.contour_wall = rect_frame(1,1,cfg.rows-2,cfg.cols-2);
        set_kind(grid, st.contour_wall, CellKind::Wall);
    }
    // перегородка (опц.)
    if (cfg.part_col>=0){
        int r0 = std::max(0, cfg.part_r0);
        int r1 = (cfg.part_r1<0)? cfg.rows-1 : std::min(cfg.rows-1, cfg.part_r1);
        for (int r=r0;r<=r1;++r) st.partition.emplace_back(r, cfg.part_col);
        set_kind(grid, st.partition, CellKind::Wall);
    }
    // сенсор
    st.sensor_r = cfg.sensor_r;
    st.sensor_c = cfg.sensor_c;
    place_sensor(grid, cfg.sensor_r, cfg.sensor_c);

    // печи
    st.heaters = cfg.heaters;

    // dt
    st.dt_phys = cfg.dt_base * cfg.speed_mult;

    return st;
}

inline void sim_substep(HeatGrid& grid, SceneState& st,
                        bool heater_on, double heater_power)
{
    if (heater_on){
        const double dT = 0.04 * heater_power * st.dt_phys / 100.0;
        inject_heat(grid, st.heaters, dT);
    }
    // простое охлаждение к целям (если твоя физика не делает сама)
    if (st.cool_external) relax_to(grid, st.contour_external, 5.0,  0.05);
    if (st.cool_wall)     relax_to(grid, st.contour_wall,     15.0, 0.03);
    if (st.cool_partition)relax_to(grid, st.partition,        15.0, 0.03);

    grid.step(st.dt_phys);
}

inline void draw_hud_at(const HeatGrid& grid, const SceneConfig& cfg, const SceneState& st,
                        bool heater_on, double heater_power, int row_off)
{
    simmon::ansi_goto(row_off, 1);
    double Ts = (st.sensor_r>=0 && st.sensor_c>=0) ? grid.at(st.sensor_r, st.sensor_c).T : NAN;
    std::cout << "[HUD] Sensor(" << st.sensor_r << "," << st.sensor_c << ") T="
              << (std::isnan(Ts)? -999 : (int)std::lround(Ts)) << "C  |  "
              << "Heater: " << (heater_on? "ON " : "OFF")
              << " P=" << (int)std::lround(heater_power)
              << "  sub=" << cfg.substeps
              << "  x" << cfg.speed_mult
              << "      \n";
}

inline void draw_grid_at(const HeatGrid& grid, const SceneConfig& cfg, int row_off)
{
    for (int r = 0; r < grid.rows(); ++r) {
        simmon::ansi_goto(row_off + 1 + r, 1);  // +1: строка HUD
        for (int c = 0; c < grid.cols(); ++c) {
            const auto& cell = grid.at(r,c);
            simmon::print_cell(cell.kind, cell.T, cfg.Tmin, cfg.Tmax, cfg.cellw);
            std::cout << ' ';
        }
        std::cout << "   \n"; // затираем хвост
    }
    std::cout.flush();
}
