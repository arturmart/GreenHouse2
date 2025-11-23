#pragma once
#include <any>
#include <string>
#include <unordered_map>

// forward-декларация универсального поля из GlobalState.hpp
template<typename T>
struct Field;

namespace dg {

// Небольшой нетемплейтный базовый класс, чтобы DataGetter мог хранить
// стратегии разного типа в одном контейнере.
class ADataGetterStrategyBase {
public:
    using Ctx = std::unordered_map<std::string, std::any>;

    virtual ~ADataGetterStrategyBase() = default;

    // Инициализация зависимостей (как в AExecutorStrategy::init)
    virtual void init(const Ctx& /*ctx*/) {}

    // Периодический вызов от DataGetter::tick()
    // Конкретные стратегии внутри обычно делают getDataRef().
    virtual void tick() = 0;

    // Имя стратегии (для логов/отладки)
    virtual std::string name() const { return "ADataGetterStrategyBase"; }
};

// Шаблон абстрактной стратегии для конкретного типа T.
// T может быть чем угодно: float, int, struct { ... }, std::array<double,3> и т.п.
template<typename T>
class ADataGetterStrategy : public ADataGetterStrategyBase {
public:
    using value_type = T;
    using FieldType  = Field<T>;

protected:
    // «Сырая» переменная сенсора (тип и "количество" могут отличаться —
    // T может быть и скаляром, и структурой, и вектором)
    T sensorValue_{};

    // Ссылка (через указатель) на глобальное поле, куда складываем данные
    Field<T>* ref_ = nullptr;

public:
    virtual ~ADataGetterStrategy() = default;

    // Привязка к Field из GlobalState (initRef)
    void initRef(Field<T>& field) {
        ref_ = &field;
    }

    // Чтение датчика — ДОЛЖНА реализовать каждая конкретная стратегия.
    // Обычно внутри обновляется sensorValue_ и он же возвращается.
    virtual T getData() = 0;

    // Обновление привязанного Field в глобальном стейте
    // (валидность ставится true через Field<T>::set()).
    void getDataRef() {
        if (!ref_) {
            // нет привязки — просто ничего не делаем
            return;
        }
        T v = getData();
        ref_->set(v);
    }

    // По умолчанию tick() просто обновляет ref
    void tick() override {
        getDataRef();
    }

    // Имя стратегии (можно переопределять)
    std::string name() const override {
        return "ADataGetterStrategy<" + std::string(typeid(T).name()) + ">";
    }
};

} // namespace dg
