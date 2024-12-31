#include "config_manager.h"
#include "config_validator.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

ConfigManager::ConfigManager(const std::string& filepath)
    : filepath_(filepath) {

    if (!std::filesystem::exists(filepath_)) {
        std::cout << "[ConfigManager] Config file not found, attempting to load JSON fallback...\n";
        initializeWithDefaults();
        saveToDotEnv();
    } else {
        loadFromDotEnv();
    }
}

ConfigManager::~ConfigManager() {
    saveToDotEnv();
}

std::string ConfigManager::get(const std::string& key) const {
    auto it = configValues_.find(key);
    return (it != configValues_.end()) ? it->second : "";
}

bool ConfigManager::set(const std::string& key, const std::string& value) {
    if (!validator_.isKeyKnown(key)) {
        std::cerr << "[ConfigManager] Unknown config key: " << key << "\n";
        return false;
    }
    if (!validator_.validate(key, value)) {
        std::cerr << "[ConfigManager] Invalid value for key: " << key << "\n";
        return false;
    }
    configValues_[key] = value;
    return true;
}

bool ConfigManager::deleteConfigVar(const std::string& key) {
    return configValues_.erase(key) > 0;
}

bool ConfigManager::save() {
    saveToDotEnv();
    return true;
}

void ConfigManager::initializeWithDefaults() {
    for (const auto& [key, config] : validator_.getSchema()) {
        configValues_[key] = config.defaultValue;
    }
}

void ConfigManager::loadFromDotEnv() {
    std::ifstream file(filepath_);
    std::string line;

    if (!file.is_open()) {
        std::cerr << "[ConfigManager] Could not open config file: " << filepath_ << "\n";
        return;
    }

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            set(key, value);
        }
    }
    file.close();
}

void ConfigManager::saveToDotEnv() const {
    std::ofstream file(filepath_);
    for (const auto& [key, value] : configValues_) {
        file << key << "=" << value << "\n";
    }
    file.close();
}
