#pragma once
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <string>

// Совместим с SIM.hpp:
// enum class CellKind { Air, Wall, External, Heater, Sensor };
// class HeatGrid { int rows() const; int cols() const; const Cell& at(int,int) const; }
// struct Cell { CellKind kind; double T; }

// ===== TrueColor toggle =====
#ifndef SIM_TRUECOLOR
#define SIM_TRUECOLOR 1   // 1 = 24-bit TrueColor; 0 = xterm-256 fallback
#endif

namespace simmon {

// ===== ANSI (адресные, без глобальных очисток) =====
inline void ansi_hidecur() { std::cout << "\x1b[?25l"; }
inline void ansi_showcur() { std::cout << "\x1b[?25h"; }
inline void ansi_reset()   { std::cout << "\x1b[0m";  }
inline void ansi_goto(int row, int col){ std::cout << "\x1b["<<row<<';'<<col<<'H'; }

// Локальная очистка прямоугольника (строки row0..row1 включительно), cols — ширина
inline void clear_box(int row0, int row1, int cols = 200) {
    for (int r = row0; r <= row1; ++r) {
        ansi_goto(r, 1);
        for (int c = 0; c < cols; ++c) std::cout << ' ';
    }
}

// (оставлены для обратной совместимости — НЕ используйте их в совместном рендере)
inline void ansi_clear_legacy() { std::cout << "\x1b[2J"; }
inline void ansi_home_legacy()  { std::cout << "\x1b[H";  }

// ===== Цвет =====
inline void ansi_bg_true(int R,int G,int B){ std::cout << "\x1b[48;2;"<<R<<';'<<G<<';'<<B<<'m'; }
inline void ansi_fg_true(int R,int G,int B){ std::cout << "\x1b[38;2;"<<R<<';'<<G<<';'<<B<<'m'; }
inline void ansi_bg256(int code){ std::cout << "\x1b[48;5;"<<code<<"m"; }
inline void ansi_fg256(int code){ std::cout << "\x1b[38;5;"<<code<<"m"; }

inline char type_char(CellKind k) {
    switch (k) {
        case CellKind::Air:      return 'A';
        case CellKind::Wall:     return 'W';
        case CellKind::External: return 'E';
        case CellKind::Heater:   return 'H';
        case CellKind::Sensor:   return 'S';
        default: return '?';
    }
}

// RGB->xterm256 куб 6×6×6 (fallback)
inline int rgb_to_xterm256(int R,int G,int B){
    auto q = [](int v)->int{
        static const int lvl[6] = {0,95,135,175,215,255};
        int best=0, dmin=1e9;
        for (int i=0;i<6;i++){ int d = std::abs(lvl[i]-v); if (d<dmin){ dmin=d; best=i; } }
        return best; // 0..5
    };
    int r=q(R), g=q(G), b=q(B);
    return 16 + 36*r + 6*g + b;
}

// Температура → цвет (синий→красный), без зелёного
inline void temp_to_rgb(double T, double Tmin, double Tmax, int& R, int& G, int& B){
    double x = (T - Tmin) / std::max(1e-9, (Tmax - Tmin));
    x = std::clamp(x, 0.0, 1.0);

    constexpr double gammaR = 0.85;
    constexpr double gammaB = 0.85;

    double r = std::pow(x,          gammaR);
    double b = std::pow(1.0 - x,    gammaB);

    R = (int)std::lround(255.0 * r);
    G = 0;
    B = (int)std::lround(255.0 * b);

    R = std::clamp(R, 0, 255);
    B = std::clamp(B, 0, 255);
}

// Контрастный цвет текста под фоном (TrueColor)
inline void pick_fg_for_bg(int R,int G,int B,int& r,int& g,int& b){
    auto lin=[](double c){ c/=255.0; return (c<=0.03928)?(c/12.92):std::pow((c+0.055)/1.055,2.4); };
    double L = 0.2126*lin(R)+0.7152*lin(G)+0.0722*lin(B);
    if (L > 0.45) { r=0; g=0; b=0; } else { r=255; g=255; b=255; }
}

// Печать одной ячейки (тип + целая температура), ширина cellw
inline void print_cell(CellKind kind, double T, double Tmin, double Tmax, int cellw=4) {
    char tt = type_char(kind);
    int Ti = (int)std::lround(T);

    std::ostringstream os;
    os << tt << Ti; // "A23", "H18", ...
    std::string s = os.str();
    if ((int)s.size() > cellw) s.resize(cellw);
    if ((int)s.size() < cellw) s.append(cellw - (int)s.size(), ' ');

    int R,G,B; temp_to_rgb(T, Tmin, Tmax, R,G,B);

#if SIM_TRUECOLOR
    int fr,fg,fb; pick_fg_for_bg(R,G,B, fr,fg,fb);
    ansi_bg_true(R,G,B);
    ansi_fg_true(fr,fg,fb);
    std::cout << s;
    ansi_reset();
#else
    int code = rgb_to_xterm256(R,G,B);
    int idx = code - 16; int rr = (idx>=0)? idx/36 : 0;
    ansi_bg256(code);
    ansi_fg256( (rr>=4) ? 16 : 15 ); // грубая контрастность текста
    std::cout << s;
    ansi_reset();
#endif
}

// ====== РЕНДЕР (НОВЫЙ API С ОФФСЕТОМ) ======================================

// Рисует матрицу в окне начиная с (row_off, col_off).
// wipe_width > 0 — стирать строку до фиксированной ширины (полезно, чтобы «затирать хвост»).
template <class HeatGridT>
void render_matrix_compact_at(const HeatGridT& grid,
                              int row_off, int col_off,
                              double Tmin = 0.0, double Tmax = 40.0,
                              int cellw = 4,
                              bool legend = false,
                              int wipe_width = 0)
{
    for (int r = 0; r < grid.rows(); ++r) {
        ansi_goto(row_off + r, col_off);
        int printed = 0;
        for (int c = 0; c < grid.cols(); ++c) {
            const auto& cell = grid.at(r,c);
            print_cell(cell.kind, cell.T, Tmin, Tmax, cellw);
            std::cout << ' ';
            printed += cellw + 1;
        }
        if (wipe_width > printed) {
            for (int k = 0; k < wipe_width - printed; ++k) std::cout << ' ';
        }
        std::cout << "\n";
    }
    if (legend) {
        ansi_goto(row_off + grid.rows(), col_off);
        std::cout << "Legend: A=Air, W=Wall, E=External, H=Heater, S=Sensor | "
                  << "Color: cold(blue) → hot(red), TrueColor=" << (SIM_TRUECOLOR? "on":"off") << "\n";
    }
    std::cout.flush();
}

// Однострочный HUD поверх матрицы (в адресе row_off)
inline void render_hud_line_at(int row_off, int col_off,
                               const std::string& text,
                               int wipe_width = 0)
{
    ansi_goto(row_off, col_off);
    int printed = (int)text.size();
    std::cout << text;
    if (wipe_width > printed) {
        for (int k = 0; k < wipe_width - printed; ++k) std::cout << ' ';
    }
    std::cout << "\n";
    std::cout.flush();
}

// Быстрый хелпер: HUD + матрица в своём окне [row_hud, row_hud+rows]
template <class HeatGridT>
void render_hud_and_matrix_box(const HeatGridT& grid,
                               int row_hud,        // строка HUD
                               int col_off,        // отступ по колонке
                               double Tmin, double Tmax,
                               int cellw,
                               const std::string& hud_text,
                               int wipe_width_matrix = 0,
                               int wipe_width_hud    = 0)
{
    render_hud_line_at(row_hud, col_off, hud_text, wipe_width_hud);
    render_matrix_compact_at(grid, row_hud + 1, col_off, Tmin, Tmax, cellw, /*legend=*/false, wipe_width_matrix);
}

// ====== СТАРЫЙ API (ОСТАВЛЕН ДЛЯ СОВМЕСТИМОСТИ) ============================

// Старый рендер без оффсета. НЕ чистит экран, только печатает с текущей позиции.
// Чтобы получить старое поведение «с верха», перед вызовом сделай ansi_goto(1,1).
template <class HeatGridT>
void render_matrix_compact(const HeatGridT& grid,
                           double Tmin = 0.0, double Tmax = 40.0,
                           int cellw = 4, bool legend = false, bool clear = false)
{
    // legacy: если clear=true — лишь возвращаем курсор в 1,1 (без глобального clear)
    if (clear) ansi_goto(1, 1);
    for (int r = 0; r < grid.rows(); ++r) {
        for (int c = 0; c < grid.cols(); ++c) {
            const auto& cell = grid.at(r,c);
            print_cell(cell.kind, cell.T, Tmin, Tmax, cellw);
            std::cout << ' ';
        }
        std::cout << "\n";
    }
    if (legend) {
        std::cout << "Legend: A=Air, W=Wall, E=External, H=Heater, S=Sensor | "
                  << "Color: cold(blue) → hot(red), TrueColor=" << (SIM_TRUECOLOR? "on":"off") << "\n";
    }
    std::cout.flush();
}

// Старый «живой» цикл. Сохранён для совместимости, но для двух мониторов лучше
// использовать свой планировщик задач и новые функции с оффсетом.
template <class HeatGridT, class StepFn>
void run_live_legacy(HeatGridT& grid, StepFn step_fn,
                     int refresh_ms = 120, double Tmin = -10.0, double Tmax = 60.0, int cellw = 4)
{
    ansi_hidecur();
    ansi_clear_legacy(); // глобальный clear — legacy
    while (true) {
        step_fn(); // например: grid.step(0.1);
        render_matrix_compact(grid, Tmin, Tmax, cellw, /*legend*/false, /*clear*/true);
        std::this_thread::sleep_for(std::chrono::milliseconds(refresh_ms));
    }
}

// Современная версия «живого» цикла в своём окне (кооперативная)
template <class HeatGridT, class StepFn>
void run_live_in_box(HeatGridT& grid, StepFn step_fn,
                     int row_hud, int col_off,
                     int refresh_ms = 120,
                     double Tmin = -10.0, double Tmax = 60.0, int cellw = 4,
                     int wipe_width_matrix = 0, int wipe_width_hud = 0)
{
    // Никаких глобальных clear/home.
    while (true) {
        step_fn();
        // hud_text должен генериться снаружи; сюда можно подать пустую строку
        render_hud_line_at(row_hud, col_off, "", wipe_width_hud);
        render_matrix_compact_at(grid, row_hud + 1, col_off, Tmin, Tmax, cellw, /*legend=*/false, wipe_width_matrix);
        std::this_thread::sleep_for(std::chrono::milliseconds(refresh_ms));
    }
}

} // namespace simmon
