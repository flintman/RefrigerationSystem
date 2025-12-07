#include "tools/api_client.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

APIClient::APIClient(const std::string& host, int port, const std::string& api_key)
    : api_key(api_key) {
    Initialize(host, port);
}

void APIClient::Initialize(const std::string& host, int port) {
    api_base_url = "https://" + host + ":" + std::to_string(port) + "/api/v1";
}

void APIClient::SetAPIKey(const std::string& new_key) {
    api_key = new_key;
}

std::string APIClient::ExecuteCurl(const std::string& method, const std::string& endpoint, bool json_output) {
    std::string url = api_base_url + endpoint;
    std::string cmd;

    if (method == "GET") {
        cmd = "curl -s -k -m 3 -H \"X-API-Key:" + api_key + "\" " + url + " 2>&1";
    } else if (method == "POST") {
        cmd = "curl -s -k -m 3 -X POST -H \"X-API-Key: " + api_key + "\" " + url + " 2>&1";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return json_output ? "{}" : "[Command failed]";

    std::string result;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    if (result.empty()) {
        return json_output ? "{}" : "[No response]";
    }

    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \n\r\t"));
    result.erase(result.find_last_not_of(" \n\r\t") + 1);

    return result;
}

std::string APIClient::CheckHealth() {
    std::string result = ExecuteCurl("GET", "/health");

    // Check if response contains JSON status
    if (result.find("\"status\":\"ok\"") != std::string::npos) {
        // Extract timestamp if present
        size_t ts_pos = result.find("\"timestamp\":");
        if (ts_pos != std::string::npos) {
            ts_pos += 12;  // Length of "\"timestamp\":"
            size_t ts_end = result.find(",", ts_pos);
            if (ts_end == std::string::npos) {
                ts_end = result.find("}", ts_pos);
            }
            if (ts_end != std::string::npos) {
                std::string timestamp_str = result.substr(ts_pos, ts_end - ts_pos);
                // Trim whitespace
                timestamp_str.erase(0, timestamp_str.find_first_not_of(" \n\r\t"));
                timestamp_str.erase(timestamp_str.find_last_not_of(" \n\r\t") + 1);

                try {
                    time_t unix_time = std::stol(timestamp_str);
                    struct tm* tm_info = localtime(&unix_time);
                    char formatted[32];
                    strftime(formatted, sizeof(formatted), "%m/%d/%Y  %H:%M:%S", tm_info);
                    return "[✓ API Running] " + std::string(formatted);
                } catch (...) {
                    return "[✓ API Running]";
                }
            }
        }
        return "[✓ API Running]";
    } else {
        return "[✗ API Error]";
    }
}

nlohmann::json APIClient::GetStatus(const std::string& endpoint) {
    std::string result = ExecuteCurl("GET", endpoint);

    try {
        return nlohmann::json::parse(result);
    } catch (...) {
        return nlohmann::json::object();
    }
}

std::string APIClient::PostControl(const std::string& endpoint) {
    return ExecuteCurl("POST", endpoint);
}

nlohmann::json APIClient::SetDemoMode(bool enable) {
    std::string url = api_base_url + "/demo-mode";
    std::string json_data = enable ? "{\"enable\": true}" : "{\"enable\": false}";
    std::string cmd = "curl -s -k -m 3 -X POST -H \"X-API-Key: " + api_key + "\" " +
                      "-H \"Content-Type: application/json\" " +
                      "-d '" + json_data + "' " + url + " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return nlohmann::json::object();

    std::string result;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    if (result.empty()) {
        return nlohmann::json::object();
    }

    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \n\r\t"));
    result.erase(result.find_last_not_of(" \n\r\t") + 1);

    try {
        return nlohmann::json::parse(result);
    } catch (...) {
        return nlohmann::json::object();
    }
}

nlohmann::json APIClient::GetDemoMode() {
    return GetStatus("/demo-mode");
}

