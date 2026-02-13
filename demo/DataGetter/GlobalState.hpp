#pragma once
#include <atomic>
#include <mutex>

template<typename T>
struct Field {
public:
    void set(const T& v) {
        std::lock_guard<std::mutex> lock(mtx_);
        value_ = v;
        valid_ = true;
    }

    T get() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return value_;
    }

    bool isValid() const {
        return valid_;
    }

private:
    mutable std::mutex mtx_;
    T value_{};
    std::atomic<bool> valid_{false};
};
