#pragma once

#include "ADataGetter_Strategy.hpp"
#include "../Tools/SysCpu.hpp"

namespace dg {

class DG_SYS_CPU final : public ADataGetterStrategy<double>
{
public:

    std::string name() const override
    {
        return "DG_SYS_CPU";
    }

    void init(const Ctx&) override
    {
        prev_ = SysCPU::readTimes();
        initialized_ = true;
    }

    double getData() override
    {
        auto curr = SysCPU::readTimes();

        if (!initialized_) {
            prev_ = curr;
            initialized_ = true;
            return 0.0;
        }

        double usage = SysCPU::calcUsage(prev_, curr);

        prev_ = curr;

        return usage;
    }

private:

    SysCPU::CpuTimes prev_;
    bool initialized_ = false;
};

}