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
 * TemperatureGraphGenerator creates ASCII temperature graphs from condition data
 */
class TemperatureGraphGenerator {
public:
    /**
     * Generate ASCII temperature graph from condition data points
     * @param data Vector of condition data points
     * @param width Graph width in characters (default 80)
     * @param height Graph height in lines (default 10)
     * @return Vector of strings representing the graph
     */
    static std::vector<std::string> GenerateGraph(
        const std::vector<ConditionDataPoint>& data,
        int width = 80,
        int height = 10
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
