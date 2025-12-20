#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include "config_validator.h"

class ConfigManager {
public:
    ConfigManager(const std::string& filepath);
    ~ConfigManager();

    std::string get(const std::string& key) const;
    bool deleteConfigVar(const std::string& key);
    bool update(const std::string& key, const std::string& value);
    bool resetToDefaults();

    const std::map<std::string, ConfigEntry>& getSchema() const {
        return validator_.getSchema();
    }

private:
    std::string filepath_;
    std::map<std::string, std::string> configValues_;
    ConfigValidator validator_;

    void loadFromDotEnv();
    void initializeWithDefaults();
    bool set(const std::string& key, const std::string& value);
    void saveToDotEnv() const;
    bool save();
};

#endif // CONFIG_MANAGER_H