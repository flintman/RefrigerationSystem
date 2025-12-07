/*
 * Configuration Manager Implementation
 */

#include "../include/tools/web_interface/config_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <iomanip>
#include <map>

ConfigManager::ConfigManager(const std::string& config_file)
    : config_file_(config_file), email_port_(587), web_port_(9000),
      config_file_mtime_(0), watching_(false) {
    load_config();
}

ConfigManager::~ConfigManager() {
    stop_watch_thread();
}

void ConfigManager::load_config() {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::ifstream file(config_file_);
    if (!file.is_open()) {
        write_log("ERROR: Config file not found: " + config_file_);
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Trim whitespace around values
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Remove quotes if present
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "EMAIL_ADDRESS") {
            email_address_ = value;
        } else if (key == "EMAIL_PASSWORD") {
            email_password_ = value;
        } else if (key == "EMAIL_SERVER") {
            email_server_ = value;
        } else if (key == "EMAIL_PORT") {
            try {
                email_port_ = std::stoi(value);
            } catch (...) {
                email_port_ = 587;
            }
        } else if (key == "WEB_PORT") {
            try {
                web_port_ = std::stoi(value);
            } catch (...) {
                web_port_ = 9000;
            }
        } else if (key == "WEB_PASSWORD") {
            web_password_ = value;
        }
    }

    load_units_from_config();
    write_log("Configuration loaded successfully");
}

void ConfigManager::load_units_from_config() {
    units_.clear();

    std::ifstream file(config_file_);
    if (!file.is_open()) {
        write_log("ERROR: Cannot open config file to load units");
        return;
    }

    std::map<std::string, Unit> unit_map;
    std::string line;

    while (std::getline(file, line)) {
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        // Parse UNIT_X_* format
        if (key.substr(0, 5) == "UNIT_") {
            size_t first_underscore = 5;
            size_t second_underscore = key.find('_', first_underscore);
            if (second_underscore != std::string::npos) {
                std::string unit_num = key.substr(first_underscore, second_underscore - first_underscore);
                std::string field = key.substr(second_underscore + 1);

                if (unit_map.find(unit_num) == unit_map.end()) {
                    unit_map[unit_num] = Unit{"", "", 0, ""};
                }

                if (field == "ID") {
                    unit_map[unit_num].id = value;
                } else if (field == "ADDRESS") {
                    unit_map[unit_num].api_address = value;
                } else if (field == "PORT") {
                    try {
                        unit_map[unit_num].api_port = std::stoi(value);
                    } catch (...) {
                        unit_map[unit_num].api_port = 8095;
                    }
                } else if (field == "KEY") {
                    unit_map[unit_num].api_key = value;
                }
            }
        }
    }

    // Convert map to vector, only adding complete units
    for (const auto& pair : unit_map) {
        if (!pair.second.id.empty() && !pair.second.api_address.empty()) {
            units_.push_back(pair.second);
            write_log("Loaded unit: " + pair.second.id + " at " + pair.second.api_address + ":" + std::to_string(pair.second.api_port));
        }
    }

    write_log("Loaded " + std::to_string(units_.size()) + " units from config");
}

void ConfigManager::start_watch_thread() {
    if (watching_) return;

    watching_ = true;
    config_watch_thread_ = std::thread(&ConfigManager::config_watch_loop, this);
    write_log("Config watch thread started");
}

void ConfigManager::stop_watch_thread() {
    watching_ = false;
    if (config_watch_thread_.joinable()) {
        config_watch_thread_.join();
    }
    write_log("Config watch thread stopped");
}

void ConfigManager::config_watch_loop() {
    while (watching_) {
        reload_if_changed();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void ConfigManager::reload_if_changed() {
    struct stat st;
    if (stat(config_file_.c_str(), &st) == 0) {
        if (st.st_mtime > config_file_mtime_) {
            write_log("Config file changed, reloading...");
            config_file_mtime_ = st.st_mtime;
            load_config();
            if (on_config_changed_) {
                on_config_changed_();
            }
        }
    }
}

void ConfigManager::write_log(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time);

    std::cout << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] [ConfigManager] " << message << std::endl;
}
