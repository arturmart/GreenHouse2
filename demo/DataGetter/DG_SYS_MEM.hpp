#pragma once
#include <string>
#include <stdexcept>

#include "ADataGetter_Strategy.hpp"
#include "../Tools/SysMem.hpp"

namespace dg {

class DG_SYS_MEM final : public ADataGetterStrategy<double>
{
public:

    enum class Field
    {
        MEM_TOTAL,
        MEM_FREE,
        MEM_AVAILABLE,
        MEM_PROCESS
    };

    DG_SYS_MEM(Field field)
        : field_(field)
    {}

    std::string name() const override
    {
        return "DG_SYS_MEM";
    }

    void init(const Ctx&) override
    {
        // ничего не нужно
    }

    double getData() override
    {
        SysMemInfo mem = SysMem::read();

        switch(field_)
        {
            case Field::MEM_TOTAL:
                return static_cast<double>(mem.memTotalKB);

            case Field::MEM_FREE:
                return static_cast<double>(mem.memFreeKB);

            case Field::MEM_AVAILABLE:
                return static_cast<double>(mem.memAvailableKB);
            case Field::MEM_PROCESS:
            return static_cast<double>(SysMem::readProcessRamKB());
        }

        throw std::runtime_error("DG_SYS_MEM: invalid field");
    }

private:

    Field field_;
};

}