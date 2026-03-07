#include "DeviceControlModule.hpp"
#include <thread>
#include <chrono>

int main() {
    DeviceControlModule dcm("/dev/ttyS3", 115200);

    dcm.enqueuePacket({68, 0, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 0, 0});
    dcm.enqueuePacket({68, 1, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 1, 0});
    dcm.enqueuePacket({68, 2, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 2, 0});
    dcm.enqueuePacket({68, 3, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 3, 0});
    dcm.enqueuePacket({68, 4, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 4, 0});
    dcm.enqueuePacket({68, 5, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 5, 0});
    dcm.enqueuePacket({68, 6, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 6, 0});
    dcm.enqueuePacket({68, 7, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dcm.enqueuePacket({68, 7, 0});
    //dcm.enqueuePacket({80, 0, 128});
    //dcm.enqueueKeyword("showall");

    dcm.update();

    return 0;
}