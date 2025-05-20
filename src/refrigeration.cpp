#include "refrigeration.h"

void display_all_variables(){
    logger.log_events("Debug", "YOU NEED TO RUN config_editor to initalize the sensors");
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
    while (running) {
        std::lock_guard<std::mutex> lock(mtx);
        return_temp = sensors.readSensor(cfg.get("sensor.return"));
        supply_temp = sensors.readSensor(cfg.get("sensor.supply"));
        coil_temp = sensors.readSensor(cfg.get("sensor.coil"));
        time_t current_time = time(NULL);
        if (current_time - last_log_timestamp >= static_cast<time_t>(log_interval)) {
            logger.log_conditions(setpoint, return_temp, coil_temp, supply_temp, status);
            last_log_timestamp = time(NULL);
        }
        mtx.unlock();
        refrigeration_system();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
    cleanup_all();
}

void null_mode() {
    status["status"] = "Null";
    status["compressor"] = "False";
    status["fan"] = "False";
    status["valve"] = "False";
    status["electric_heater"] = "False";
    compressor_last_stop_time = time(NULL);
    logger.log_events("Debug", "Lasttime: " + compressor_last_stop_time);
    update_gpio_from_status();
    logger.log_events("Debug", "System Status: " + status["status"]);
    return;
}

void cooling_mode() {
    status["status"] = "Cooling";
    status["compressor"] = "True";
    status["fan"] = "True";
    status["valve"] = "False";
    status["electric_heater"] = "False";
    update_gpio_from_status();
    logger.log_events("Debug", "System Status: " + status["status"]);
    return;
}

void heating_mode() {
    status["status"] = "Heating";
    status["compressor"] = "True";
    status["fan"] = "True";
    status["valve"] = "True";
    status["electric_heater"] = "True";
    update_gpio_from_status();
    logger.log_events("Debug", "System Status: " + status["status"]);
    return;
}

void defrost_mode() {
    status["status"] = "Defrost";
    status["compressor"] = "True";
    status["fan"] = "False";
    status["valve"] = "True";
    status["electric_heater"] = "True";
    update_gpio_from_status();
    logger.log_events("Debug", "System Status: " + status["status"]);
    return;
}

void update_gpio_from_status() {
    gpio.write("fan_pin", status["fan"] == "True");
    gpio.write("compressor_pin", status["compressor"] == "True");
    gpio.write("valve_pin", status["valve"] == "True");
    gpio.write("electric_heater_pin", status["electric_heater"] == "True");
}

void refrigeration_system(){
    time_t current_time = time(NULL);
    int off_timer_value = stoi(cfg.get("compressor.off_timer")) * 60;
    int setpoint_offset = stoi(cfg.get("setpoint.offset"));
    std::lock_guard<std::mutex> lock(mtx);
    if(status["status"] == "Cooling"){ // Only check this if we are in cooling mode
        if(return_temp <= setpoint){ //Checked if already cooling when should we stop
            null_mode();
        }
    }
    if(status["status"] == "Heating"){ // Only check this if we are in heating mode
        if(return_temp >= setpoint){ //Checked if already heating when should we stop
            null_mode();
        }
    }
    if(status["status"] == "Null"){
        if (current_time - compressor_last_stop_time >= static_cast<time_t>(off_timer_value)) {
            if(return_temp >= (setpoint + setpoint_offset)){
                cooling_mode();
            }
            if(return_temp <= (setpoint - setpoint_offset)){
                heating_mode();
            }
            anti_timer = false;
        } else {
            logger.log_events("Debug", "Inside AntiCycle");
            anti_timer = true;
        }
    }
    mtx.unlock();
    display_system();
}

void display_system(){
    try {
        display1.display("Status: " + status["status"], 0);
        std::stringstream ss;
        ss << "SP: " << setpoint << " RT: " << return_temp;
        std::string setpoint_string = ss.str();
        display1.display(setpoint_string, 1);

        ss.str("");
        ss << "CT: " << coil_temp << " DT: " << supply_temp;
        std::string coil_temp_string = ss.str();
        display1.display(coil_temp_string, 2);

        //lcd2
        display2.display("Status: " + status["status"], 0);

    } catch (const std::exception& e) {
        logger.log_events("Error", std::string("During display updating: ") + e.what());
        return;
    }
}

void signalHandler(int signal) {
    if (signal == SIGINT) {
        running = false;
    }
}

void cleanup_all(){
    // Cleanup code when thread exits
    logger.log_events("Debug",  "Running Cleanup");
    display1.clear();
    display2.clear();
    display1.backlight(false);
    display2.backlight(false);

    try {
        gpio.write("fan_pin", false);
        gpio.write("compressor_pin", false);
        gpio.write("valve_pin", false);
        gpio.write("electric_heater_pin", false);
    } catch (const std::exception& e) {
        logger.log_events("Error", std::string("During GPIO cleanup: ") + e.what());
    }
}
int main() {
    // Set up signal handler
    std::signal(SIGINT, signalHandler);

    std::cout << "Welcome to the Refrigeration system \n";
    std::cout << "The system is starting up please wait \n";
    std::cout << "Press Ctrl+C to exit gracefully\n";
    std::cout << "Version: " << version << "\n";
    logger.log_events("Debug", "System started up");

    try {
        std::thread sensor_thread(update_sensor_thread);

        if (cfg.get("sensor.return") == "0") {
            display_all_variables();
        } else {
            // Wait for the sensor thread to finish (which will happen when running becomes false)
            sensor_thread.join();
        }

        cleanup_all();
        logger.clear_old_logs(log_retention_period);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}