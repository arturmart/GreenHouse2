// SIM/HeatSimAdapter.hpp
#pragma once
#include "SIM/ISimControl.hpp"
#include <atomic>
#include <mutex>

class HeatSimAdapter : public ISimControl {
public:
    explicit HeatSimAdapter(double default_power = 150.0)
        : enabled_(false), power_(default_power) {}

    // ISimControl
    void setEnabled(bool v) override {
        std::lock_guard<std::mutex> lk(m_);
        enabled_ = v;
    }
    void setPower(double w) override {
        std::lock_guard<std::mutex> lk(m_);
        power_ = w;
    }

    // Дадим и геттеры — удобно для «инъекции тепла» в основном цикле
    bool isEnabled() const {
        std::lock_guard<std::mutex> lk(m_);
        return enabled_;
    }
    double getPower() const {
        std::lock_guard<std::mutex> lk(m_);
        return power_;
    }

private:
    mutable std::mutex m_;
    bool   enabled_;
    double power_;
};
