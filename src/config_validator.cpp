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

#include "config_validator.h"
#include <regex>

ConfigValidator::ConfigValidator() {
    schema_ = {
        {"api.key",                   {"refrigeration-api-default-key-change-me", ConfigType::String}},
        {"api.port",                  {"8095", ConfigType::Integer}},
        {"compressor.off_timer",      {"5", ConfigType::Integer}},
        {"debug.code",                {"1", ConfigType::Boolean}},
        {"defrost.coil_temperature",  {"45", ConfigType::Integer}},
        {"defrost.interval_hours",    {"8", ConfigType::Integer}},
        {"defrost.timeout_mins",      {"45", ConfigType::Integer}},
        {"logging.interval_mins",     {"5", ConfigType::Integer}},
        {"logging.retention_period",  {"30", ConfigType::Integer}},
        {"sensor.coil",               {"0", ConfigType::Integer}},
        {"sensor.return",             {"0", ConfigType::Integer}},
        {"sensor.supply",             {"0", ConfigType::Integer}},
        {"setpoint.high_limit",       {"80", ConfigType::Integer}},
        {"setpoint.low_limit",        {"-20", ConfigType::Integer}},
        {"setpoint.offset",           {"2", ConfigType::Integer}},
        {"unit.compressor_run_seconds", {"0", ConfigType::Integer}},
        {"unit.electric_heat",        {"1", ConfigType::Boolean}},
        {"unit.fan_continuous",       {"0", ConfigType::Boolean}},
        {"unit.number",               {"1234", ConfigType::Integer}},
        {"unit.relay_active_low",     {"1", ConfigType::Boolean}},
        {"unit.setpoint",             {"55", ConfigType::Integer}},
        {"wifi.enable_hotspot",       {"1", ConfigType::Boolean}},
        {"wifi.hotspot_password",     {"changeme", ConfigType::String}}
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