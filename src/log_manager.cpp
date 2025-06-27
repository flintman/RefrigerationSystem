#include "log_manager.h"

namespace fs = std::filesystem;

Logger::Logger(int debug)
    : debug_code(debug), log_folder("/var/log/refrigeration") {
    fs::create_directories(log_folder);
}

std::string Logger::get_current_datetime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&in_time_t, &tm_buf);

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::get_current_date() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&in_time_t, &tm_buf);

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d");
    return ss.str();
}

std::string Logger::get_log_filename(const std::string& base_name, const std::string& date) {
    std::string date_str = date.empty() ? get_current_date() : date;
    return log_folder + "/" + base_name + "-" + date_str + ".log";
}

void Logger::clear_old_logs(int days) {
    try {
        auto now = std::chrono::system_clock::now();
        auto cutoff_time = now - std::chrono::hours(24 * days);

        if (!fs::exists(log_folder)) {
            std::cerr << "Directory '" << log_folder << "' does not exist." << std::endl;
            return;
        }

        for (const auto& entry : fs::directory_iterator(log_folder)) {
            if (!entry.is_regular_file()) continue;

            auto file_mtime = fs::last_write_time(entry);
            auto file_mtime_sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                file_mtime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

            if (file_mtime_sys < cutoff_time) {
                fs::remove(entry.path());
                std::cout << "Deleted: " << entry.path() << std::endl;
            }
        }
        std::cout << "Log cleanup complete." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error during log cleanup: " << e.what() << std::endl;
    }
}

void Logger::log_conditions(float setpoint, float return_sensor, float coil_sensor,
                          float supply_sensor, const std::map<std::string, std::string>& systems_status) {
    try {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::string log_file_path = get_log_filename("conditions");
        std::ofstream log_file(log_file_path, std::ios::app);

        if (log_file.is_open()) {
            log_file << get_current_datetime() << " - "
                     << "Setpoint: " << setpoint << ", Return Sensor: " << return_sensor << ", "
                     << "Coil Sensor: " << coil_sensor << ", Supply: " << supply_sensor << ", "
                     << "Status: " << systems_status.at("status") << ", "
                     << "Compressor: " << systems_status.at("compressor") << ", "
                     << "Fan: " << systems_status.at("fan") << ", "
                     << "Valve: " << systems_status.at("valve") << ", "
                     << "Electric_heater: " << (systems_status.count("electric_heater") ? systems_status.at("electric_heater") : "N/A")
                     << "\n";
            std::cout << "(" << get_current_datetime() << ") " << "Log_condition: "
                     << "Setpoint: " << setpoint << ", Return Sensor: " << return_sensor << ", "
                     << "Coil Sensor: " << coil_sensor << ", Supply: " << supply_sensor << ", "
                     << "Status: " << systems_status.at("status") << ", "
                     << "Compressor: " << systems_status.at("compressor") << ", "
                     << "Fan: " << systems_status.at("fan") << ", "
                     << "Valve: " << systems_status.at("valve") << ", "
                     << "Electric_heater: " << (systems_status.count("electric_heater") ? systems_status.at("electric_heater") : "N/A")
                     <<  std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error logging conditions: " << e.what() << std::endl;
    }
}

void Logger::log_events(const std::string& event_type, const std::string& event_message) {
    try {
        if (event_type == "Error" || (event_type == "Debug" && debug_code == 1)) {
            std::lock_guard<std::mutex> lock(log_mutex);
            std::string log_file_path = get_log_filename("events");
            std::ofstream log_file(log_file_path, std::ios::app);

            if (log_file.is_open()) {
                std::string timestamp = get_current_datetime();
                log_file << "[" << timestamp << "] " << event_type << "] " << event_message << "\n";
                std::cout << "(" << timestamp << ") " << event_type << ": " << event_message << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error logging events: " << e.what() << std::endl;
    }
}
