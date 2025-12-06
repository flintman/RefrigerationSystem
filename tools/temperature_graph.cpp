#include "tools/temperature_graph.h"
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <fstream>

bool TemperatureGraphGenerator::ParseConditionLine(const std::string& line, ConditionDataPoint& point) {
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

std::vector<ConditionDataPoint> TemperatureGraphGenerator::ReadLast6Hours() {
    std::string log_path = "/var/log/refrigeration/conditions-";
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    log_path += std::string(buffer) + ".log";
    std::string lock_file = log_path + ".lock";

    std::vector<ConditionDataPoint> data;

    // Wait for any ongoing writes to complete
    int max_wait_cycles = 20;  // ~200ms total with 10ms sleeps
    for (int wait = 0; wait < max_wait_cycles; ++wait) {
        if (!std::filesystem::exists(lock_file)) {
            break;  // Lock released, safe to read
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

std::vector<std::string> TemperatureGraphGenerator::GenerateGraph(const std::vector<ConditionDataPoint>& data, int width, int height) {
    std::vector<std::string> graph;

    if (data.empty()) {
        graph.push_back("[No data available for the last 6 hours]");
        return graph;
    }

    try {
        // Find min/max temperatures with safety checks
        float min_temp = 0.0f, max_temp = 0.0f;
        bool first = true;

        for (const auto& point : data) {
            // Skip invalid data points
            if (std::isnan(point.setpoint) || std::isnan(point.return_sensor) ||
                std::isnan(point.coil_sensor) || std::isnan(point.supply)) {
                continue;
            }

            float min_val = std::min({point.setpoint, point.return_sensor, point.coil_sensor, point.supply});
            float max_val = std::max({point.setpoint, point.return_sensor, point.coil_sensor, point.supply});

            if (first) {
                min_temp = min_val;
                max_temp = max_val;
                first = false;
            } else {
                min_temp = std::min(min_temp, min_val);
                max_temp = std::max(max_temp, max_val);
            }
        }

        // Add some padding
        float range = max_temp - min_temp;
        if (range < 1.0f) range = 1.0f;
        min_temp -= range * 0.05f;
        max_temp += range * 0.05f;

        // Sample data points to fit width
        std::vector<ConditionDataPoint> sampled;
        if (data.size() <= (size_t)width) {
            sampled = data;
        } else {
            int step = std::max(1, (int)data.size() / width);
            for (size_t i = 0; i < data.size(); i += step) {
                sampled.push_back(data[i]);
            }
            if (!sampled.empty() && sampled.back().timestamp != data.back().timestamp) {
                sampled.push_back(data.back());
            }
        }

        // Ensure we have valid data
        if (sampled.empty()) {
            graph.push_back("[No valid temperature data]");
            return graph;
        }

        // Ensure width doesn't exceed sampled data
        int graph_width = std::min((int)sampled.size(), width);

        // Normalize and create graph
        std::vector<std::vector<char>> grid(height, std::vector<char>(graph_width, ' '));

        for (int x = 0; x < graph_width && x < (int)sampled.size(); ++x) {
            const auto& point = sampled[x];

            // Skip invalid points
            if (std::isnan(point.setpoint) && std::isnan(point.return_sensor) &&
                std::isnan(point.coil_sensor) && std::isnan(point.supply)) {
                continue;
            }

            // Plot all 4 temperature values
            auto plot_temp = [&](float temp, char symbol) {
                if (std::isnan(temp) || temp < min_temp || temp > max_temp) return;
                int y = height - 1 - (int)((temp - min_temp) / (max_temp - min_temp + 0.001f) * (height - 1));
                y = std::max(0, std::min(height - 1, y));
                if (x >= 0 && x < graph_width && y >= 0 && y < height) {
                    if (grid[y][x] == ' ') grid[y][x] = symbol;
                }
            };
            plot_temp(point.setpoint, 'S');       // Setpoint
            plot_temp(point.return_sensor, 'R');  // Return sensor
            plot_temp(point.coil_sensor, 'C');    // Coil sensor
            plot_temp(point.supply, 'P');         // suPply
        }

        // Format as strings
        char y_label[16];
        for (int y = 0; y < height; ++y) {
            float temp = max_temp - (float)y / (height - 1) * (max_temp - min_temp);
            snprintf(y_label, sizeof(y_label), "%5.1f|", temp);
            std::string row(y_label);
            for (int x = 0; x < graph_width && x < (int)grid[y].size(); ++x) {
                row += grid[y][x];
            }
            graph.push_back(row);
        }

        // Add legend and time range
        graph.push_back("      +" + std::string(graph_width, '-'));
        graph.push_back("Legend: S=Setpoint, R=Return, C=Coil, P=Supply");

        if (!data.empty()) {
            auto first_tm = *std::localtime(&data.front().timestamp);
            auto last_tm = *std::localtime(&data.back().timestamp);
            char time_range[128];
            snprintf(time_range, sizeof(time_range), "Time range: %02d:%02d - %02d:%02d",
                    first_tm.tm_hour, first_tm.tm_min, last_tm.tm_hour, last_tm.tm_min);
            graph.push_back(time_range);
        }

    } catch (const std::exception& e) {
        graph.clear();
        graph.push_back(std::string("[Error generating graph: ") + e.what() + "]");
    } catch (...) {
        graph.clear();
        graph.push_back("[Unknown error generating graph]");
    }

    return graph;
}
