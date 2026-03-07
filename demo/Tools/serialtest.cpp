#include "SerialComm.hpp"
#include <iostream>

int main(int argc, char* argv[]) {

    SerialComm serial("/dev/ttyS3", 115200);

    std::string feedback;

    if (argc == 1) {

        std::cout << "-h for help\n";
        feedback = serial.executeCommand("showall/8d984889\n");

    }
    else if (argc == 2) {

        std::string command = std::string(argv[1]) + "\n";

        if (command == "-h\n") {

            std::cout << "Usage: ./serialapp [command]\n";
            return 0;

        }

        feedback = serial.executeCommand(command);

    }

    if (!feedback.empty())
        std::cout << "Received: " << feedback << std::endl;
    else
        std::cout << "No response\n";

    return 0;
}