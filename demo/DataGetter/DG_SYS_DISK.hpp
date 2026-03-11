#pragma once

#include <string>
#include <stdexcept>

#include "ADataGetter_Strategy.hpp"
#include "../Tools/SysDisk.hpp"

namespace dg {

class DG_SYS_DISK final : public ADataGetterStrategy<double>
{
public:
    enum class Field
    {
        TOTAL,
        FREE,
        AVAILABLE
    };

    DG_SYS_DISK(Field field, std::string path = "/")
        : field_(field)
        , path_(std::move(path))
    {}

    std::string name() const override
    {
        return "DG_SYS_DISK(" + path_ + ")";
    }

    void init(const Ctx&) override
    {
        // ничего не нужно
    }

    double getData() override
    {
        const SysDiskInfo d = SysDisk::read(path_);

        switch (field_)
        {
            case Field::TOTAL:
                this->sensorValue_ = static_cast<double>(d.totalKB);
                return this->sensorValue_;

            case Field::FREE:
                this->sensorValue_ = static_cast<double>(d.freeKB);
                return this->sensorValue_;

            case Field::AVAILABLE:
                this->sensorValue_ = static_cast<double>(d.availableKB);
                return this->sensorValue_;
        }

        throw std::runtime_error("DG_SYS_DISK: invalid field");
    }

private:
    Field field_;
    std::string path_;
};

} // namespace dg