#pragma once

#include <string>

#include "ADataGetter_Strategy.hpp"
#include "../Tools/DateTime.hpp"

namespace dg {

class DG_TIME final : public ADataGetterStrategy<tools::UnixMs> {
public:
    DG_TIME() = default;

    std::string name() const override {
        return "DG_TIME";
    }

    void init(const Ctx& /*ctx*/) override {}

    tools::UnixMs getData() override {
        const long long now = tools::nowUnixMs();

        this->sensorValue_ = tools::UnixMs(now);
        return this->sensorValue_;
    }
};

} // namespace dg