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

#include "log_manager.h"

namespace fs = std::filesystem;

Logger::Logger(int debug)
    : debug_code(debug), log_folder("/var/log/refrigeration") {
    fs::create_directories(log_folder);
}

Logger::~Logger() {
}

int Logger::acquire_file_lock(const std::string& lock_file_path) {
    try {
        // Open or create the lock file
        int fd = open(lock_file_path.c_str(), O_CREAT | O_WRONLY, 0644);
        if (fd == -1) {
            std::cerr << "Failed to open lock file: " << lock_file_path << std::endl;
            return -1;
        }

        // Acquire exclusive lock (blocks if another process has it)
        if (flock(fd, LOCK_EX) == -1) {
            std::cerr << "Failed to acquire lock on: " << lock_file_path << std::endl;
            close(fd);
            return -1;
        }

        return fd;
    } catch (const std::exception& e) {
        std::cerr << "Error acquiring file lock: " << e.what() << std::endl;
        return -1;
    }
}

void Logger::release_file_lock(int lock_fd) {
    if (lock_fd != -1) {
        try {
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
        } catch (const std::exception& e) {
            std::cerr << "Error releasing file lock: " << e.what() << std::endl;
        }
    }
}

void Logger::log_to_file(const std::string& log_file_path, const std::string& log_line) {
    try {
        std::lock_guard<std::mutex> lock(log_mutex);

        // Create lock file path
        std::string lock_file_path_full = log_file_path + ".lock";

        // Acquire exclusive lock across processes
        int lock_fd = acquire_file_lock(lock_file_path_full);
        if (lock_fd == -1) {
            std::cerr << "Could not acquire lock, writing without exclusive access" << std::endl;
        }

        // Now write to the log file
        std::ofstream log_file(log_file_path, std::ios::app);
        if (log_file.is_open()) {
            log_file << log_line;
            log_file.close();
        } else {
            std::cerr << "Failed to open log file: " << log_file_path << std::endl;
        }

        // Release lock immediately after writing
        if (lock_fd != -1) {
            release_file_lock(lock_fd);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in log_to_file: " << e.what() << std::endl;
    }
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

            // Skip lock files
            std::string filename = entry.path().filename().string();
            if (filename.find(".lock") != std::string::npos) continue;

            auto file_mtime = fs::last_write_time(entry);
            auto file_mtime_sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                file_mtime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

            if (file_mtime_sys < cutoff_time) {
                try {
                    // Try to acquire lock before deleting (non-blocking)
                    std::string lock_file = entry.path().string() + ".lock";
                    int lock_fd = open(lock_file.c_str(), O_CREAT | O_WRONLY, 0644);

                    if (lock_fd != -1) {
                        // Try non-blocking lock
                        if (flock(lock_fd, LOCK_EX | LOCK_NB) == 0) {
                            // Lock acquired, safe to delete
                            fs::remove(entry.path());
                            fs::remove(lock_file);
                            std::cout << "Deleted: " << entry.path() << std::endl;
                            flock(lock_fd, LOCK_UN);
                        } else {
                            std::cout << "File still in use, skipping: " << entry.path() << std::endl;
                        }
                        close(lock_fd);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error deleting file " << entry.path() << ": " << e.what() << std::endl;
                }
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
        std::string log_file_path = get_log_filename("conditions");
        std::string log_line = get_current_datetime() + " - "
                 + "Setpoint: " + std::to_string(setpoint) + ", Return Sensor: " + std::to_string(return_sensor) + ", "
                 + "Coil Sensor: " + std::to_string(coil_sensor) + ", Supply: " + std::to_string(supply_sensor) + ", "
                 + "Status: " + systems_status.at("status") + ", "
                 + "Compressor: " + systems_status.at("compressor") + ", "
                 + "Fan: " + systems_status.at("fan") + ", "
                 + "Valve: " + systems_status.at("valve") + ", "
                 + "Electric_heater: " + (systems_status.count("electric_heater") ? systems_status.at("electric_heater") : "N/A");

        log_to_file(log_file_path, log_line + "\n");
        log_events("Info", log_line);
    } catch (const std::exception& e) {
        std::cerr << "Error logging conditions: " << e.what() << std::endl;
    }
}

void Logger::log_events(const std::string& event_type, const std::string& event_message) {
    try {
        if (event_type == "Error" || event_type == "Info" || (event_type == "Debug" && debug_code == 1)) {
            std::string log_file_path = get_log_filename("events");
            std::string timestamp = get_current_datetime();
            std::string log_line = "[" + timestamp + "] " + event_type + "] " + event_message + "\n";
            std::cout << log_line;
            log_to_file(log_file_path, log_line);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error logging events: " << e.what() << std::endl;
    }
}
