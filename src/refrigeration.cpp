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
    float setpoint_;
    while (running) {
        {
            std::lock_guard<std::mutex> setpoint_lock(setpoint_mutex);
            setpoint_ = setpoint;
            setpoint_mutex.unlock();
        }
        std::lock_guard<std::mutex> lock(mtx);
        return_temp = sensors.readSensor(cfg.get("sensor.return"));
        supply_temp = sensors.readSensor(cfg.get("sensor.supply"));
        coil_temp = sensors.readSensor(cfg.get("sensor.coil"));
        time_t current_time = time(NULL);
        if (current_time - last_log_timestamp >= static_cast<time_t>(log_interval)) {
            logger.log_conditions(setpoint_, return_temp, coil_temp, supply_temp, status);
            last_log_timestamp = time(NULL);
        }
        refrigeration_system(return_temp, supply_temp, coil_temp, setpoint_);
        mtx.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    cleanup_all();
}

void null_mode() {
    std::lock_guard<std::mutex> setpoint_lock(status_mutex);
    status["status"] = "Null";
    status["compressor"] = "False";
    status["fan"] = "True";
    status["valve"] = "False";
    status["electric_heater"] = "False";
    compressor_last_stop_time = time(NULL);
    logger.log_events("Debug", "System Status: " + status["status"]);
    status_mutex.unlock();
    update_gpio_from_status();
    return;
}

void cooling_mode() {
    std::lock_guard<std::mutex> setpoint_lock(status_mutex);
    status["status"] = "Cooling";
    status["compressor"] = "True";
    status["fan"] = "True";
    status["valve"] = "False";
    status["electric_heater"] = "False";
    logger.log_events("Debug", "System Status: " + status["status"]);
    status_mutex.unlock();
    update_gpio_from_status();
    return;
}

void heating_mode() {
    std::lock_guard<std::mutex> setpoint_lock(status_mutex);
    status["status"] = "Heating";
    status["compressor"] = "True";
    status["fan"] = "True";
    status["valve"] = "True";
    status["electric_heater"] = "True";
    logger.log_events("Debug", "System Status: " + status["status"]);
    status_mutex.unlock();
    update_gpio_from_status();
    return;
}

void defrost_mode() {
    std::lock_guard<std::mutex> setpoint_lock(status_mutex);
    status["status"] = "Defrost";
    status["compressor"] = "True";
    status["fan"] = "False";
    status["valve"] = "True";
    status["electric_heater"] = "True";
    defrost_start_time  = time(NULL);
    logger.log_events("Debug", "System Status: " + status["status"]);
    status_mutex.unlock();
    update_gpio_from_status();
    return;
}

void update_gpio_from_status() {
    std::lock_guard<std::mutex> setpoint_lock(status_mutex);
    gpio.write("fan_pin", status["fan"] == "False");
    gpio.write("compressor_pin", status["compressor"] == "False");
    gpio.write("valve_pin", status["valve"] == "False");
    gpio.write("electric_heater_pin", status["electric_heater"] == "False");
    status_mutex.unlock();
}

void refrigeration_system(float return_temp_, float supply_temp_, float coil_temp_, float setpoint_){
    std::string status_;
    time_t compressor_last_stop_time_;
    time_t current_time = time(NULL);
    int off_timer_value = stoi(cfg.get("compressor.off_timer")) * 60;
    int setpoint_offset = stoi(cfg.get("setpoint.offset"));
    int defrost_coil_temperature = stoi(cfg.get("defrost.coil_temperature"));
    int defrost_timeout = stoi(cfg.get("defrost.timeout_mins")) * 60;
    int defrost_intervals = (stoi(cfg.get("defrost.interval_hours")) * 60) * 60;
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status_ = status["status"];
        compressor_last_stop_time_ = compressor_last_stop_time;
        status_mutex.unlock();
    }

    if(status_ == "Cooling"){ // Only check this if we are in cooling mode
        if(return_temp_ <= setpoint_){ //Checked if already cooling when should we stop
            null_mode();
        }
    }

    if(status_== "Heating"){ // Only check this if we are in heating mode
        if(return_temp_ >= setpoint_){ //Checked if already heating when should we stop
            null_mode();
        }
    }

    if(status_ == "Null"){
        if (current_time - compressor_last_stop_time_ >= static_cast<time_t>(off_timer_value)) {
            if(return_temp_ >= (setpoint_ + setpoint_offset)){
                cooling_mode();
            }
            if(return_temp_ <= (setpoint_ - setpoint_offset)){
                heating_mode();
            }
            anti_timer = false;
        } else {
            logger.log_events("Debug", "Inside AntiCycle");
            anti_timer = true;
        }
    }

    if(status_ == "Defrost"){
        if ((coil_temp_ > defrost_coil_temperature) || ((current_time - defrost_last_time) < defrost_intervals)) {
            null_mode();
            defrost_last_time = time(NULL);
        }
    }

    if (coil_temp_ < defrost_coil_temperature) { // Make sure we are under the coil temp
        if(((current_time - defrost_last_time) > defrost_intervals) || trigger_defrost) { //Lets check how long since last or was it triggered
            if (defrost_start_time == 0) { // Only trigger the mode if not already called.
                trigger_defrost = false;
                defrost_mode();
            }
        }
    }
}

void display_system_thread(){
    // Local copies of protected variables
    float return_temp_;
    float supply_temp_;
    float coil_temp_;
    std::string status_;
    int setpoint_;

    while(running){
        // Lock and copy variables in separate scopes
        {
            std::lock_guard<std::mutex> lock(mtx);
            return_temp_ = return_temp;
            supply_temp_ = supply_temp;
            coil_temp_ = coil_temp;
            mtx.unlock();
        }
        {
            std::lock_guard<std::mutex> setpoint_lock(status_mutex);
            status_ = status["status"];
            status_mutex.unlock();
        }
        {
            std::lock_guard<std::mutex> setpoint_lock(setpoint_mutex);
            setpoint_ = setpoint;
            setpoint_mutex.unlock();
        }

        try {
            display1.display("Status: " + status_, 0);
            std::stringstream ss;
            ss << "SP: " << setpoint_ << " RT: " << return_temp_;
            std::string setpoint_string = ss.str();
            display1.display(setpoint_string, 1);

            ss.str("");
            ss << "CT: " << coil_temp_ << " DT: " << supply_temp_;
            std::string coil_temp_string = ss.str();
            display1.display(coil_temp_string, 2);

            //lcd2
            display2.display("Status: " + status_, 0);

        } catch (const std::exception& e) {
            logger.log_events("Error", std::string("During display updating: ") + e.what());
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void setpoint_system_thread(){
    float min_voltage = 0.00;
    float max_voltage = 3.28;
    float min_setpoint = -20;
    float max_setpoint = 80;

    while (running) {
        float voltage = adc.readVoltage(3);
        if (!std::isfinite(voltage)) {
            logger.log_events("Error", "Invalid voltage reading!");
            continue;
        }
        if (max_voltage == min_voltage) {
            logger.log_events("Error", "max_voltage and min_voltage are equal! Skipping setpoint calculation.");
            continue;
        }

        float m = (max_setpoint - min_setpoint) / (max_voltage - min_voltage);
        if (!std::isfinite(m)) {
            logger.log_events("Error", "m is not finite!");
            continue;
        }
        float b = min_setpoint - (m * min_voltage);
        if (!std::isfinite(b)) {
            logger.log_events("Error", "b is not finite!");
            continue;
        }
        float setpoint_ = m * voltage + b;

        std::lock_guard<std::mutex> lock(setpoint_mutex);
        setpoint = max(min_setpoint, min(max_setpoint, setpoint_));
        // Round to nearest tenth (one decimal place)
        setpoint = round(setpoint * 10.0f) / 10.0f;

        logger.log_events("Debug", "Read Voltage: " + std::to_string(voltage) + " setpoint: " + std::to_string(setpoint) );
        setpoint_mutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
        if (cfg.get("sensor.return") == "0") {
            display_all_variables();
        } else {
            std::thread sensor_thread(update_sensor_thread);
            std::thread setpoint_thread(setpoint_system_thread);
            std::thread display_system(display_system_thread);
            // Wait for the sensor thread to finish (which will happen when running becomes false)
            sensor_thread.join();
            setpoint_thread.join();
            display_system.join();
            cleanup_all();
            logger.clear_old_logs(log_retention_period);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}