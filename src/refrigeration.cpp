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
        refrigeration_system();
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

void refrigeration_system(){
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
    display_system();
}

void display_system(){
    try {
        display1.display("Status: " + system_status, 0);
        std::stringstream ss;
        ss << "SP: " << setpoint << " RT: " << return_temp;
        std::string setpoint_string = ss.str();
        display1.display(setpoint_string, 1);

        ss.str("");
        ss << "CT: " << coil_temp << " DT: " << supply_temp;
        std::string coil_temp_string = ss.str();
        display1.display(coil_temp_string, 2);

        //lcd2
        display2.display("Status: " + system_status, 0);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }
}

int main() {
    std::cout << "Welcome to the Refrigeration system \n";
    std::cout << "The system is starting up please wait \n";
    std::thread sensor_thread(update_sensor_thread);

    if (cfg.get("sensor.return") == "0") {
        display_all_variables();
    } else {
        sensor_thread.join();
    }

    return 0;
}