#include "refrigeration.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <csignal>
#include <sstream>
#include <algorithm>

void display_all_variables() {
    logger.log_events("Debug", "YOU NEED TO RUN config_editor to initialize the sensors");
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

void update_sensor_thread() {
    using namespace std::chrono;
    std::this_thread::sleep_for(milliseconds(200)); // Wait for system to load

    while (running) {
        float local_return_temp, local_supply_temp, local_coil_temp, local_setpoint;
        std::map<std::string, std::string> local_status;
        return_temp = sensors.readSensor(cfg.get("sensor.return"));
        supply_temp = sensors.readSensor(cfg.get("sensor.supply"));
        coil_temp   = sensors.readSensor(cfg.get("sensor.coil"));
        local_return_temp = return_temp;
        local_supply_temp = supply_temp;
        local_coil_temp   = coil_temp;
        local_setpoint    = setpoint.load();

        {
            std::lock_guard<std::mutex> lock(status_mutex);
            local_status = status;
        }

        refrigeration_system(local_return_temp, local_supply_temp, local_coil_temp, local_setpoint);

        time_t current_time = time(nullptr);
        if (current_time - last_log_timestamp >= static_cast<time_t>(log_interval)) {
            logger.log_conditions(setpoint, return_temp, coil_temp, supply_temp, local_status);
            last_log_timestamp = time(nullptr);
        }

        std::this_thread::sleep_for(milliseconds(100));
    }
}

void null_mode() {
    compressor_last_stop_time = time(nullptr);
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status["status"] = "Null";
        status["compressor"] = "False";
        status["fan"] = "True";
        status["valve"] = "False";
        status["electric_heater"] = "False";
        logger.log_events("Debug", "System Status: " + status["status"]);
    }
    update_gpio_from_status();
}

void cooling_mode() {
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status["status"] = "Cooling";
        status["compressor"] = "True";
        status["fan"] = "True";
        status["valve"] = "False";
        status["electric_heater"] = "False";
        logger.log_events("Debug", "System Status: " + status["status"]);
    }
    update_gpio_from_status();
}

void heating_mode() {
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status["status"] = "Heating";
        status["compressor"] = "True";
        status["fan"] = "True";
        status["valve"] = "True";
        status["electric_heater"] = "True";
        logger.log_events("Debug", "System Status: " + status["status"]);
    }
    update_gpio_from_status();
}

void defrost_mode() {
    defrost_start_time  = time(nullptr);

   {
        std::lock_guard<std::mutex> lock(status_mutex);
        status["status"] = "Defrost";
        status["compressor"] = "True";
        status["fan"] = "False";
        status["valve"] = "True";
        status["electric_heater"] = "True";
        logger.log_events("Debug", "System Status: " + status["status"]);
    }
    update_gpio_from_status();
}

void update_gpio_from_status() {
    std::lock_guard<std::mutex> lock(status_mutex);
    gpio.write("fan_pin", status["fan"] == "False");
    gpio.write("compressor_pin", status["compressor"] == "False");
    gpio.write("valve_pin", status["valve"] == "False");
    gpio.write("electric_heater_pin", status["electric_heater"] == "False");
}

void refrigeration_system(float return_temp_, float supply_temp_, float coil_temp_, float setpoint_) {
    std::string status_;
    time_t current_time = time(nullptr);
    int off_timer_value = stoi(cfg.get("compressor.off_timer")) * 60;
    int setpoint_offset = stoi(cfg.get("setpoint.offset"));
    int defrost_coil_temperature = stoi(cfg.get("defrost.coil_temperature"));
    int defrost_timeout = stoi(cfg.get("defrost.timeout_mins")) * 60;
    int defrost_intervals = (stoi(cfg.get("defrost.interval_hours")) * 60) * 60;

    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status_ = status["status"];
    }

    if (status_ == "Cooling" && return_temp_ <= setpoint_) {
        null_mode();
    }

    if (status_ == "Heating" && return_temp_ >= setpoint_) {
        null_mode();
    }

    {
        if (status_ == "Null") {
            if (current_time - compressor_last_stop_time >= static_cast<time_t>(off_timer_value)) {
                if (return_temp_ >= (setpoint_ + setpoint_offset)) {
                    cooling_mode();
                }
                if (return_temp_ <= (setpoint_ - setpoint_offset)) {
                    heating_mode();
                }
                anti_timer = false;
            } else {
                logger.log_events("Debug", "Inside AntiCycle");
                anti_timer = true;
            }
        }
    }

    if (status_ == "Defrost") {
        if ((coil_temp_ > defrost_coil_temperature) || ((current_time - defrost_last_time) < defrost_intervals)) {
            null_mode();
            defrost_last_time = time(nullptr);
        }
    }

    if (coil_temp_ < defrost_coil_temperature) {
        if (((current_time - defrost_last_time) > defrost_intervals) || trigger_defrost) {
            if (defrost_start_time == 0) {
                trigger_defrost = false;
                defrost_mode();
            }
        }
    }
}

void display_system_thread() {
    float return_temp_;
    float supply_temp_;
    float coil_temp_;
    std::string status_;
    float setpoint_;

    while (running) {
        return_temp_ = return_temp;
        supply_temp_ = supply_temp;
        coil_temp_ = coil_temp;
        setpoint_ = setpoint.load();
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            status_ = status["status"];
        }

        try {
            display1.display("Status: " + status_, 0);
            std::stringstream ss;
            ss << "SP: " << setpoint_ << " RT: " << return_temp_;
            display1.display(ss.str(), 1);

            ss.str("");
            ss << "CT: " << coil_temp_ << " DT: " << supply_temp_;
            display1.display(ss.str(), 2);

            display2.display("Status: " + status_, 0);

        } catch (const std::exception& e) {
            logger.log_events("Error", std::string("During display updating: ") + e.what());
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void setpoint_system_thread() {
    constexpr float min_voltage = 0.00f;
    constexpr float max_voltage = 3.28f;
    constexpr float min_setpoint = -20.0f;
    constexpr float max_setpoint = 80.0f;
    float voltage = 0.0f;

    while (running) {
        try {
            voltage = adc.readVoltage(3);
        } catch (const std::exception& e) {
            logger.log_events("Error", std::string("ADS1115 error: ") + e.what());
        }
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

        setpoint = std::clamp(setpoint_, min_setpoint, max_setpoint);
        setpoint = std::round(setpoint * 10.0f) / 10.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void ws8211_system_thread() {
    std::string status_;
    if (!ws2811.initialize()) {
        logger.log_events("Error", "Failed to initialize WS2811 controller");
        return;
    }

    bool wigwag_toggle = false;
    auto last_wigwag_time = std::chrono::steady_clock::now();

    while (running) {
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            status_ = status["status"];
        }

        try {
            if (status_ == "Alarm") {
                auto now = std::chrono::steady_clock::now();
                if (now - last_wigwag_time >= std::chrono::milliseconds(250)) {
                    wigwag_toggle = !wigwag_toggle;
                    last_wigwag_time = now;
                }

                if (wigwag_toggle) {
                    ws2811.setLED(0, 255, 0, 0);
                    ws2811.setLED(1, 255, 255, 0);
                } else {
                    ws2811.setLED(0, 255, 255, 0);
                    ws2811.setLED(1, 255, 0, 0);
                }
            } else {
                if (status_ == "Cooling") {
                    ws2811.setLED(1, 0, 0, 255); // Blue
                } else if (status_ == "Heating") {
                    ws2811.setLED(1, 0, 255, 0); // Red
                } else if (status_ == "Defrost") {
                    ws2811.setLED(1, 255, 255, 0); // Yellow
                } else {
                    ws2811.setLED(1, 255, 255, 255); // Off
                }
                ws2811.setLED(0, 255, 0, 0); // Green when not in alarm
            }

            if (!ws2811.render()) {
                logger.log_events("Error", "Failed to render WS2811 changes");
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } catch (const std::exception& e) {
            logger.log_events("Error", std::string("During WS2811 operation: ") + e.what());
            break;
        }
    }
    ws2811.clear();
    ws2811.render();
}

void checkDefrostPin() {
    if (gpio.read("defrost_pin")) {
        if (defrost_button_press_start_time == 0) {
            logger.log_events("Debug", "Defrost Button Pushed");
            defrost_button_press_start_time = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        }
    } else {
        if (defrost_button_press_start_time != 0) {
            double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            double press_duration = now - defrost_button_press_start_time;
            defrost_button_press_start_time = 0;

            logger.log_events("Debug", "Defrost Button released in " + std::to_string(press_duration));

            int setpoint_int = static_cast<int>(setpoint.load());
            if (press_duration >= 5 && setpoint_int == 65) {
                logger.log_events("Debug", "Entering Pretrip");
            } else if (press_duration >= 5 && setpoint_int == 80) {
                logger.log_events("Debug", "Leaving Demo Mode");
            } else {
                if (!trigger_defrost) {
                    logger.log_events("Debug", "Defrost pin active");
                    trigger_defrost = true;
                }
            }
        }
    }
}
void checkAlarmPin(){
    if (gpio.read("alarm_pin")) {
        if (alarm_reset_button_press_start_time == 0) {
            logger.log_events("Debug", "Alarm Button Pushed");
            alarm_reset_button_press_start_time = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        } else {
            double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            double press_duration = now - alarm_reset_button_press_start_time;
            if (press_duration >= 5) {
                logger.log_events("Debug", "Alarm Reset ");
            }
        }
    } else {
        alarm_reset_button_press_start_time = 0;
    }
}

void button_system_thread() {
    while (running) {
        try {
            checkDefrostPin();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            checkAlarmPin();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (const std::exception& e) {
            logger.log_events("Error", std::string("During button system thread: ") + e.what());
            break;
        }
    }
}

void signalHandler(int signal) {
    if (signal == SIGINT) {
        running = false;
    }
}

void cleanup_all() {
    logger.log_events("Debug", "Running Cleanup");
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
            std::thread refrigeration_thread(update_sensor_thread);
            std::thread setpoint_thread(setpoint_system_thread);
            std::thread display_system(display_system_thread);
            std::thread ws8211_system(ws8211_system_thread);
            std::thread button_system(button_system_thread);

            refrigeration_thread.join();
            setpoint_thread.join();
            display_system.join();
            ws8211_system.join();
            button_system.join();

            cleanup_all();
            logger.clear_old_logs(log_retention_period);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}