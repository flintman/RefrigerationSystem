#include "config_manager.h"
#include "refrigeration.h"
#include <iostream>

int main() {
    ConfigManager cfg(config_file_name);

    std::cout << "Logging Interval: " << cfg.get("logging.interval_sec") << " seconds\n";
    std::cout << "Log Retention Period: " << cfg.get("logging.retention_period") << " days\n";
    std::cout << "TRL Number: " << cfg.get("trl.number") << "\n";
    std::cout << "Defrost Interval: " << cfg.get("defrost.interval_hours") << " hours\n";
    std::cout << "Defrost Timeout: " << cfg.get("defrost.timeout_mins") << " minutes\n";
    std::cout << "Defrost Coil Temperature: " << cfg.get("defrost.coil_temperature") << "°F\n";
    std::cout << "Temperature Setpoint Offset: " << cfg.get("setpoint.offset") << "°F\n";
    std::cout << "Compressor Off Timer: " << cfg.get("compressor.off_timer") << " minutes\n";
    std::cout << "Debug Code: " << cfg.get("debug.code") << "\n";
    std::cout << "Debug Data Sending: " << (cfg.get("debug.enable_send_data") == "1" ? "Enabled" : "Disabled") << "\n";

    return 0;
}