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
    std::cout << "  HAVE A NICE DAY AND LET ME KNOW IF YOU NEED HELP \n";
}

void update_sensor_thread(){
    while (true) {
        std::lock_guard<std::mutex> lock(mtx);
        return_temp = sensors.readSensor(cfg.get("sensor.return"));
        supply_temp = sensors.readSensor(cfg.get("sensor.supply"));
        coil_temp = sensors.readSensor(cfg.get("sensor.coil"));
        std::cout << "Return: " << return_temp << " Supply: " << supply_temp << " Coil: " << coil_temp << "Setpoint: "<< setpoint <<"\n";
        mtx.unlock(); // Forgot to unlock it
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

std::string null_mode(){
    return "Null";
}
std::string cooling_mode(){
    return "Cooling";
}
std::string heating_mode(){
    return "Heating";
}

void refrigeration_system_thread(){
    while (true) {
        std::lock_guard<std::mutex> lock(mtx);
        if(system_status == "Cooling"){ // Only check this if we are in cooling mode
            std::cout << "Inside Cooling" << "\n";
            if(return_temp <= setpoint){ //Checked if already cooling when should we stop
                system_status = null_mode();
            }
        }
        if(system_status == "Heating"){ // Only check this if we are in heating mode
            std::cout << "Inside Heating" << "\n";
            if(return_temp >= setpoint){ //Checked if already heating when should we stop
                system_status = null_mode();
            }
        }
        if(system_status == "Null"){
            std::cout << "Inside Null" << "\n";
            if(return_temp >= setpoint){
                system_status = cooling_mode();
            }
            if(return_temp <= setpoint){
                system_status = heating_mode();
            }
        }
        mtx.unlock();
        std::cout << "System Status: " << system_status << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    }
}

int main() {
    std::cout << "Welcome to the Refrigeration system \n";
    std::cout << "The system is starting up please wait \n";
    std::thread sensor_thread(update_sensor_thread);
    std::thread refrigeration_thread(refrigeration_system_thread);

    if (cfg.get("sensor.return") == "0") {
        display_all_variables();
    } else {
        sensor_thread.join();
        refrigeration_thread.join();
    }

    return 0;
}