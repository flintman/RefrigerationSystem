#pragma once

#include <string>
#include <vector>
#include <ctime>

/**
 * LogReader handles reading and parsing of refrigeration log files
 * Supports events logs and conditions logs
 */
class LogReader {
public:
    /**
     * Read events log for today
     */
    std::vector<std::string> ReadEventsLog();

    /**
     * Read conditions log for today
     */
    std::vector<std::string> ReadConditionsLog();

    /**
     * Get path to today's events log file
     */
    static std::string GetTodaysEventLogPath();

    /**
     * Get path to today's conditions log file
     */
    static std::string GetTodaysConditionsLogPath();

private:
    /**
     * Wait for log file lock to be released
     */
    static void WaitForLogLock(const std::string& lock_file);
};
