#include "tools/log_reader.h"
#include <fstream>
#include <ctime>
#include <filesystem>
#include <thread>
#include <cstring>

std::string LogReader::GetTodaysEventLogPath() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return std::string("/var/log/refrigeration/events-") + buffer + ".log";
}

std::string LogReader::GetTodaysConditionsLogPath() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return std::string("/var/log/refrigeration/conditions-") + buffer + ".log";
}

void LogReader::WaitForLogLock(const std::string& lock_file) {
    int max_wait_cycles = 20;  // ~200ms total with 10ms sleeps
    for (int wait = 0; wait < max_wait_cycles; ++wait) {
        if (!std::filesystem::exists(lock_file)) {
            break;  // Lock released, safe to read
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<std::string> LogReader::ReadEventsLog() {
    std::string log_path = GetTodaysEventLogPath();
    std::string lock_file = log_path + ".lock";
    std::vector<std::string> lines;

    WaitForLogLock(lock_file);

    FILE* file = fopen(log_path.c_str(), "r");
    if (!file) {
        lines.push_back("[Log file not found: " + log_path + "]");
        return lines;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), file)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        lines.push_back(buffer);
    }
    fclose(file);
    return lines;
}

std::vector<std::string> LogReader::ReadConditionsLog() {
    std::string log_path = GetTodaysConditionsLogPath();
    std::string lock_file = log_path + ".lock";
    std::vector<std::string> lines;

    WaitForLogLock(lock_file);

    FILE* file = fopen(log_path.c_str(), "r");
    if (!file) {
        lines.push_back("[Log file not found: " + log_path + "]");
        return lines;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), file)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        lines.push_back(buffer);
    }
    fclose(file);
    return lines;
}
