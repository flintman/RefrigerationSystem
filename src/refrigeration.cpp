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

#include "refrigeration.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <csignal>
#include <sstream>
#include <algorithm>
#include <future>

time_t last_log_timestamp = time(nullptr) - 400;

void display_all_variables() {
    logger.log_events("Debug", "YOU NEED TO RUN 'sudo tech-tool' to initialize the sensors");
    std::cout << "Logging Interval: " << cfg.get("logging.interval_mins") << " Minutes\n";
    std::cout << "Log Retention Period: " << cfg.get("logging.retention_period") << " days\n";
    std::cout << "UNIT Number: " << cfg.get("unit.number") << "\n";
    std::cout << "Defrost Interval: " << cfg.get("defrost.interval_hours") << " hours\n";
    std::cout << "Defrost Timeout: " << cfg.get("defrost.timeout_mins") << " minutes\n";
    std::cout << "Defrost Coil Temperature: " << cfg.get("defrost.coil_temperature") << "°F\n";
    std::cout << "Temperature Setpoint Offset: " << cfg.get("setpoint.offset") << "°F\n";
    std::cout << "Compressor Off Timer: " << cfg.get("compressor.off_timer") << " minutes\n";
    std::cout << "Debug Code: " << cfg.get("debug.code") << "\n";
    std::cout << "Debug Data Sending: " << (cfg.get("client.enable_send_data") == "1" ? "Enabled" : "Disabled") << "\n";
    std::cout << "return: " << cfg.get("sensor.return") << "\n";
    std::cout << "wifi.enable_hotspot: " << cfg.get("wifi.enable_hotspot") << "\n";
    std::cout << "wifi.hotspot_password: " << cfg.get("wifi.hotspot_password") << "\n";
    std::cout << "client.sent_mins: " << cfg.get("client.sent_mins") << "\n";
    std::cout << "client.ip_address: " << cfg.get("client.ip_address") << "\n";
    std::cout << "coil: " << cfg.get("sensor.coil") << "\n";
    std::cout << "supply: " << cfg.get("sensor.supply") << "\n";
    std::cout << "  HAVE A NICE DAY AND LET ME KNOW IF YOU NEED HELP \n";
}

void check_sensor_status(float return_temp, float supply_temp, float coil_temp) {
    // Check if any sensor readings are out of bounds
    if (return_temp < -50.0f || return_temp > 150.0f) {
        systemAlarm.activateAlarm(1, "2000: Return Sensor Failed.");
        systemAlarm.addAlarmCode(2000);
        logger.log_events("Error", "Return temperature out of bounds");
    }
    if (supply_temp < -50.0f || supply_temp > 150.0f) {
        systemAlarm.activateAlarm(0, "2002: Supply Sensor Failed.");
        systemAlarm.addAlarmCode(2002);
        logger.log_events("Error", "Supply temperature out of bounds");
    }
    if (coil_temp < -50.0f || coil_temp > 150.0f) {
        systemAlarm.activateAlarm(1, "2001: Coil Sensor Failed.");
        systemAlarm.addAlarmCode(2001);
        logger.log_events("Error", "Coil temperature out of bounds");
    }
}

void update_sensor_thread() {
    using namespace std::chrono;
    std::this_thread::sleep_for(milliseconds(500)); // Wait for system to load

    while (running) {
        float local_return_temp, local_supply_temp, local_coil_temp, local_setpoint;
        std::map<std::string, std::string> local_status;
        if (demo_mode) {
            demo.setStatus(status["status"]);        // Only update local_setpoint if not in setpoint mode
            if (!setpointMode) {
                demo.setSetpoint(setpoint.load());
            }
            demo.update();
            return_temp = std::round(demo.readReturnTemp() * 10.0f) / 10.0f;
            supply_temp = std::round(demo.readSupplyTemp() * 10.0f) / 10.0f;
            coil_temp   = std::round(demo.readCoilTemp()   * 10.0f) / 10.0f;
        } else {
            return_temp = sensors.readSensor(cfg.get("sensor.return"));
            supply_temp = sensors.readSensor(cfg.get("sensor.supply"));
            coil_temp   = sensors.readSensor(cfg.get("sensor.coil"));
        }
        local_return_temp = return_temp;
        local_supply_temp = supply_temp;
        local_coil_temp   = coil_temp;
        // Only update local_setpoint if not in setpoint mode
        if (!setpointMode) {
            local_setpoint = setpoint.load();
        }

        {
            std::lock_guard<std::mutex> lock(status_mutex);
            local_status = status;
        }
        check_sensor_status(local_return_temp, local_supply_temp, local_coil_temp);
        if(!systemAlarm.getShutdownStatus()){
            refrigeration_system(local_return_temp, local_supply_temp, local_coil_temp, local_setpoint);
        }

        time_t current_time = time(nullptr);
        if (current_time - last_log_timestamp >= static_cast<time_t>(log_interval)) {
            logger.log_conditions(setpoint, return_temp, coil_temp, supply_temp, local_status);
            last_log_timestamp = time(nullptr);
        }

        std::this_thread::sleep_for(milliseconds(1000));
    }
    if(cfg.get("unit.relay_active_low") == "0") {
        // Set all outputs to OFF for normally closed relays
        gpio.write("fan_pin", false);
        gpio.write("compressor_pin", false);
        gpio.write("valve_pin", false);
        gpio.write("electric_heater_pin", false);
    } else {
        // Set all outputs to ON for normally open relays
        gpio.write("fan_pin", true);
        gpio.write("compressor_pin", true);
        gpio.write("valve_pin", true);
        gpio.write("electric_heater_pin", true);
    }
    std::this_thread::sleep_for(milliseconds(100)); // Give time for GPIO to settle
    logger.log_events("Debug", "Sensor thread stopped");
}

void null_mode() {
    compressor_last_stop_time = time(nullptr);
    state_timer = time(nullptr);
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status["status"] = "Null";
        status["compressor"] = "False";
        status["fan"] = "False";
        status["valve"] = "False";
        status["electric_heater"] = "False";
        logger.log_events("Debug", "System Status: " + status["status"]);
    }
    update_gpio_from_status();
}

void cooling_mode() {
    state_timer = time(nullptr);
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
    state_timer = time(nullptr);
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
    state_timer = time(nullptr);
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

void alarm_mode() {
    state_timer = time(nullptr);
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status["status"] = "Alarm";
        status["compressor"] = "False";
        status["fan"] = "False";
        status["valve"] = "False";
        status["electric_heater"] = "False";
        logger.log_events("Debug", "System Status: " + status["status"]);
    }
    update_gpio_from_status();
}

void update_gpio_from_status() {
    std::lock_guard<std::mutex> lock(status_mutex);
    if(cfg.get("unit.fan_continuous") == "1" && status["status"] != "Alarm" && status["status"] != "Defrost") {
        status["fan"] = "True"; // Force fan to be ON in continuous mode
    }
    bool relayNO = (cfg.get("unit.relay_active_low") != "0");
    gpio.write("fan_pin", relayNO ? (status["fan"] == "False") : (status["fan"] == "True"));
    gpio.write("compressor_pin", relayNO ? (status["compressor"] == "False") : (status["compressor"] == "True"));
    gpio.write("valve_pin", relayNO ? (status["valve"] == "False") : (status["valve"] == "True"));
    if (unit_has_electric_heater) {
        gpio.write("electric_heater_pin", relayNO ? (status["electric_heater"] == "False") : (status["electric_heater"] == "True"));
    } else {
        logger.log_events("Debug", "Electric heater not configured, skipping GPIO update for electric_heater_pin");
    }
    update_compressor_on_time(status["compressor"]);
}

void refrigeration_system(float return_temp_, float supply_temp_, float coil_temp_, float setpoint_) {
    std::string status_;
    time_t current_time = time(nullptr);
    int off_timer_value = stoi(cfg.get("compressor.off_timer")) * 60;
    int setpoint_offset = stoi(cfg.get("setpoint.offset"));
    int defrost_coil_temperature = stoi(cfg.get("defrost.coil_temperature"));
    int defrost_timeout = stoi(cfg.get("defrost.timeout_mins")) * 60;
    int defrost_intervals = (stoi(cfg.get("defrost.interval_hours")) * 60) * 60;
    bool defrost_timed_out = false;

    {
        std::lock_guard<std::mutex> lock(status_mutex);
        status_ = status["status"];
    }

    if(!pretrip_enable){
        if (status_ == "Cooling" && return_temp_ <= setpoint_) {
            null_mode();
        }

        if (status_ == "Heating" && return_temp_ >= setpoint_) {
            null_mode();
        }

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
                if(!anti_timer) {
                    logger.log_events("Debug", "Inside AntiCycle");
                    anti_timer = true;
                }
            }
        }

        if (status_ == "Defrost") {
            defrost_timed_out = (current_time - defrost_start_time) > defrost_timeout;
            if ((coil_temp_ > defrost_coil_temperature) || defrost_timed_out) {
                null_mode();
                defrost_last_time = time(nullptr);
                defrost_start_time = 0;
            }
        }

        if(defrost_timed_out){
            systemAlarm.activateAlarm(0, "1004: Defrost timed out.");
            systemAlarm.addAlarmCode(1004);
            defrost_timed_out = false;
        }

        if (coil_temp_ < defrost_coil_temperature) {
            if (((current_time - defrost_last_time) > defrost_intervals) || trigger_defrost) {
                if (defrost_start_time == 0) {
                    defrost_mode();
                }
            }
        }
        trigger_defrost = false;
    } else {
        pretrip_mode();
        return;
    }
}

void update_compressor_on_time(const std::string& new_status) {
    if (last_compressor_status == "False" && new_status == "True") {
        // Compressor just turned ON
        compressor_on_start_time = time(nullptr);
    } else if (last_compressor_status == "True" && new_status == "False") {
        // Compressor just turned OFF
        time_t now = time(nullptr);
        compressor_on_total_seconds += (now - compressor_on_start_time);
        cfg.set("unit.compressor_run_seconds", std::to_string(compressor_on_total_seconds));
        cfg.save();
        compressor_on_start_time = 0;
    }
    last_compressor_status = new_status;
}

void display_system_thread() {
    float return_temp_;
    float supply_temp_;
    float coil_temp_;
    std::string status_;
    float setpoint_;
    display1.initiate();
    display2.initiate();

    while (running) {
        return_temp_ = return_temp;
        supply_temp_ = supply_temp;
        coil_temp_ = coil_temp;
        setpoint_ = setpoint.load();
        time_t state_duration = time(nullptr) - state_timer;
        int hours = static_cast<int>(state_duration / 3600);
        int minutes = static_cast<int>((state_duration % 3600) / 60);
        int seconds = static_cast<int>(state_duration % 60);
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            status_ = status["status"];
        }

        if(pretrip_enable){
            status_ = "P-" + status_;
        }

        try {
            if (anti_timer) {
                display1.display("Status: " + status_ + " AC", 0);
            } else {
                display1.display("Status: " + status_, 0);
            }
            std::stringstream ss;
            if (setpointMode) {
                static bool flash = false;
                flash = !flash;
                if (flash) {
                    ss << "Setpoint = " << setpoint_;
                } else {
                    ss << "Setpoint =       "; // Blank for flashing effect
                }
            } else {
                ss << "SP: " << setpoint_ << " RT: " << return_temp_;
            }
            display1.display(ss.str(), 1);

            ss.str("");
            ss << "CT: " << coil_temp_ << " DT: " << supply_temp_;
            display1.display(ss.str(), 2);

            // Display alarm codes if any
            auto codes = systemAlarm.getAlarmCodes();
            if (!codes.empty()) {
                ss.str("");
                ss << "Alarms: ";
                for (int code : codes) {
                    ss << code << " ";
                }
                display1.display(ss.str(), 3);
            } else {
                display1.display("Normal", 3);
            }

            ss.str("");
            ss << "       " << (hours < 10 ? "0" : "") << hours << ":"
               << (minutes < 10 ? "0" : "") << minutes << ":"
               << (seconds < 10 ? "0" : "") << seconds;
            display2.display("Status: " + status_, 0);
            display2.display(ss.str(), 1);
            display2.display("IP:" + wifi_manager.get_ip_address("wlan0"), 2);
            std::string ap_ip = wifi_manager.get_ip_address("wlan0_ap");
            if (ap_ip == "xxx.xxx.xxx.xxx") {
                // Display compressor run seconds as HH:MM
                int run_seconds = 0;
                try {
                    run_seconds = std::stoi(cfg.get("unit.compressor_run_seconds"));
                } catch (...) {
                    run_seconds = 0;
                }
                int ch = run_seconds / 3600;
                int cm = (run_seconds % 3600) / 60;
                std::stringstream css;
                css << "Run Hours: ";
                css << (ch < 10 ? "0" : "") << ch << ":"
                    << (cm < 10 ? "0" : "") << cm;
                display2.display(css.str(), 3);
            } else {
                display2.display("HP:" + ap_ip, 3);
            }

        } catch (const std::exception& e) {
            logger.log_events("Error", std::string("During display updating: ") + e.what());
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    display1.clear();
    display2.clear();
    display1.backlight(false);
    display2.backlight(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger.log_events("Debug", "Display system thread stopped");
}

void setpoint_system_buttons(float min_setpoint, float max_setpoint) {

    // Setpoint button mode logic
    static float setpointStart = setpoint.load();
    static time_t setpointModeStart = 0;
    static time_t setpointPressedDuration = 0;
    static time_t button_press_start = 0;
    logger.log_events("Debug", "Running Buttons!");

    while (running) {
        bool up_pressed = gpio.read("up_button_pin");
        bool down_pressed = gpio.read("down_button_pin");

        // Only enter setpoint mode if BOTH buttons are pressed for 2 seconds
        if (!setpointMode && (up_pressed || down_pressed)) {
            if (button_press_start == 0) {
                button_press_start = time(nullptr);
            }
            if (time(nullptr) - button_press_start >= 2) {
                setpointMode = true;
                setpointStart = setpoint.load();
                setpointModeStart = time(nullptr);
                setpointPressedDuration = time(nullptr);
                logger.log_events("Debug", "Setpoint button mode entered");
                button_press_start = 0;
            }
        } else {
            button_press_start = 0;
        }

        if (setpointMode) {
            float current_setpoint = setpoint.load();
            float step = 1.0f;
            // If held for more than 4 seconds, jump by 5°F
            if ((setpointPressedDuration != 0) && (time(nullptr) - setpointPressedDuration) >= 4) {
                step = 5.0f;
            }

            if (up_pressed && !down_pressed) {
                // Up
                setpoint = std::min(current_setpoint + step, max_setpoint);
                setpointModeStart = time(nullptr); // Reset timer on press
            } else if (down_pressed && !up_pressed) {
                // Down
                setpoint = std::max(current_setpoint - step, min_setpoint);
                setpointModeStart = time(nullptr); // Reset timer on press
            } else {
                setpointPressedDuration = time(nullptr);
                // If no button pressed for 10 seconds, exit without saving
                if (setpointModeStart != 0 && (time(nullptr) - setpointModeStart) >= 10) {
                    setpointMode = false;
                    setpoint = setpointStart;
                    logger.log_events("Debug", "Setpoint mode exited due to inactivity (no save)");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

void setpoint_system_thread() {
    float min_setpoint = -20.0f;
    float max_setpoint = 80.0f;

    try {
        min_setpoint = std::stof(cfg.get("setpoint.min"));
    } catch (...) {
        min_setpoint = -20.0f;
    }
    try {
        max_setpoint = std::stof(cfg.get("setpoint.max"));
    } catch (...) {
        max_setpoint = 80.0f;
    }
    setpoint_system_buttons(min_setpoint, max_setpoint);
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
                    ws2811.setLED(0, 0, 255, 0);
                    ws2811.setLED(1, 255, 255, 0);
                } else {
                    ws2811.setLED(0, 255, 255, 0);
                    ws2811.setLED(1, 0, 255, 0);
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
                if(!systemAlarm.getWarningStatus()) {
                    ws2811.setLED(0, 255, 0, 0); // Green when not in alarm
                } else {
                    ws2811.setLED(0, 255, 255, 0); // Yellow when warning alarm
                }
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

            int setpoint_int = static_cast<int>(setpoint.load());
            logger.log_events("Debug", "Defrost Button released in " + std::to_string(press_duration) + "  setpoint_int: " + std::to_string(setpoint_int));
            if (press_duration >= 5 && setpoint_int == 65) {
                if(!pretrip_enable) {
                    pretrip_enable = true;
                    logger.log_events("Debug", "Entering Pretrip Mode");
                }
            } else if (press_duration >= 5 && setpoint_int == 80) {
                if (demo_mode) {
                    demo_mode = false;
                    logger.log_events("Debug", "Leaving Demo Mode");
                } else {
                    demo_mode = true;
                    logger.log_events("Debug", "Entering Demo Mode");
                }
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
            if(setpointMode){
                // Save and exit setpoint mode
                cfg.set("unit.setpoint", std::to_string(static_cast<int>(setpoint.load())));
                cfg.save();
                setpointMode = false;
                logger.log_events("Debug", "Setpoint saved and button mode exited");
            }
            logger.log_events("Debug", "Alarm Button Pushed");
            alarm_reset_button_press_start_time = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        }
    } else {
        if (alarm_reset_button_press_start_time != 0) {
            double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            double press_duration = now - alarm_reset_button_press_start_time;
            int setpoint_int = static_cast<int>(setpoint.load());
            alarm_reset_button_press_start_time = 0;
            if (press_duration >= 10 && setpoint_int == 65) {
                if (!wifi_manager.is_hotspot_active()) {
                    std::thread hotspot_system(hotspot_start);
                    hotspot_system.detach(); // Run in background
                } else {
                    logger.log_events("Debug", "Hotspot already active, not starting again.");
                }
                logger.log_events("Debug", "HotSpot started ");
            }
            if (press_duration >= 5 && setpoint_int != 65) {
                if (systemAlarm.alarmAnyStatus()) {
                    logger.log_events("Debug", "Alarm Reset ");
                    systemAlarm.resetAlarm();
                } else {
                    logger.log_events("Debug", "Alarm Reset Button pressed but no active alarms to reset.");
                }
            }
        }
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

void hotspot_start() {
    bool enable_hotspot_loop = false;
    int enable_hotspot = stoi(cfg.get("wifi.enable_hotspot"));
    std::string ssid = "REFRIGERATION-" + cfg.get("unit.number");
    std::string hotspot_password = cfg.get("wifi.hotspot_password");
    if (enable_hotspot == 1) {
        wifi_manager.set_credentials(ssid, hotspot_password);
        wifi_manager.start_hotspot();
        logger.log_events("Debug", "Hotspot started. Checking for clients...");
        enable_hotspot_loop = true;
    }

    int no_client_duration = 0;
    const int check_interval = 10; // seconds
    bool have_clients = false;

    while (enable_hotspot_loop) {
        auto clients = wifi_manager.check_hotspot_clients();
        if (!clients.empty()) {
            logger.log_events("Debug", "Clients connected to the hotspot. Waiting...");
            no_client_duration = 0;
            have_clients = true;
        } else {
            no_client_duration += check_interval;
            have_clients = false;
            logger.log_events("Debug", "No clients connected for " + std::to_string(no_client_duration) + " seconds.");

            if (no_client_duration >= 120) {
                logger.log_events("Debug", "No clients for 2 minutes. Stopping hotspot");
                break;
            }
        }

        // If Ctrl+C is pressed and no clients are connected, stop the hotspot
        if (!running) {
            logger.log_events("Debug", "HOTSPOT: Ctrl+C detected.");
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(check_interval));
    }

    if (enable_hotspot_loop && !have_clients) {
        logger.log_events("Debug", "Stopping hotspot.");
        wifi_manager.stop_hotspot();
    }
}

void checkAlarms_system(){
    std::string status_;
    float return_temp_;
    float supply_temp_;
    static bool sent_alarm_status = false;

    while(running){
        return_temp_ = return_temp;
        supply_temp_ = supply_temp;
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            status_ = status["status"];
        }
        if (status_== "Cooling"){
            systemAlarm.coolingAlarm(return_temp_, supply_temp_, 5.0f);
        } else if(status_ == "Heating"){
            systemAlarm.heatingAlarm(return_temp_, supply_temp_, 5.0f);
        } else {
            systemAlarm.clearTimers();
        }
        if (systemAlarm.getShutdownStatus()) {
            if (status_ != "Alarm") {
                alarm_mode();
                if (!sent_alarm_status && cfg.get("client.enable_send_data") == "1") {
                    logger.log_events("Debug", "Alarm detected, Sending Data to the site.");
                    bool resend = secure_client_send(); // Send alarm status to server
                    sent_alarm_status = true;
                }
            }
        } else {
            if (status_ == "Alarm") {
                sent_alarm_status = false;
                null_mode();
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void secureclient_loop() {
    int secureclient_timer = std::stoi(cfg.get("client.sent_mins")) * 60; // Convert minutes to seconds
    bool resend = false;
    std::this_thread::sleep_for(std::chrono::seconds(10)); // Wait for system to load

    while (running) {
        if (cfg.get("client.enable_send_data") == "1") {
            bool stuck = false;
            std::future<bool> future_send = std::async(std::launch::async, secure_client_send);
            // Wait for up to 15 seconds for secure_client_send to finish
            if (future_send.wait_for(std::chrono::seconds(15)) == std::future_status::ready) {
                resend = future_send.get();
            } else {
                logger.log_events("Error", "secure_client_send appears stuck, skipping get() and will retry later.");
                stuck = true;
            }

            if (!stuck && resend) {
                logger.log_events("Debug", "Resending data due to command received.");
                interruptible_sleep(10); // Wait before next send
                // Try again, with timeout
                std::future<bool> future_resend = std::async(std::launch::async, secure_client_send);
                if (future_resend.wait_for(std::chrono::seconds(15)) == std::future_status::ready) {
                    future_resend.get();
                } else {
                    logger.log_events("Error", "secure_client_send (resend) stuck, skipping.");
                }
            } else if (!stuck) {
                logger.log_events("Debug", "Data sent successfully, no command received.");
            }
        } else {
            logger.log_events("Debug", "Data sending is disabled. Skipping secure client send.");
        }
        interruptible_sleep(secureclient_timer);
    }
}

bool secure_client_send() { // returns if we need to resend
    float return_temp_;
    float supply_temp_;
    float coil_temp_;
    std::string status_;
    std::string status_compresor;
    std::string status_fan;
    std::string status_valve;
    std::string status_electric_heater;
    bool resend = false;
    float setpoint_;

    int secureclient_timer = std::stoi(cfg.get("client.sent_mins")) * 60; // Convert minutes to seconds
    std::map<std::string, std::string> command;

    // Protect all shared/global variables with mutexes
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        return_temp_ = return_temp;
        supply_temp_ = supply_temp;
        coil_temp_ = coil_temp;
        setpoint_ = setpoint.load();
        status_ = status["status"];
        status_compresor = status["compressor"];
        status_fan = status["fan"];
        status_valve = status["valve"];
        status_electric_heater = status["electric_heater"];
    }

    try {
        // Get timestamp
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S  %m:%d:%Y", std::localtime(&now_c));

        // Convert alarm codes to comma-separated string
        auto codes = systemAlarm.getAlarmCodes();
        std::ostringstream codes_ss;
        for (size_t i = 0; i < codes.size(); ++i) {
            codes_ss << codes[i];
            if (i + 1 < codes.size()) codes_ss << ",";
        }

        std::ostringstream setpoint_ss, return_ss, supply_ss, coil_ss;
        setpoint_ss << std::fixed << std::setprecision(0) << setpoint_;
        return_ss   << std::fixed << std::setprecision(1) << return_temp_;
        supply_ss   << std::fixed << std::setprecision(1) << supply_temp_;
        coil_ss     << std::fixed << std::setprecision(1) << coil_temp_;

        std::map<std::string, std::string> array = {
            {"timestamp", timestamp},
            {"unit", cfg.get("unit.number")},
            {"alarm_codes", codes_ss.str()},
            {"setpoint", setpoint_ss.str()},
            {"status", status_},
            {"compressor", status_compresor},
            {"fan", status_fan},
            {"valve", status_valve},
            {"electric_heater", status_electric_heater},
            {"return_temp", return_ss.str()},
            {"supply_temp", supply_ss.str()},
            {"coil_temp", coil_ss.str()}
        };

        if (wifi_manager.is_connected()) {
            try {
                secure_client.connect();
                command = secure_client.send_and_receive(array);
                logger.log_events("Debug", "Sent data, response received.");
            } catch (const std::exception& e) {
                logger.log_events("Error", std::string("Error in send/receive: ") + e.what());
            }
        } else {
            logger.log_events("Debug", "No active internet connection. Function execution skipped.");
        }
    } catch (const std::exception& e) {
        logger.log_events("Error", std::string("secureclient_loop encountered an error: ") + e.what());
    }

    // Handle command
    if (!command.empty()) {
        if (command["status"] == "alarm_reset") {
            resend = true;
            systemAlarm.resetAlarm();
        } else if (command["status"] == "defrost") {
            resend = true;
            trigger_defrost = true;
        }
    }
    return resend;
}

void pretrip_mode() {
    // If just entered pretrip mode, initialize
    if (pretrip_stage == 0) {
        logger.log_events("Debug", "Starting Pretrip Mode");
        pretrip_stage = 1;
        pretrip_stage_start = time(nullptr);
        cooling_mode();
        logger.log_events("Debug", "Pretrip: Cooling for 10 minutes");
        return;
    }

    time_t now = time(nullptr);

    switch (pretrip_stage) {
        case 1: // Cooling for 10 minutes
            if (return_temp >= (coil_temp + 4.0f)) {
                logger.log_events("Debug", "Pretrip: Cooling confirmed");
                pretrip_stage = 2;
                pretrip_stage_start = now;
                heating_mode();
                logger.log_events("Debug", "Pretrip: Heating for 10 minutes");
            } else if (now - pretrip_stage_start >= 600) {
                systemAlarm.activateAlarm(1, "9001: Pretrip Cooling Failed.");
                systemAlarm.addAlarmCode(9001);
                logger.log_events("Debug", "Pretrip: Cooling timeout reached");
                pretrip_stage = 4;
                pretrip_stage_start = now;
            }
            return;
        case 2: // Heating for 10 minutes
            if (systemAlarm.alarmAnyStatus()) {
                logger.log_events("Debug", "Pretrip: Alarm status detected");
                pretrip_stage = 4;
                pretrip_stage_start = now;
            } else if (return_temp <= (coil_temp - 4.0f)) {
                logger.log_events("Debug", "Pretrip: Heating confirmed");
                pretrip_stage = 3;
                pretrip_stage_start = now;
                cooling_mode();
                logger.log_events("Debug", "Pretrip: Cooling for 5 minutes");
            } else if (now - pretrip_stage_start >= 600) {
                systemAlarm.activateAlarm(1, "9002: Pretrip Heating Failed.");
                systemAlarm.addAlarmCode(9002);
                logger.log_events("Debug", "Pretrip: Heating timeout reached");
                pretrip_stage = 4;
                pretrip_stage_start = now;
            }
            return;
        case 3: // Cooling for 5 minutes
            if (systemAlarm.alarmAnyStatus()) {
                logger.log_events("Debug", "Pretrip: Alarm status detected");
                pretrip_stage = 4;
                pretrip_stage_start = now;
            } else if (return_temp >= (coil_temp + 4.0f)) {
                logger.log_events("Debug", "Pretrip: Cooling confirmed (final)");
                pretrip_stage = 4;
                pretrip_stage_start = now;
            } else if (now - pretrip_stage_start >= 300) {
                systemAlarm.activateAlarm(1, "9003: Pretrip Cooling Failed 2nd time.");
                systemAlarm.addAlarmCode(9003);
                logger.log_events("Debug", "Pretrip: 2nd Cooling timeout reached");
                pretrip_stage = 4;
                pretrip_stage_start = now;
            }
            return;
        case 4: // Done
            null_mode();
            pretrip_enable = false;
            pretrip_stage = 0;
            systemAlarm.activateAlarm(0, "9000: Pretrip Completed successfully.");
            systemAlarm.addAlarmCode(9000);
            logger.log_events("Debug", "Pretrip: Completed");
            return;
    }
}

void interruptible_sleep(int total_seconds) {
    for (int i = 0; i < total_seconds && running; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void signalHandler(int signal) {
    if (signal == SIGINT) {
        running = false;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);

    if (geteuid() != 0) {
        logger.log_events("Debug", "This tool must be run as root (sudo).");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "demo_mode=true" || arg == "--demo" || arg == "-d") {
            demo_mode = true;
            logger.log_events("Debug", "Demo mode enabled!");
        }
    }

    logger.log_events("Debug", "Welcome to the Refrigeration system");
    logger.log_events("Debug", "The system is starting up please wait");
    logger.log_events("Debug", "Press Ctrl+C to exit gracefully");
    logger.log_events("Debug", "Version: " + version);
    logger.log_events("Debug", "System started up");

    try {
        if (cfg.get("sensor.return") == "0") {
            display_all_variables();
            std::thread hotspot_system(hotspot_start);
            hotspot_system.join();
            running = false; // Stop all threads if any were started elsewhere
            logger.log_events("Debug", "Exiting because sensors are not initialized.");
            // If running as a systemd service, request stop:
            system("systemctl stop refrigeration.service");
            return 0;
        } else {
            // Thread wrappers and monitoring
            auto start_thread = [](std::function<void()> func, const std::string& name) -> std::thread {
                return std::thread([func, name]() {
                    while (running) {
                        try {
                            func();
                            // If function returns, log and restart unless running is false
                            if (running) {
                                logger.log_events("Error", name + " thread exited unexpectedly, restarting...");
                            }
                        } catch (const std::exception& e) {
                            logger.log_events("Error", name + " thread exception: " + e.what());
                        }
                        if (name == "hotspot_system") break; // Don't restart hotspot thread
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                });
            };

            std::thread refrigeration_thread = start_thread(update_sensor_thread, "refrigeration_thread");
            std::thread setpoint_thread = start_thread(setpoint_system_thread, "setpoint_thread");
            std::thread display_system = start_thread(display_system_thread, "display_system_thread");
            std::thread ws8211_system = start_thread(ws8211_system_thread, "ws8211_system_thread");
            std::thread button_system = start_thread(button_system_thread, "button_system_thread");
            std::thread hotspot_system(hotspot_start); // Hotspot: do not restart
            std::thread alarm_system = start_thread(checkAlarms_system, "alarm_system_thread");
            std::thread secureclient_system = start_thread(secureclient_loop, "secureclient_system_thread");

            refrigeration_thread.join();
            setpoint_thread.join();
            display_system.join();
            ws8211_system.join();
            button_system.join();
            hotspot_system.join();
            alarm_system.join();
            secureclient_system.join();

            logger.clear_old_logs(log_retention_period);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}