#include "config_manager.h"
#include "config_validator.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <sys/file.h>
#include <unistd.h>

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
    int fd = open(filepath_.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "[ConfigManager] Could not open config file for reading: " << filepath_ << "\n";
        return;
    }

    if (flock(fd, LOCK_SH) == -1) {
        std::cerr << "[ConfigManager] Failed to acquire shared lock on config file.\n";
        close(fd);
        return;
    }

    std::ifstream file(filepath_);
    std::string line;
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

    flock(fd, LOCK_UN); // Unlock the file
    close(fd);
}

void ConfigManager::saveToDotEnv() const {
    int fd = open(filepath_.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        std::cerr << "[ConfigManager] Failed to open file for writing: " << filepath_ << "\n";
        return;
    }

    if (flock(fd, LOCK_EX) == -1) {
        std::cerr << "[ConfigManager] Failed to acquire exclusive lock on config file.\n";
        close(fd);
        return;
    }

    // Use std::ofstream with file descriptor
    std::ofstream file;
    file.basic_ios<char>::rdbuf()->pubsetbuf(0, 0); // Disable internal buffering
    file.open(filepath_);
    if (file.is_open()) {
        for (const auto& [key, value] : configValues_) {
            file << key << "=" << value << "\n";
        }
        file.close();
    } else {
        std::cerr << "[ConfigManager] Could not write to config file: " << filepath_ << "\n";
    }

    flock(fd, LOCK_UN); // Unlock the file
    close(fd);
}

