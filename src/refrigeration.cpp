#include "config_manager.h"
#include "sensor_manager.h"
#include "refrigeration.h"

void display_all_variables(){
    std::cout << "YOU NEED TO RUN config_editor to initalize the sensors.....\n\n\n\n";
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

    std::cout << "return: " << cfg.get("sensor.return") << "\n";
    std::cout << "coil: " << cfg.get("sensor.coil") << "\n";
    std::cout << "supply: " << cfg.get("sensor.supply") << "\n";
}

void update_sensor_thread(){
    while (true) {
        std::lock_guard<std::mutex> lock(mtx);
        return_temp = sensors.readSensor(cfg.get("sensor.return"));
        supply_temp = sensors.readSensor(cfg.get("sensor.supply"));
        coil_temp = sensors.readSensor(cfg.get("sensor.coil"));
        std::cout << "Return: " << return_temp << " Supply: " << supply_temp << " Coil: " << coil_temp << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(30000));
    }
}

int main() {
    std::thread sensor_thread(update_sensor_thread);

    if (cfg.get("sensor.return") == "0") {
        display_all_variables();
    } else {
        sensor_thread.join();
    }

    return 0;
}