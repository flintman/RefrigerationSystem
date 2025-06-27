// config_editor.cpp
#include "config_manager.h"
#include "config_validator.h"
#include "sensor_manager.h"
#include <iostream>
#include <iomanip>
#include <termios.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <chrono>
#include <thread>

class ConfigEditor {
public:
    ConfigEditor(const std::string& filepath) : manager(filepath) {}

    void run() {
        while (true) {
            clearScreen();
            printCurrentConfig();
            printMenu();

            int choice = getMenuChoice();
            if (choice == 0) {
                if (confirmSave()) {
                    manager.save();
                }
                break;
            }

            if (choice > 0 && choice <= manager.getSchema().size()) {
                editConfigItem(choice - 1);
            }
        }
    }

private:
    ConfigManager manager;
    SensorManager sensors;

    void clearScreen() {
        std::cout << "\033[2J\033[1;1H"; // ANSI escape codes
    }

    void printCurrentConfig() {
        std::cout << "=== Current Configuration ===\n";
        int index = 1;
        for (const auto& [key, entry] : manager.getSchema()) {
            std::cout << std::setw(2) << index++ << ". "
                      << std::setw(30) << std::left << key
                      << " = " << manager.get(key)
                      << " (default: " << entry.defaultValue << ")\n";
        }
        std::cout << "\n\n";

        std::cout << "=== Temp Sensors ===\n";
        sensors.readOneWireTempSensors();
        std::cout << "\n\n";
    }

    void printMenu() {
        std::cout << "=== Menu ===\n";
        std::cout << "1-" << manager.getSchema().size() << ". Edit configuration item\n";
        std::cout << "0. Save and Exit\n\n";
        std::cout << "Enter your choice: ";
    }

    int getMenuChoice() {
        int choice;
        while (!(std::cin >> choice) || choice < 0 || choice > manager.getSchema().size()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid choice. Please try again: ";
        }
        return choice;
    }

    void editConfigItem(int index) {
        auto it = manager.getSchema().begin();
        std::advance(it, index);
        const auto& [key, entry] = *it;

        std::cout << "\nEditing: " << key << "\n";
        std::cout << "Current value: " << manager.get(key) << "\n";
        std::cout << "Default value: " << entry.defaultValue << "\n";
        std::cout << "Enter new value (or 'd' for default, 'c' to cancel): ";

        std::string input;
        std::cin >> input;

        if (input == "d") {
            manager.set(key, entry.defaultValue);
            std::cout << "Reset to default value.\n";
        } else if (input != "c") {
            if (manager.set(key, input)) {
                std::cout << "Value updated successfully.\n";
            } else {
                std::cout << "Invalid value for this configuration item.\n";
            }
        }

        std::cout << "Press Enter to continue...";
        std::cin.ignore();
        std::cin.get();
    }

    bool confirmSave() {
        std::cout << "\nSave changes to config file? (y/n): ";
        char choice;
        std::cin >> choice;
        return (choice == 'y' || choice == 'Y');
    }
};

int killRefrigerationProcess() {
    FILE* pipe = popen("pgrep -x refrigeration", "r");
    if (!pipe) return 0;
    char buffer[128];
    bool killed = false;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        int pid = atoi(buffer);
        if (pid > 0) {
            kill(pid, SIGINT); // Send Ctrl+C
            killed = true;
        }
    }
    pclose(pipe);

    // Wait for process to exit
    if (killed) {
        while (true) {
            FILE* check = popen("pgrep -x refrigeration", "r");
            bool running = (fgets(buffer, sizeof(buffer), check) != nullptr);
            pclose(check);
            if (!running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    return killed ? 1 : 0;
}

int main(int argc, char* argv[]) {
    if (killRefrigerationProcess()) {
        std::cout << "Waiting for ./refrigeration to close...\n";
    }

    const char* defaultConfig = "/etc/refrigeration/config.env";
    if (argc != 2) {
        // Check if default config exists
        FILE* file = fopen(defaultConfig, "r");
        if (!file) {
            std::cerr << "Usage: " << argv[0] << " <config_file_path>\n";
            std::cerr << "Default config file not found at " << defaultConfig << "\n";
            return 1;
        }
        fclose(file);
        argv[1] = const_cast<char*>(defaultConfig);
    }

    ConfigEditor editor(argv[1]);
    editor.run();
    return 0;
}