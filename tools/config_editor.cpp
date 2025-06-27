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
            printMainMenu();

            int choice = getMenuChoice(2); // 1: Edit config, 2: Service menu, 0: Exit
            if (choice == 0) {
                if (confirmSave()) {
                    manager.save();
                }
                break;
            } else if (choice == 1) {
                // Stop service before editing config
                if (killRefrigerationProcess()) {
                    std::cout << "Waiting for ./refrigeration to close...\n";
                }
                runConfigMenu();
            } else if (choice == 2) {
                runServiceMenu();
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

    void printMainMenu() {
        std::cout << "=== Main Menu ===\n";
        std::cout << "1. Edit configuration (requires stopping refrigeration.service)\n";
        std::cout << "2. Manage refrigeration.service\n";
        std::cout << "0. Save and Exit\n\n";
        std::cout << "Enter your choice: ";
    }

    int getMenuChoice(int maxOption) {
        int choice;
        while (!(std::cin >> choice) || choice < 0 || choice > maxOption) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid choice. Please try again: ";
        }
        return choice;
    }

    void runConfigMenu() {
        while (true) {
            clearScreen();
            printCurrentConfig();
            printConfigMenu();

            int choice = getMenuChoice(manager.getSchema().size());
            if (choice == 0) break;

            if (choice > 0 && choice <= manager.getSchema().size()) {
                editConfigItem(choice - 1);
            }
        }
    }

    void printConfigMenu() {
        std::cout << "=== Config Menu ===\n";
        std::cout << "1-" << manager.getSchema().size() << ". Edit configuration item\n";
        std::cout << "0. Back to Main Menu\n\n";
        std::cout << "Enter your choice: ";
    }

    void runServiceMenu() {
        while (true) {
            clearScreen();
            std::cout << "=== refrigeration.service Menu ===\n";
            std::cout << "1. Start service\n";
            std::cout << "2. Stop service\n";
            std::cout << "3. Restart service\n";
            std::cout << "4. View logs (journalctl -u refrigeration.service -f)\n";
            std::cout << "0. Back to Main Menu\n";
            std::cout << "Enter your choice: ";

            int choice = getMenuChoice(4);
            if (choice == 0) break;

            switch (choice) {
                case 1:
                    system("sudo systemctl start refrigeration.service");
                    std::cout << "Service started. Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 2:
                    system("sudo systemctl stop refrigeration.service");
                    std::cout << "Service stopped. Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 3:
                    system("sudo systemctl restart refrigeration.service");
                    std::cout << "Service restarted. Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 4:
                    std::cout << "Press Ctrl+C to exit logs.\n";
                    system("sudo journalctl -u refrigeration.service -f");
                    break;
            }
        }
    }

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

int main(int argc, char* argv[]) {
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