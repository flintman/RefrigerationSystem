#ifndef CONFIG_VALIDATOR_H
#define CONFIG_VALIDATOR_H

#include <string>
#include <optional>
#include <map>

enum class ConfigType {
    Integer,
    Boolean,
    String
};

struct ConfigEntry {
    std::string defaultValue;
    ConfigType type;
};

class ConfigValidator {
public:
    ConfigValidator();
    bool validate(const std::string& key, const std::string& value) const;
    std::optional<std::string> getDefault(const std::string& key) const;
    bool isKeyKnown(const std::string& key) const;
    const std::map<std::string, ConfigEntry>& getSchema() const;

private:
    std::map<std::string, ConfigEntry> schema_;
};

#endif // CONFIG_VALIDATOR_H