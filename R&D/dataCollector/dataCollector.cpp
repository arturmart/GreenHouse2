#include <iostream>
#include <unordered_map>
#include <memory>
#include <any>
#include <string>

// ===== Абстрактная стратегия =====
class ADataStrategy {
public:
    virtual ~ADataStrategy() = default;
    virtual void tick() = 0;              // обновление состояния
    virtual std::any get() = 0;           // получение данных
};

// ===== Конкретные стратегии =====

// Датчик температуры
class TempSensorStrategy : public ADataStrategy {
    float value = 0.0f;
public:
    void tick() override {
        // имитация чтения температуры
        value = 24.7f;
    }
    std::any get() override {
        return value;
    }
};

// Уровень жидкости в баке
class TankLevelStrategy : public ADataStrategy {
    int level = 0;
public:
    void tick() override {
        // имитация уровня бака
        level = 83;
    }
    std::any get() override {
        return level;
    }
};

// Газовый сенсор
class GasSensorStrategy : public ADataStrategy {
    bool detected = false;
public:
    void tick() override {
        // имитация обнаружения газа
        detected = true;
    }
    std::any get() override {
        return detected;
    }
};

// ===== Сборщик данных =====
class DataCollector {
    std::unordered_map<std::string, std::unique_ptr<ADataStrategy>> strategies;
public:
    DataCollector() {
        init();  // сразу при создании регистрируем сенсоры
    }

    void init() {
        strategies["temp"] = std::make_unique<TempSensorStrategy>();
        strategies["tank"] = std::make_unique<TankLevelStrategy>();
        strategies["gas"]  = std::make_unique<GasSensorStrategy>();
    }

    void tickAll() {
        for (auto& [k, s] : strategies) {
            s->tick();
        }
    }

    std::any getData(const std::string& key) {
        auto it = strategies.find(key);
        if (it != strategies.end()) {
            return it->second->get();
        }
        return {};
    }
};

// ===== main =====
int main() {
    DataCollector collector;

    // обновляем все датчики
    collector.tickAll();

    // вывод данных
    try {
        std::cout << "Temperature: "
                  << std::any_cast<float>(collector.getData("temp"))
                  << " °C\n";

        std::cout << "Tank level: "
                  << std::any_cast<int>(collector.getData("tank"))
                  << " %\n";

        std::cout << "Gas detected: "
                  << std::boolalpha
                  << std::any_cast<bool>(collector.getData("gas"))
                  << "\n";
    } catch (const std::bad_any_cast& e) {
        std::cerr << "Type error: " << e.what() << "\n";
    }

    return 0;
}
