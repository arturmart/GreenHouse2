#pragma once
#include <unordered_map>
#include <memory>
#include <string>
#include <cstdint>
#include "ADataGetter_Strategy.hpp"

namespace dg {

class DataGetter {
public:
    using StrategyBase = ADataGetterStrategyBase;
    using StrategyUP   = std::unique_ptr<StrategyBase>;
    using Ctx          = StrategyBase::Ctx;

    // Регистрация уже созданной стратегии
    void add(const std::string& key, StrategyUP strat) {
        strategies_.emplace(key, std::move(strat));
    }

    // Удобный emplace, как у Executor: создаёт стратегию и возвращает ссылку
    template <class Strategy, class... Args>
    Strategy& emplace(const std::string& key, Args&&... args) {
        auto ptr = std::make_unique<Strategy>(std::forward<Args>(args)...);
        auto& ref = *ptr;
        strategies_.emplace(key, std::move(ptr));
        return ref;
    }

    // Проброс init() во все стратегии
    void init(const Ctx& ctx) {
        for (auto& [_, s] : strategies_) {
            s->init(ctx);
        }
    }

    // Главный tick: обновляем все рефы, вызывая tick() каждой стратегии
    void tick() {
        for (auto& [_, s] : strategies_) {
            s->tick();
        }
    }

    // Доступ к стратегии по ключу (если нужно)
    StrategyBase* get(const std::string& key) {
        auto it = strategies_.find(key);
        return (it == strategies_.end()) ? nullptr : it->second.get();
    }

private:
    std::unordered_map<std::string, StrategyUP> strategies_;
};

} // namespace dg
