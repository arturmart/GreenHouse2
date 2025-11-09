#pragma once
#include <any>
#include <string>
#include <vector>
#include <unordered_map>

namespace exec {

class AExecutorStrategy {
public:
    using Args = std::vector<std::any>;
    using Ctx  = std::unordered_map<std::string, std::any>;

    virtual ~AExecutorStrategy() = default;

    // Основной вызов
    virtual void execute(const Args& args) = 0;

    // Инициализация зависимостей (по умолчанию — ничего)
    // Например: ctx["sim"] = (ISimControl*)ptr;
    virtual void init(const Ctx& /*ctx*/) {}

    // Периодический «пульс» (опционально)
    virtual void tick() {}

    // Имя стратегии (для логов/отладки)
    virtual std::string name() const { return "AExecutorStrategy"; }
};

} // namespace exec
