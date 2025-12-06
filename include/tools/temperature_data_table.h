#pragma once

#include <string>
#include <vector>
#include <ctime>

/**
 * Represents a single data point from the conditions log
 */
struct ConditionDataPoint {
    time_t timestamp;
    float setpoint;
    float return_sensor;
    float coil_sensor;
    float supply;
};

/**
 * TemperatureDataTable reads temperature/condition data and formats it as an Excel-style table
 */
class TemperatureDataTable {
public:
    /**
     * Generate Excel-style table from condition data points
     * @param data Vector of condition data points
     * @param height Number of rows to display (default 6)
     * @param scroll_offset Starting row offset for scrolling
     * @return Vector of strings representing the table
     */
    static std::vector<std::string> FormatAsTable(
        const std::vector<ConditionDataPoint>& data,
        int height = 6,
        int scroll_offset = 0
    );

    /**
     * Parse a conditions log line into a ConditionDataPoint
     * @param line Log line to parse
     * @param point Output parameter for parsed data
     * @return true if parsing succeeded
     */
    static bool ParseConditionLine(const std::string& line, ConditionDataPoint& point);

    /**
     * Read and parse conditions log, filtering to last 6 hours
     * @return Vector of condition data points from last 6 hours
     */
    static std::vector<ConditionDataPoint> ReadLast6Hours();
};
