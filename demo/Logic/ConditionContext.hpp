#pragma once

#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace logic {

// ------------------------------------------------------------
// Base typed condition strategy
// ------------------------------------------------------------
template<typename T>
class IConditionStrategy {
public:
    virtual ~IConditionStrategy() = default;
    virtual bool evaluate(const std::vector<T>& args) const = 0;
};

// ------------------------------------------------------------
// Generic strategies
// ------------------------------------------------------------
template<typename T>
class CondGreater final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 2 && args[0] > args[1];
    }
};

template<typename T>
class CondLess final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 2 && args[0] < args[1];
    }
};

template<typename T>
class CondEqual final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 2 && args[0] == args[1];
    }
};

template<typename T>
class CondNotEqual final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 2 && args[0] != args[1];
    }
};

template<typename T>
class CondGreaterEqual final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 2 && args[0] >= args[1];
    }
};

template<typename T>
class CondLessEqual final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 2 && args[0] <= args[1];
    }
};

template<typename T>
class CondInRange final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 3 && args[0] >= args[1] && args[0] <= args[2];
    }
};

template<typename T>
class CondOutOfRange final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        return args.size() == 3 && (args[0] < args[1] || args[0] > args[2]);
    }
};

template<typename T>
class CondAlways final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>&) const override {
        return true;
    }
};

template<typename T>
class CondNever final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>&) const override {
        return false;
    }
};

template<typename T>
class CondModPart final : public IConditionStrategy<T> {
public:
    bool evaluate(const std::vector<T>& args) const override {
        // args:
        // [0] = data
        // [1] = part size
        // [2] = part count
        // [3] = which part index
        return args.size() == 4 &&
               (((args[0] % (args[1] * args[2])) / args[1]) == args[3]);
    }
};

// ------------------------------------------------------------
// ConditionContext
// ------------------------------------------------------------
class ConditionContext {
public:
    ConditionContext() {
        // double
        addStrategy<double>("gt", std::make_unique<CondGreater<double>>());
        addStrategy<double>("lt", std::make_unique<CondLess<double>>());
        addStrategy<double>("eq", std::make_unique<CondEqual<double>>());
        addStrategy<double>("neq", std::make_unique<CondNotEqual<double>>());
        addStrategy<double>("gte", std::make_unique<CondGreaterEqual<double>>());
        addStrategy<double>("lte", std::make_unique<CondLessEqual<double>>());
        addStrategy<double>("in_range", std::make_unique<CondInRange<double>>());
        addStrategy<double>("out_of_range", std::make_unique<CondOutOfRange<double>>());
        addStrategy<double>("always", std::make_unique<CondAlways<double>>());
        addStrategy<double>("never", std::make_unique<CondNever<double>>());

        // int64
        addStrategy<long long>("gt_i64", std::make_unique<CondGreater<long long>>());
        addStrategy<long long>("lt_i64", std::make_unique<CondLess<long long>>());
        addStrategy<long long>("eq_i64", std::make_unique<CondEqual<long long>>());
        addStrategy<long long>("neq_i64", std::make_unique<CondNotEqual<long long>>());
        addStrategy<long long>("gte_i64", std::make_unique<CondGreaterEqual<long long>>());
        addStrategy<long long>("lte_i64", std::make_unique<CondLessEqual<long long>>());
        addStrategy<long long>("in_range_i64", std::make_unique<CondInRange<long long>>());
        addStrategy<long long>("out_of_range_i64", std::make_unique<CondOutOfRange<long long>>());
        addStrategy<long long>("mod_part", std::make_unique<CondModPart<long long>>());
        addStrategy<long long>("always_i64", std::make_unique<CondAlways<long long>>());
        addStrategy<long long>("never_i64", std::make_unique<CondNever<long long>>());

        // bool
        addStrategy<bool>("is_true", std::make_unique<CondEqual<bool>>());
        addStrategy<bool>("is_false", std::make_unique<CondEqual<bool>>());
        addStrategy<bool>("always_bool", std::make_unique<CondAlways<bool>>());
        addStrategy<bool>("never_bool", std::make_unique<CondNever<bool>>());
    }

    bool check(const std::string& key, const std::vector<std::string>& args) const {
        if (boolStrategies().count(key)) {
            return checkTyped<bool>(key, args);
        }

        if (i64Strategies().count(key)) {
            return checkTyped<long long>(key, args);
        }

        if (doubleStrategies().count(key)) {
            return checkTyped<double>(key, args);
        }

        std::cerr << "[LOGIC] Condition not found: " << key << "\n";
        return false;
    }

    std::vector<std::string> listAll() const {
        std::vector<std::string> out;

        for (const auto& kv : doubleStrategies()) out.push_back(kv.first);
        for (const auto& kv : i64Strategies()) out.push_back(kv.first);
        for (const auto& kv : boolStrategies()) out.push_back(kv.first);

        return out;
    }

private:
    template<typename T>
    using StrategyMap = std::unordered_map<std::string, std::unique_ptr<IConditionStrategy<T>>>;

    template<typename T>
    static StrategyMap<T>& strategies() {
        static StrategyMap<T> mp;
        return mp;
    }

    static StrategyMap<double>& doubleStrategies() {
        return strategies<double>();
    }

    static StrategyMap<long long>& i64Strategies() {
        return strategies<long long>();
    }

    static StrategyMap<bool>& boolStrategies() {
        return strategies<bool>();
    }

    template<typename T>
    void addStrategy(const std::string& key, std::unique_ptr<IConditionStrategy<T>> strategy) {
        strategies<T>()[key] = std::move(strategy);
    }

    template<typename T>
    bool checkTyped(const std::string& key, const std::vector<std::string>& args) const {
        std::vector<T> converted;
        if (!convertArgs<T>(args, converted)) {
            std::cerr << "[LOGIC] Failed to convert args for condition: " << key << "\n";
            return false;
        }

        auto& mp = strategies<T>();
        auto it = mp.find(key);
        if (it == mp.end() || !it->second) {
            std::cerr << "[LOGIC] Strategy not found: " << key << "\n";
            return false;
        }

        // special handling for bool unary conditions
        if constexpr (std::is_same_v<T, bool>) {
            if (key == "is_true") {
                return converted.size() == 1 && converted[0] == true;
            }
            if (key == "is_false") {
                return converted.size() == 1 && converted[0] == false;
            }
        }

        return it->second->evaluate(converted);
    }

    template<typename T>
    static bool convertArgs(const std::vector<std::string>& args, std::vector<T>& out) {
        try {
            for (const auto& s : args) {
                std::istringstream iss(s);
                T v{};
                iss >> v;
                if (iss.fail()) {
                    return false;
                }
                out.push_back(v);
            }
            return true;
        } catch (...) {
            return false;
        }
    }
};

// ------------------------------------------------------------
// bool specialization for convertArgs
// ------------------------------------------------------------
template<>
inline bool ConditionContext::convertArgs<bool>(const std::vector<std::string>& args,
                                                std::vector<bool>& out) {
    try {
        for (const auto& s : args) {
            std::string v = s;
            for (auto& c : v) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            if (v == "true" || v == "1") {
                out.push_back(true);
            } else if (v == "false" || v == "0") {
                out.push_back(false);
            } else {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace logic