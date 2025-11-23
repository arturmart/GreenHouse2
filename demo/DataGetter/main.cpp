#include <iostream>
#include <thread>
#include <chrono>

#include "DataGetter.hpp"
#include "RandomFloatStrategy.hpp"
#include "GlobalState.hpp"

int main() {
    using namespace dg;

    std::cout << "=== DataGetter test ===\n";

    // 1) Создаём менеджер
    DataGetter getter;

    // 2) Добавляем стратегию
    auto& strat = getter.emplace<RandomFloatStrategy>("rand");

    // 3) Привязываем стратегию к глобальному стейту
    auto& gs = GlobalState::instance();
    strat.initRef(gs.dataGetter.randomNumber);

    // 4) Инициализируем (нам пока нечего передавать)
    DataGetter::Ctx ctx;
    getter.init(ctx);

    // 5) Главный цикл
    for (int i = 0; i < 10; i++) {
        getter.tick();   // обновит gs.dataGetter.randomNumber

        if (gs.dataGetter.randomNumber.valid) {
            std::cout
                << "Tick " << i
                << " | Value = " << gs.dataGetter.randomNumber.value
                << " | Valid = " << gs.dataGetter.randomNumber.valid
                << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "=== Test completed ===\n";
    return 0;
}
