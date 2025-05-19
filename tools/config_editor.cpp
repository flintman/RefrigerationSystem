// config_editor.cpp
#include "config_manager.h"
#include "config_validator.h"
#include "refrigeration.h"
#include <iostream>
#include <iomanip>
#include <termios.h>
#include <unistd.h>

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
                break;
            }
            
            if (choice > 0 && choice <= manager.getSchema().size()) {
                editConfigItem(choice - 1);
            }
        }
    }

private:
    ConfigManager manager;

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
        std::cout << "\n";
    }

    void printMenu() {
        std::cout << "=== Menu ===\n";
        std::cout << "1-" << manager.getSchema().size() << ". Edit configuration item\n";
        std::cout << "0. Exit\n\n";
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
        
        if (confirmSave()) {
                manager.save();
        }
    }

    bool confirmSave() {
        std::cout << "\nSave changes to config file? (y/n): ";
        char choice;
        std::cin >> choice;
        return (choice == 'y' || choice == 'Y');
    }
};

int main() {
    ConfigEditor editor(config_file_name);
    editor.run();
    return 0;
}