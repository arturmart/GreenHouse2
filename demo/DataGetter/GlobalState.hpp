#pragma once
#include <mutex>

template<typename T>
struct Field {
    T value{};
    bool valid = false;

    void set(const T& v) {
        value = v;
        valid = true;
    }
};

struct GlobalState {
    static GlobalState& instance() {
        static GlobalState inst;
        return inst;
    }

    struct {
        Field<float> randomNumber;
    } dataGetter;

private:
    GlobalState() = default;
};
