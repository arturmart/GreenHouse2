#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <cmath>

// --------------------- Типы ---------------------
enum class CellKind : uint8_t {
    Air,
    Wall,
    External,
    Heater,
    Sensor
};

struct Cell {
    CellKind kind = CellKind::Air;

    double T  = 25.0;   // °C — текущая температура
    double C  = 1000.0; // Дж/°C — тепловая "масса" (инерция)
    double kN = 5.0;    // Дж/(с*°C) — проводимость к соседям (условная)

    // Для внешних ячеек:
    double h_ext    = 1.0;  // Дж/(с*°C) — конвективные потери наружу
    double T_amb    = 0.0;  // локальная внешняя температура
    bool   T_amb_set = false; // true => использовать локальную T_amb

    // Для печи:
    bool   heater_on        = false;
    double heater_power_W   = 0.0;   // Дж/с (Вт) — текущая подводимая мощность
    double heater_power_W_max = 0.0; // максимум

    std::string name; // опциональная метка
};

// --------------------- Сетка ---------------------
class HeatGrid {
public:
    HeatGrid(int rows, int cols)
        : rows_(rows), cols_(cols), cells_(rows*cols) {}

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    bool inBounds(int r, int c) const { return r>=0 && r<rows_ && c>=0 && c<cols_; }


    const Cell& at(int r, int c) const { return cells_[r*cols_ + c]; }
    Cell& at(int r, int c) { return cells_[r*cols_ + c]; }

    void   setAmbient(double t) { ambient_ = t; }
    double ambient() const      { return ambient_; }

    // ---------- Шаг симуляции ----------
    // Явная схема Эйлера:
    // dT/dt = (Σ k_eff*(T_nb - T_i) + h_ext*(T_amb - T_i) + Q_heater) / C_i
    // Уменьши массу (инерцию) — делай < 1.0 для ускорения
    
    void scaleAllMass(double factor) {
        for (auto& c : cells_) c.C *= factor;
    }

    // Увеличь проводимость — делай > 1.0 для ускорения
    void scaleAllConductivity(double factor) {
        for (auto& c : cells_) c.kN *= factor;
    }

    // Усиль внешние потери — делай > 1.0 для ускорения остывания
    void scaleAllExternalLoss(double factor) {
        for (auto& c : cells_) c.h_ext *= factor;
    }

    // Оценка устойчивого шага интегрирования для явного Эйлера
    double estimateStableDt() const {
        // очень грубая оценка: dt < C / (4*kN + h_ext)
        double dt_min = std::numeric_limits<double>::infinity();
        for (const auto& c : cells_) {
            double denom = 4.0 * std::max(1e-12, c.kN) + std::max(0.0, c.h_ext);
            double dt_i  = std::max(1e-12, c.C) / denom;
            dt_min = std::min(dt_min, dt_i);
        }
        return 0.5 * dt_min; // с запасом
    }
    void step(double dt_sec) {
        tempBuf_.resize(cells_.size());
        auto idx = [&](int r,int c){ return r*cols_ + c; };

        for (int r=0; r<rows_; ++r) {
            for (int c=0; c<cols_; ++c) {
                const int  i    = idx(r,c);
                const Cell& cell = cells_[i];

                const double Ti  = cell.T;
                double heat_flow  = 0.0;

                auto add_neighbor = [&](int rr, int cc){
                    if (!inBounds(rr,cc)) return;
                    const Cell& nb = at(rr,cc);
                    const double k_eff = std::min(cell.kN, nb.kN);
                    heat_flow += k_eff * (nb.T - Ti);
                };
                add_neighbor(r-1,c);
                add_neighbor(r+1,c);
                add_neighbor(r,c-1);
                add_neighbor(r,c+1);

                double ext_term = 0.0;
                if (cell.kind == CellKind::External) {
                    const double Tamb = cell.T_amb_set ? cell.T_amb : ambient_;
                    ext_term = cell.h_ext * (Tamb - Ti);
                }

                double Q = 0.0;
                if (cell.kind == CellKind::Heater && cell.heater_on) {
                    Q = std::clamp(cell.heater_power_W, 0.0, cell.heater_power_W_max);
                }

                double dTdt = 0.0;
                if (cell.C > 1e-12) {
                    dTdt = (heat_flow + ext_term + Q) / cell.C;
                }
                tempBuf_[i] = Ti + dTdt * dt_sec;
            }
        }

        for (size_t i=0; i<cells_.size(); ++i) {
            cells_[i].T = tempBuf_[i];
        }
    }

    // ---------- Инициализация ячеек ----------
    void makeAir(int r, int c, double C=1000.0, double k=5.0) {
        auto& x = at(r,c);
        x.kind = CellKind::Air;
        x.C = C; x.kN = k;
        x.name = "Air";
    }

    void makeWall(int r, int c, double C=2000.0, double k=0.2) {
        auto& x = at(r,c);
        x.kind = CellKind::Wall;
        x.C = C; x.kN = k;
        x.name = "Wall";
    }

    void makeExternal(int r, int c, double C=1100.0, double k=3.0, double h_ext=3.0,
                      std::optional<double> T_amb = std::nullopt) {
        auto& x = at(r,c);
        x.kind = CellKind::External;
        x.C = C; x.kN = k; x.h_ext = h_ext;
        if (T_amb.has_value()) { x.T_amb = *T_amb; x.T_amb_set = true; }
        x.name = "External";
    }

    void makeHeater(int r, int c, double C=1500.0, double k=4.0, double Pmax=1200.0) {
        auto& x = at(r,c);
        x.kind = CellKind::Heater;
        x.C = C; x.kN = k;
        x.heater_on = false;
        x.heater_power_W = 0.0;
        x.heater_power_W_max = Pmax;
        x.name = "Heater";
    }

    void makeSensor(int r, int c, double C=800.0, double k=4.5) {
        auto& x = at(r,c);
        x.kind = CellKind::Sensor;
        x.C = C; x.kN = k;
        x.name = "Sensor";
    }

    // Точечная настройка параметров
    void setCellTemp(int r,int c,double T){ at(r,c).T  = T; }
    void setCellMass(int r,int c,double C){ at(r,c).C  = C; }
    void setCellCond(int r,int c,double k){ at(r,c).kN = k; }

    // ---------- API датчиков / печей ----------
    int registerSensor(int r, int c, const std::string& name="") {
        const int id = nextSensorId_++;
        sensors_[id] = {r,c};
        if (!name.empty()) at(r,c).name = name;
        return id;
    }

    int registerHeater(int r, int c, const std::string& name="") {
        const int id = nextHeaterId_++;
        heaters_[id] = {r,c};
        if (!name.empty()) at(r,c).name = name;
        return id;
    }

    double readSensor(int sensorId) const {
        auto it = sensors_.find(sensorId);
        if (it == sensors_.end()) return std::nan("");
        const auto& rc = it->second;
        return at(rc.r, rc.c).T;
    }

    void heaterSetPower(int heaterId, double power_W) {
        auto it = heaters_.find(heaterId);
        if (it == heaters_.end()) return;
        auto& x = at(it->second.r, it->second.c);
        if (x.kind != CellKind::Heater) return;
        x.heater_on = power_W > 1e-9;
        x.heater_power_W = std::clamp(power_W, 0.0, x.heater_power_W_max);
    }

    void heaterOn (int heaterId) { heaterToggle(heaterId, true);  }
    void heaterOff(int heaterId) { heaterToggle(heaterId, false); }

private:
    struct RC { int r; int c; };

    void heaterToggle(int heaterId, bool on) {
        auto it = heaters_.find(heaterId);
        if (it == heaters_.end()) return;
        auto& x = at(it->second.r, it->second.c);
        if (x.kind != CellKind::Heater) return;
        x.heater_on = on;
        if (!on) {
            x.heater_power_W = 0.0;
        } else if (x.heater_power_W <= 0.0) {
            x.heater_power_W = std::min(100.0, x.heater_power_W_max); // дефолт при включении
        }
    }

private:
    int rows_, cols_;
    std::vector<Cell>  cells_;
    std::vector<double> tempBuf_;
    double ambient_ = 0.0;

    int nextSensorId_ = 1;
    int nextHeaterId_ = 1;
    std::unordered_map<int, RC> sensors_;
    std::unordered_map<int, RC> heaters_;
};
