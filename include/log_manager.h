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
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

class Logger {
public:
    Logger(int debug);
    ~Logger();

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

    // File locking helpers
    int acquire_file_lock(const std::string& lock_file_path);
    void release_file_lock(int lock_fd);
    void log_to_file(const std::string& log_file_path, const std::string& log_line);
};

#endif // LOGGER_H