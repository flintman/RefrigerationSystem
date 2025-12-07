#include "tools/temperature_data_table.h"
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <fstream>

bool TemperatureDataTable::ParseConditionLine(const std::string& line, ConditionDataPoint& point) {
    // Format: 2025-12-02 06:07:23 - Setpoint: 55.000000, Return Sensor: 61.799999, Coil Sensor: 60.700001, Supply: 62.900002, ...
    std::istringstream iss(line);
    std::string date, time_str, dash;
    if (!(iss >> date >> time_str >> dash)) return false;

    struct tm tm_info = {};
    if (!strptime((date + " " + time_str).c_str(), "%Y-%m-%d %H:%M:%S", &tm_info)) return false;
    point.timestamp = mktime(&tm_info);

    std::string token;
    while (std::getline(iss, token, ',')) {
        size_t colon_pos = token.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string key = token.substr(0, colon_pos);
        std::string value_str = token.substr(colon_pos + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" "));
        key.erase(key.find_last_not_of(" ") + 1);
        value_str.erase(0, value_str.find_first_not_of(" "));
        value_str.erase(value_str.find_last_not_of(" ") + 1);

        try {
            if (key == "Setpoint") point.setpoint = std::stof(value_str);
            else if (key == "Return Sensor") point.return_sensor = std::stof(value_str);
            else if (key == "Coil Sensor") point.coil_sensor = std::stof(value_str);
            else if (key == "Supply") point.supply = std::stof(value_str);
        } catch (...) { }
    }
    return true;
}

std::vector<ConditionDataPoint> TemperatureDataTable::ReadLast6Hours() {
    std::string log_path = "/var/log/refrigeration/conditions-";
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    log_path += std::string(buffer) + ".log";
    std::string lock_file = log_path + ".lock";

    std::vector<ConditionDataPoint> data;

    // Wait for any ongoing writes to complete
    int max_wait_cycles = 20;
    for (int wait = 0; wait < max_wait_cycles; ++wait) {
        if (!std::filesystem::exists(lock_file)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Try to open file with retries
    FILE* file = nullptr;
    int max_retries = 3;
    for (int i = 0; i < max_retries; ++i) {
        file = fopen(log_path.c_str(), "r");
        if (file) break;
        if (i < max_retries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (!file)
        return data;

    char line_buf[512];
    time_t six_hours_ago = now - (6 * 3600);

    while (fgets(line_buf, sizeof(line_buf), file)) {
        std::string line(line_buf);
        if (!line.empty() && line.back() == '\n') line.pop_back();

        ConditionDataPoint point = {};
        if (ParseConditionLine(line, point) && point.timestamp >= six_hours_ago) {
            data.push_back(point);
        }
    }
    fclose(file);
    return data;
}

std::vector<std::string> TemperatureDataTable::FormatAsTable(const std::vector<ConditionDataPoint>& data, int height, int scroll_offset) {
    std::vector<std::string> table;

    if (data.empty()) {
        table.push_back("[No temperature data available]");
        return table;
    }

    try {
        // Excel-style header
        table.push_back("Timestamp            Setpoint(°F)  Return(°F)   Coil(°F)     Supply(°F)");
        table.push_back("─────────────────────────────────────────────────────────────────────────");

        // Calculate start index based on scroll offset
        // Show the most recent data by default, scroll backwards with offset
        int total_entries = data.size();
        int start_idx = std::max(0, total_entries - height - scroll_offset);
        int end_idx = std::min(total_entries, start_idx + height);

        for (int i = start_idx; i < end_idx; ++i) {
            const auto& point = data[i];

            // Format time
            auto tm = *std::localtime(&point.timestamp);
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);

            // Format temperatures with safe handling
            char line[128];
            float sp = std::isnan(point.setpoint) ? 0.0f : point.setpoint;
            float rt = std::isnan(point.return_sensor) ? 0.0f : point.return_sensor;
            float ct = std::isnan(point.coil_sensor) ? 0.0f : point.coil_sensor;
            float st = std::isnan(point.supply) ? 0.0f : point.supply;

            snprintf(line, sizeof(line), "%s     %7.1f        %7.1f      %7.1f       %7.1f",
                    time_str, sp, rt, ct, st);
            table.push_back(line);
        }

        table.push_back("─────────────────────────────────────────────────────────────────────────");
        table.push_back("Total entries: " + std::to_string(total_entries) + " | Showing: " + std::to_string(end_idx - start_idx));

    } catch (const std::exception& e) {
        table.clear();
        table.push_back(std::string("[Error formatting table: ") + e.what() + "]");
    } catch (...) {
        table.clear();
        table.push_back("[Unknown error formatting table]");
    }

    return table;
}
