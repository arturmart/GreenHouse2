#pragma once
#include "ADataGetter_Strategy.hpp"
#include <random>

class RandomFloatStrategy : public dg::ADataGetterStrategy<float> {
public:
    RandomFloatStrategy()
        : dist_(0.0f, 100.0f) {}

    float getData() override {
        sensorValue_ = dist_(rng_);
        return sensorValue_;
    }

    std::string name() const override {
        return "RandomFloatStrategy";
    }

private:
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> dist_;
};
