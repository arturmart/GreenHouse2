#include <iostream>
#include <thread>
#include <chrono>

#include "GlobalState.hpp"
#include "DataGetter.hpp"
#include "DG_DS18B20.hpp"

int main() {

    // --- 1. Глобальное поле ---
    Field<float> tempRoom;

    // --- 2. DataGetter ---
    dg::DataGetter dataGetter;

    // --- 3. Добавляем стратегию ---
    // ID смотри через:
    // ls /sys/bus/w1/devices/
    // обычно 28-xxxxxxxxxxxx

    auto& ds = dataGetter.emplace<dg::DG_DS18B20>(
        "room_temp",
        "28-030397941733" 
    );

    // привязка к глобальному Field
    ds.initRef(tempRoom);

    // init (если нужен ctx)
    dataGetter.init({});

    std::cout << "DataGetter started...\n";

    // --- 4. Главный цикл ---
    while (true) {
        try {
            dataGetter.tick();   // обновляет Field

            if (tempRoom.isValid()) {
                std::cout << "Temperature: "
                          << tempRoom.get()
                          << " °C\n";
            }
            else {
                std::cout << "Temperature not valid\n";
            }

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}
