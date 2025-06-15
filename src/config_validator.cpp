#include "config_validator.h"
#include <regex>

ConfigValidator::ConfigValidator() {
    schema_ = {
        {"logging.interval_sec",     {"300", ConfigType::Integer}},
        {"logging.retention_period", {"30", ConfigType::Integer}},
        {"trl.number",               {"1234", ConfigType::Integer}},
        {"defrost.interval_hours",   {"8", ConfigType::Integer}},
        {"defrost.timeout_mins",     {"45", ConfigType::Integer}},
        {"defrost.coil_temperature", {"45", ConfigType::Integer}},
        {"setpoint.offset",          {"2", ConfigType::Integer}},
        {"compressor.off_timer",     {"5", ConfigType::Integer}},
        {"debug.code",               {"1", ConfigType::Integer}},
        {"wifi.enable_hotspot",      {"1", ConfigType::Integer}},
        {"debug.enable_send_data",   {"0", ConfigType::Boolean}},
        {"sensor.return",   {"0", ConfigType::Integer}},
        {"sensor.supply",   {"0", ConfigType::Integer}},
        {"sensor.coil",   {"0", ConfigType::Integer}}
    };
}

bool ConfigValidator::validate(const std::string& key, const std::string& value) const {
    auto it = schema_.find(key);
    if (it == schema_.end()) return false;

    const auto& type = it->second.type;

    try {
        switch (type) {
            case ConfigType::Integer:
                std::stoi(value);
                return true;
            case ConfigType::Boolean:
                return value == "0" || value == "1";
            case ConfigType::String:
                return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

std::optional<std::string> ConfigValidator::getDefault(const std::string& key) const {
    auto it = schema_.find(key);
    return (it != schema_.end()) ? std::optional<std::string>{it->second.defaultValue} : std::nullopt;
}

bool ConfigValidator::isKeyKnown(const std::string& key) const {
    return schema_.find(key) != schema_.end();
}

const std::map<std::string, ConfigEntry>& ConfigValidator::getSchema() const {
    return schema_;
}