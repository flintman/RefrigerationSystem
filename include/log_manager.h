#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <map>
#include <mutex>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

class Logger {
public:
    Logger(int debug);
    
    void clear_old_logs(int days = 30);
    void log_conditions(float setpoint, float return_sensor, float coil_sensor,
                       float supply_sensor, const std::map<std::string, std::string>& systems_status);
    void log_events(const std::string& event_type, const std::string& event_message);

private:
    int log_interval;
    int debug_code;
    std::string log_folder;
    std::mutex log_mutex;

    std::string get_current_datetime();
    std::string get_current_date();
    std::string get_log_filename(const std::string& base_name, const std::string& date = "");
};

#endif // LOGGER_H