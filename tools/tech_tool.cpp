

/*
 * Refrigeration Server
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * This project includes third-party software:
 * - OpenSSL (Apache License 2.0)
 * - ws2811 (MIT License)
 * - nlohmann/json (MIT License)
 */

#include "config_manager.h"
#include "sensor_manager.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <limits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <chrono>


class ConfigEditor {
public:
    ConfigEditor(const std::string& filepath)
        : manager(filepath), pollingActive(false) {}

    void printHeader(const std::string& title) {
        std::cout << "\033[1;34m";
        std::cout << "========================================\n";
        std::cout << title << "\n";
        std::cout << "========================================\n";
        std::cout << "\033[0m";
    }

    void startSensorPolling() {
        pollingActive = true;
        pollingThread = std::thread([this]() {
            while (pollingActive) {
                auto lines = sensors.readOneWireTempSensors();
                {
                    std::lock_guard<std::mutex> lock(sensorMutex);
                    latestSensorLines = std::move(lines);
                }
                if (!polling1stfetch) {
                    polling1stfetch = true;
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        });
    }

    void stopSensorPolling() {
        pollingActive = false;
        polling1stfetch = false;
        if (pollingThread.joinable()) pollingThread.join();
    }

    void run() {
        startSensorPolling();
        // Wait until the first sensor poll is complete
        std::cout << "1st pull on Polling sensors...\n";
        while (!polling1stfetch) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        while (true) {
            clearScreen();
            printCurrentConfig();
            printTemperatureSensors();
            printMainMenu();
            int choice = getMenuChoice(3);
            if (choice == 0) {
                break;
            } else if (choice == 1) {
                if (killRefrigerationProcess()) {
                    std::cout << "Waiting for ./refrigeration to close...\n";
                }
                runConfigMenu();
            } else if (choice == 2) {
                runServiceMenu();
            } else if (choice == 3) {
                runLiveTemperatureDisplay();
            }
        }
        stopSensorPolling();
    }

    void clearScreen() {
        std::cout << "\033[2J\033[H";
    }
    void printCurrentConfig() {
        printHeader("Current Configuration");
        int index = 1;
        for (const auto& [key, entry] : manager.getSchema()) {
            std::cout << "  \033[1;33m" << std::setw(2) << index++ << ".\033[0m "
                      << std::setw(30) << std::left << key
                      << " = \033[1;32m" << manager.get(key) << "\033[0m"
                      << " (default: " << entry.defaultValue << ")\n";
        }
        std::cout << "\n";
    }
    void printTemperatureSensors() {
        printHeader("Temperature Sensors");
        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(sensorMutex);
            lines = latestSensorLines;
        }
        for (const auto& line : lines) {
            std::cout << line << '\n';
        }
        std::cout << "\n";
    }
    void printMainMenu() {
        printHeader("Main Menu");
        std::cout << "  \033[1;36m1.\033[0m Edit configuration (requires stopping refrigeration.service)\n";
        std::cout << "  \033[1;36m2.\033[0m Manage refrigeration.service\n";
        std::cout << "  \033[1;36m3.\033[0m Live temperature sensors\n";
        std::cout << "  \033[1;31m0.\033[0m Exit\n\n";
        std::cout << "Enter your choice: ";
    }
    int getMenuChoice(int maxOption) {
        int choice;
        while (!(std::cin >> choice) || choice < 0 || choice > maxOption) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\033[1;31mInvalid choice. Please try again:\033[0m ";
        }
        return choice;
    }
    void runConfigMenu() {
        while (true) {
            clearScreen();
            printCurrentConfig();
            printTemperatureSensors();
            printConfigMenu();
            std::string choiceStr;
            std::cin >> choiceStr;
            if (choiceStr == "0") {
                break;
            }
            if (choiceStr == "D" || choiceStr == "d") {
                resetConfigFileToDefault();
                continue;
            }
            try {
                int choice = std::stoi(choiceStr);
                if (choice > 0 && choice <= static_cast<int>(manager.getSchema().size())) {
                    editConfigItem(choice - 1);
                }
            } catch (...) {
                // Invalid input, ignore and loop again
            }
        }
    }
    void printConfigMenu() {
        printHeader("Config Menu");
        std::cout << "  \033[1;36m1-" << manager.getSchema().size() << ".\033[0m Edit configuration item\n";
        std::cout << "  \033[1;36mD.\033[0m Reset config file to default\n";
        std::cout << "  \033[1;31m0.\033[0m Back to Main Menu\n";
        std::cout << "Enter your choice: ";
    }
    void resetConfigFileToDefault() {
        std::cout << "\033[1;31mAre you sure you want to reset the configuration file to default values? (y/n): \033[0m";
        char confirm;
        std::cin >> confirm;
        if (confirm == 'y' || confirm == 'Y') {
            manager.resetToDefaults();
        } else {
            std::cout << "\033[1;33mReset cancelled.\033[0m\n";
            std::cout << "Press Enter to continue...";
            std::cin.ignore();
            std::cin.get();
            return;
        }
        std::cout << "\033[1;32mConfiguration file has been reset to default values.\033[0m\n";
        std::cout << "Press Enter to continue...";
        std::cin.ignore();
        std::cin.get();
    }
    void runServiceMenu() {
        while (true) {
            clearScreen();
            printHeader("refrigeration.service Menu");
            std::cout << "  \033[1;36m1.\033[0m Start service\n";
            std::cout << "  \033[1;36m2.\033[0m Stop service\n";
            std::cout << "  \033[1;36m3.\033[0m Restart service\n";
            std::cout << "  \033[1;36m4.\033[0m View status (systemctl status refrigeration.service)\n";
            std::cout << "  \033[1;36m5.\033[0m View logs (journalctl -u refrigeration.service -f)\n";
            std::cout << "  \033[1;31m0.\033[0m Back to Main Menu\n";
            std::cout << "Enter your choice: ";
            int choice = getMenuChoice(5);
            if (choice == 0) break;
            switch (choice) {
                case 1:
                    system("sudo systemctl start refrigeration.service");
                    std::cout << "\033[1;32mService started.\033[0m Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 2:
                    system("sudo systemctl stop refrigeration.service");
                    std::cout << "\033[1;33mService stopped.\033[0m Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 3:
                    system("sudo systemctl restart refrigeration.service");
                    std::cout << "\033[1;32mService restarted.\033[0m Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 4:
                    system("sudo systemctl status refrigeration.service");
                    std::cout << "\033[1;32mService status displayed.\033[0m Press Enter to continue...";
                    std::cin.ignore();
                    std::cin.get();
                    break;
                case 5:
                    std::cout << "\033[1;35mPress Ctrl+C to exit logs.\033[0m\n";
                    system("sudo journalctl -u refrigeration.service -f");
                    break;
            }
        }
    }
    int killRefrigerationProcess() {
        // Stop the systemd service first
        system("sudo systemctl stop refrigeration.service");
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
        std::cout << "\n\033[1;34mEditing: \033[1;33m" << key << "\033[0m\n";
        std::cout << "Current value: \033[1;32m" << manager.get(key) << "\033[0m\n";
        std::cout << "Default value: " << entry.defaultValue << "\n";
        std::cout << "Enter new value (or 'd' for default, 'c' to cancel): ";
        std::string input;
        std::cin >> input;
        if (input == "d") {
            manager.set(key, entry.defaultValue);
            manager.save();
            std::cout << "\033[1;32mReset to default value.\033[0m\n";
        } else if (input != "c") {
            if (manager.set(key, input)) {
                manager.save();
                std::cout << "\033[1;32mValue updated successfully.\033[0m\n";
            } else {
                std::cout << "\033[1;31mInvalid value for this configuration item.\033[0m\n";
            }
        }
        std::cout << "Press Enter to continue...";
        std::cin.ignore();
        std::cin.get();
    }
    void runLiveTemperatureDisplay() {
        while (true) {
            clearScreen();
            printHeader("Live Temperature Sensors (updates every 2s)");
            std::vector<std::string> lines;
            {
                std::lock_guard<std::mutex> lock(sensorMutex);
                lines = latestSensorLines;
            }
            for (const auto& line : lines) {
                std::cout << line << '\n';
            }
            std::cout << "\nPress 'q' then Enter to return to main menu.\n";
            // Non-blocking check for 'q' input
            fd_set set;
            struct timeval timeout;
            FD_ZERO(&set);
            FD_SET(0, &set);
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
            int rv = select(1, &set, NULL, NULL, &timeout);
            if (rv > 0) {
                std::string input;
                std::getline(std::cin, input);
                if (!input.empty() && (input[0] == 'q' || input[0] == 'Q')) break;
            }
        }
    }
private:
    ConfigManager manager;
    SensorManager sensors;
    std::vector<std::string> latestSensorLines;
    std::mutex sensorMutex;
    std::thread pollingThread;
    std::atomic<bool> pollingActive;
    std::atomic<bool> polling1stfetch{false};

};


int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        std::cerr << "This tool must be run as root (sudo).\n";
        return 1;
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