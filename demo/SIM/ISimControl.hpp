#pragma once

struct ISimControl {
    virtual ~ISimControl() = default;

    // включить/выключить “нагреватель” или режим
    virtual void setEnabled(bool on) = 0;

    // задать мощность (Вт) — опционально
    virtual void setPower(double watts) = 0;
};
