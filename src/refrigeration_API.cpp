/*
 * Refrigeration API Server Implementation
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 */

#include "refrigeration_API.h"
#include "config_manager.h"
#include "config_validator.h"
#include "alarm.h"
#include "ssl_utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <iomanip>
#include <cerrno>

// Forward declarations - these globals are defined in refrigeration.cpp
extern std::atomic<float> return_temp;
extern std::atomic<float> supply_temp;
extern std::atomic<float> coil_temp;
extern std::atomic<float> setpoint;
extern std::map<std::string, std::string> status;
extern std::mutex status_mutex;
extern bool trigger_defrost;
extern std::atomic<bool> demo_mode;

extern Alarm systemAlarm;  // Forward declare global alarm system

// Simple HTTP Server implementation
class HTTPServer {
public:
    using RequestHandler = std::function<std::string(const std::string&, const std::string&)>;

    HTTPServer(int port, Logger* logger = nullptr, SSL_CTX* ssl_ctx = nullptr)
        : port_(port), running_(false), server_fd_(-1), logger_(logger), ssl_ctx_(ssl_ctx) {}

    ~HTTPServer() {
        if (running_) stop();
    }

    void start(RequestHandler handler) {
        running_ = true;
        handler_ = handler;

        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            if (logger_) {
                logger_->log_events("Error", "Failed to create HTTP socket");
            }
            return;
        }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (logger_) {
                logger_->log_events("Error", "Failed to bind HTTP socket to port " + std::to_string(port_));
            }
            close(server_fd_);
            return;
        }

        if (listen(server_fd_, 10) < 0) {
            if (logger_) {
                logger_->log_events("Error", "Failed to listen on HTTP socket");
            }
            close(server_fd_);
            return;
        }

        if (logger_) {
            logger_->log_events("Debug", "HTTP Server listening on port " + std::to_string(port_));
        }

        accept_loop();
    }

    void stop() {
        running_ = false;
        if (server_fd_ != -1) {
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }
    }

private:
    int port_;
    bool running_;
    int server_fd_;
    Logger* logger_;
    SSL_CTX* ssl_ctx_;
    RequestHandler handler_;

    void accept_loop() {
        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) continue;

            std::thread([this, client_fd]() {
                handle_client(client_fd);
            }).detach();
        }
    }

    void handle_client(int client_fd) {
        // Set read timeout (5 seconds)
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        // Handle SSL/TLS if enabled
        SSL* ssl = nullptr;
        if (ssl_ctx_) {
            ssl = SSL_new(ssl_ctx_);
            if (!ssl) {
                close(client_fd);
                return;
            }
            SSL_set_fd(ssl, client_fd);
            if (SSL_accept(ssl) <= 0) {
                SSL_free(ssl);
                close(client_fd);
                return;
            }
        }

        char buffer[4096] = {0};
        int bytes_read;
        if (ssl) {
            bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
        } else {
            bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        }

        if (bytes_read <= 0) {
            if (ssl) SSL_free(ssl);
            close(client_fd);
            return;
        }

        buffer[bytes_read] = '\0';
        std::string request(buffer);

        // Parse HTTP request for body
        size_t body_start = request.find("\r\n\r\n");
        std::string body;
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }

        // Pass the full HTTP request (including headers) to the handler
        std::string response = handler_(request, body);

        // Send HTTP response
        if (ssl) {
            SSL_write(ssl, response.c_str(), response.length());
        } else {
            send(client_fd, response.c_str(), response.length(), 0);
        }

        if (ssl) SSL_free(ssl);
        close(client_fd);
    }
};

RefrigerationAPI::RefrigerationAPI(int port, const std::string& config_file, Logger* logger,
                                   bool enable_https, const std::string& cert_file, const std::string& key_file)
    : port_(port), running_(false), enable_https_(enable_https), config_file_(config_file),
      cert_file_(cert_file), key_file_(key_file), logger_(logger),
      ssl_context_(nullptr, &SSL_CTX_free) {
    load_api_key();
    // Initialize rate limiter: 1000 global/min, 100 per-IP/min, 200 per-key/min
    rate_limiter_ = std::make_unique<RateLimiter>(1000, 100, 200);

    // Initialize SSL context if HTTPS is enabled
    if (enable_https_) {
        ssl_context_ = SSLContext::create_context(cert_file_, key_file_, true);
        if (ssl_context_) {
            if (logger_) {
                logger_->log_events("Debug", "HTTPS/TLS support enabled");
            }
        } else {
            if (logger_) {
                logger_->log_events("Error", "Failed to initialize SSL context. API will use HTTP only.");
            }
        }
    }
}

RefrigerationAPI::~RefrigerationAPI() {
    stop();
}

void RefrigerationAPI::load_api_key() {
    try {
        ConfigManager config(config_file_);
        api_key_ = config.get("api.key");

        if (api_key_.empty()) {
            // Generate default API key if not present
            api_key_ = "refrigeration-api-default-key-change-me";
            if (logger_) {
                logger_->log_events("Error", "Using default API key. Update 'api.key' in config for production!");
            }
        } else {
            if (logger_) {
                logger_->log_events("Debug", "API key loaded successfully");
            }
        }
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->log_events("Error", "Failed to load API key: " + std::string(e.what()));
        }
        api_key_ = "refrigeration-api-default-key-change-me";
    }
}

std::string RefrigerationAPI::extract_client_ip(const std::string& request) {
    // Try to extract from X-Forwarded-For header (for reverse proxies)
    std::istringstream req_stream(request);
    std::string line;
    while (std::getline(req_stream, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;
        size_t colon = line.find(":");
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        std::string key_lower = key;
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        if (key_lower == "x-forwarded-for") {
            // Extract first IP if comma-separated
            size_t comma = value.find(",");
            if (comma != std::string::npos) {
                value = value.substr(0, comma);
            }
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            return value;
        }
    }
    // Default to 127.0.0.1 if cannot extract (local connection)
    return "127.0.0.1";
}

bool RefrigerationAPI::validate_api_key(const std::string& key) {
    return !key.empty() && key == api_key_;
}

std::string RefrigerationAPI::get_error_response(int code, const std::string& message) {
    json error;
    error["error"] = true;
    error["code"] = code;
    error["message"] = message;
    error["timestamp"] = std::time(nullptr);

    std::string body = error.dump();
    std::string response = "HTTP/1.1 " + std::to_string(code) + " Error\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "\r\n";
    response += body;

    return response;
}

json RefrigerationAPI::handle_status_request() {
    json status_response;
    status_response["timestamp"] = std::time(nullptr);
    status_response["system"] = "Refrigeration Control System";
    status_response["version"] = REFRIGERATION_API_VERSION;

    try {
        status_response["relays"] = json::object();
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            status_response["relays"]["compressor"] = (status["compressor"] == "True");
            status_response["relays"]["fan"] = (status["fan"] == "True");
            status_response["relays"]["valve"] = (status["valve"] == "True");
            status_response["relays"]["electric_heater"] = (status["electric_heater"] == "True");
            status_response["system_status"] = status["status"];
            // Add alarm info to status response
            status_response["active_alarms"] = systemAlarm.getAlarmCodes();
            status_response["alarm_warning"] = systemAlarm.getWarningStatus();
            status_response["alarm_shutdown"] = systemAlarm.getShutdownStatus();
        }
    } catch (...) {
        status_response["relays"] = json::object();
        status_response["system_status"] = "Unknown";
        if (logger_) {
            logger_->log_events("Error", "API: Exception reading relay status");
        }
    }

    try {
        status_response["sensors"] = json::object();
        status_response["sensors"]["return_temp"] = return_temp.load();
        status_response["sensors"]["supply_temp"] = supply_temp.load();
        status_response["sensors"]["coil_temp"] = coil_temp.load();
        status_response["setpoint"] = setpoint.load();
    } catch (...) {
        status_response["sensors"] = json::object();
        status_response["setpoint"] = 0.0f;
        if (logger_) {
            logger_->log_events("Error", "API: Exception reading sensor data");
        }
    }

    return status_response;
}

json RefrigerationAPI::handle_relay_status_request() {
    json relays;

    try {
        std::lock_guard<std::mutex> lock(status_mutex);
        relays["compressor"] = (status["compressor"] == "True");
        relays["fan"] = (status["fan"] == "True");
        relays["valve"] = (status["valve"] == "True");
        relays["electric_heater"] = (status["electric_heater"] == "True");
        relays["timestamp"] = std::time(nullptr);
    } catch (const std::exception& e) {
        relays["error"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Exception reading relay status - " + std::string(e.what()));
        }
    }

    return relays;
}

json RefrigerationAPI::handle_sensor_status_request() {
    json sensors;

    try {
        sensors["return_temp"] = return_temp.load();
        sensors["supply_temp"] = supply_temp.load();
        sensors["coil_temp"] = coil_temp.load();
        sensors["setpoint"] = setpoint.load();
        sensors["timestamp"] = std::time(nullptr);
    } catch (const std::exception& e) {
        sensors["error"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Exception reading sensor data - " + std::string(e.what()));
        }
    }

    return sensors;
}

json RefrigerationAPI::handle_setpoint_get_request() {
    json response;

    try {
        response["setpoint"] = setpoint.load();
        response["timestamp"] = std::time(nullptr);
    } catch (const std::exception& e) {
        response["error"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Exception in GET setpoint - " + std::string(e.what()));
        }
    }

    return response;
}

json RefrigerationAPI::handle_setpoint_set_request(float new_setpoint) {
    json response;

    try {
        ConfigManager config(config_file_);
        float min_sp = std::stof(config.get("setpoint.low_limit"));
        float max_sp = std::stof(config.get("setpoint.high_limit"));

        if (new_setpoint < min_sp || new_setpoint > max_sp) {
            response["error"] = true;
            response["message"] = "Setpoint out of range";
            response["low_limit"] = min_sp;
            response["high_limit"] = max_sp;
            if (logger_) {
                logger_->log_events("Debug", "API: Setpoint " + std::to_string(new_setpoint) + " out of range [" + std::to_string(min_sp) + ", " + std::to_string(max_sp) + "]");
            }
            return response;
        }

        setpoint.store(new_setpoint);
        config.update("unit.setpoint", std::to_string(static_cast<int>(new_setpoint)));

        response["success"] = true;
        response["setpoint"] = new_setpoint;
        response["timestamp"] = std::time(nullptr);

        if (logger_) {
            logger_->log_events("Debug", "API: Setpoint updated to " + std::to_string(new_setpoint));
        }
    } catch (const std::exception& e) {
        response["error"] = true;
        response["message"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Setpoint update failed - " + std::string(e.what()));
        }
    }

    return response;
}

json RefrigerationAPI::handle_alarm_reset_request() {
    json response;

    try {
        systemAlarm.resetAlarm();
        // Sleep briefly to allow the alarm reset to take effect
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        response["success"] = true;
        response["message"] = "Alarms reset successfully";
        response["timestamp"] = std::time(nullptr);

        if (logger_) {
            logger_->log_events("Debug", "API: Alarms have been reset");
        }
    } catch (const std::exception& e) {
        response["error"] = true;
        response["message"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Exception resetting alarms - " + std::string(e.what()));
        }
    }

    return response;
}

json RefrigerationAPI::handle_defrost_trigger_request() {
    json response;

    try {
        trigger_defrost = true;
        response["success"] = true;
        response["message"] = "Defrost triggered";
        response["timestamp"] = std::time(nullptr);

        if (logger_) {
            logger_->log_events("Info", "API: Manual defrost triggered");
        }
    } catch (const std::exception& e) {
        response["error"] = true;
        response["message"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Defrost trigger failed - " + std::string(e.what()));
        }
    }

    return response;
}

json RefrigerationAPI::handle_demo_mode_request(bool enable) {
    json response;

    try {
        // Check if debug mode is enabled - if it is, block demo mode
        ConfigManager config(config_file_);
        std::string debug_code = config.get("debug.code");

        if (debug_code == "0") {
            response["success"] = false;
            response["message"] = "Demo mode is disabled";
            response["demo_mode"] = demo_mode.load();
            response["timestamp"] = std::time(nullptr);
            return response;
        }

        // Otherwise allow demo mode change
        bool old_mode = demo_mode.load();
        demo_mode.store(enable);
        response["success"] = true;
        response["message"] = enable ? "Demo mode enabled" : "Demo mode disabled";
        response["demo_mode"] = enable;
        response["previous_state"] = old_mode;
        response["timestamp"] = std::time(nullptr);

        if (logger_) {
            logger_->log_events("Info", "API: Demo mode " + std::string(enable ? "enabled" : "disabled"));
        }
    } catch (const std::exception& e) {
        response["error"] = true;
        response["message"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Demo mode change failed - " + std::string(e.what()));
        }
    }

    return response;
}

json RefrigerationAPI::handle_system_info_request() {
    json info;

    try {
        ConfigManager config("/etc/refrigeration/config.env");

        // Return all configuration values
        info["api.key"] = config.get("api.key");
        info["api.port"] = config.get("api.port");
        info["compressor.off_timer"] = config.get("compressor.off_timer");
        info["debug.code"] = config.get("debug.code");
        info["defrost.coil_temperature"] = config.get("defrost.coil_temperature");
        info["defrost.interval_hours"] = config.get("defrost.interval_hours");
        info["defrost.timeout_mins"] = config.get("defrost.timeout_mins");
        info["logging.interval_mins"] = config.get("logging.interval_mins");
        info["logging.retention_period"] = config.get("logging.retention_period");
        info["sensor.coil"] = config.get("sensor.coil");
        info["sensor.return"] = config.get("sensor.return");
        info["sensor.supply"] = config.get("sensor.supply");
        info["setpoint.high_limit"] = config.get("setpoint.high_limit");
        info["setpoint.low_limit"] = config.get("setpoint.low_limit");
        info["setpoint.offset"] = config.get("setpoint.offset");
        info["unit.compressor_run_seconds"] = config.get("unit.compressor_run_seconds");
        info["unit.electric_heat"] = config.get("unit.electric_heat");
        info["unit.fan_continuous"] = config.get("unit.fan_continuous");
        info["unit.number"] = config.get("unit.number");
        info["unit.relay_active_low"] = config.get("unit.relay_active_low");
        info["unit.setpoint"] = config.get("unit.setpoint");
        info["wifi.enable_hotspot"] = config.get("wifi.enable_hotspot");
        info["wifi.hotspot_password"] = config.get("wifi.hotspot_password");

        info["timestamp"] = std::time(nullptr);
    } catch (const std::exception& e) {
        info["error"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Exception reading system info - " + std::string(e.what()));
        }
    }

    return info;
}

json RefrigerationAPI::handle_config_update_request(const json& config_updates) {
    json response;

    if (logger_) {
        logger_->log_events("Debug", "API: Config update request received");
    }

    try {
        ConfigManager config(config_file_);

        // Track what was updated
        json updated_items;
        json skipped_items;
        json errors;

        // Iterate through all provided updates
        for (auto& [key, value] : config_updates.items()) {
            try {
                // Skip read-only fields
                if (key == "timestamp" || key == "active_alarms" || key == "alarm_warning" ||
                    key == "alarm_shutdown" || key == "unit.compressor_run_seconds") {
                    skipped_items[key] = "Read-only field";
                    continue;
                }

                // Skip security-sensitive fields that cannot be updated via API
                if (key == "api.key" || key == "api.port") {
                    skipped_items[key] = "Cannot be updated via API for security reasons";
                    if (logger_) {
                        logger_->log_events("Debug", "API: Attempt to update security-sensitive field '" + key + "' was blocked");
                    }
                    continue;
                }

                // Convert value to string
                std::string str_value;
                if (value.is_number()) {
                    str_value = std::to_string(value.get<int>());
                } else if (value.is_boolean()) {
                    str_value = value.get<bool>() ? "1" : "0";
                } else {
                    str_value = value.get<std::string>();
                }

                // Validate using ConfigValidator
                ConfigValidator validator;
                if (!validator.validate(key, str_value)) {
                    errors[key] = "Invalid value or key not found in schema";
                    if (logger_) {
                        logger_->log_events("Error", "API: Validation failed for " + key + " = " + str_value);
                    }
                    continue;
                }

                // Update the config
                config.update(key, str_value);
                updated_items[key] = str_value;

                if (logger_) {
                    logger_->log_events("Debug", "API: Config updated - " + key + " = " + str_value);
                }
            } catch (const std::exception& e) {
                errors[key] = e.what();
                if (logger_) {
                    logger_->log_events("Error", "API: Failed to update " + key + " - " + std::string(e.what()));
                }
            }
        }

        // Save config if there were successful updates
        if (!updated_items.empty()) {
            response["success"] = true;
            response["updated"] = updated_items;

            if (logger_) {
                logger_->log_events("Debug", "API: Config file saved successfully");
            }
        } else {
            response["success"] = false;
            response["message"] = "No items were updated";
        }

        // Include skipped and error items in response
        if (!skipped_items.empty()) {
            response["skipped"] = skipped_items;
        }
        if (!errors.empty()) {
            response["errors"] = errors;
        }

        response["timestamp"] = std::time(nullptr);

    } catch (const std::exception& e) {
        response["error"] = true;
        response["message"] = e.what();
        if (logger_) {
            logger_->log_events("Error", "API: Config update failed - " + std::string(e.what()));
        }
    }

    return response;
}

std::string RefrigerationAPI::handle_download_events_request(const std::string& date) {
    try {
        // Validate date format (YYYY-MM-DD)
        if (date.length() != 10 || date[4] != '-' || date[7] != '-') {
            if (logger_) {
                logger_->log_events("Debug", "API: Invalid date format provided: " + date);
            }
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Invalid date format. Use YYYY-MM-DD\"}";
        }

        std::string log_file_path = "/var/log/refrigeration/events-" + date + ".log";

        // Check if file exists
        std::ifstream log_file(log_file_path);
        if (!log_file.is_open()) {
            if (logger_) {
                logger_->log_events("Debug", "API: Events log file not found: " + log_file_path);
            }
            return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Log file not found for date: " + date + "\"}";
        }

        // Read entire file
        std::stringstream buffer;
        buffer << log_file.rdbuf();
        log_file.close();
        std::string file_content = buffer.str();

        // Return file with proper headers
        std::string response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Disposition: attachment; filename=\"events-" + date + ".log\"\r\n";
        response += "Content-Length: " + std::to_string(file_content.length()) + "\r\n";
        response += "Connection: close\r\n";
        response += "\r\n";
        response += file_content;

        return response;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->log_events("Error", "API: Exception downloading events - " + std::string(e.what()));
        }
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"" + std::string(e.what()) + "\"}";
    }
}

std::string RefrigerationAPI::handle_download_conditions_request(const std::string& date) {
    try {
        // Validate date format (YYYY-MM-DD)
        if (date.length() != 10 || date[4] != '-' || date[7] != '-') {
            if (logger_) {
                logger_->log_events("Error", "API: Invalid date format provided: " + date);
            }
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Invalid date format. Use YYYY-MM-DD\"}";
        }

        std::string log_file_path = "/var/log/refrigeration/conditions-" + date + ".log";

        // Check if file exists
        std::ifstream log_file(log_file_path);
        if (!log_file.is_open()) {
            if (logger_) {
                logger_->log_events("Error", "API: Conditions log file not found: " + log_file_path);
            }
            return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Log file not found for date: " + date + "\"}";
        }

        // Read entire file
        std::stringstream buffer;
        buffer << log_file.rdbuf();
        log_file.close();
        std::string file_content = buffer.str();

        // Return file with proper headers
        std::string response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Disposition: attachment; filename=\"conditions-" + date + ".log\"\r\n";
        response += "Content-Length: " + std::to_string(file_content.length()) + "\r\n";
        response += "Connection: close\r\n";
        response += "\r\n";
        response += file_content;

        return response;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->log_events("Error", "API: Exception downloading conditions - " + std::string(e.what()));
        }
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"" + std::string(e.what()) + "\"}";
    }
}

void RefrigerationAPI::start() {
    running_ = true;

    if (logger_) {
        logger_->log_events("Debug", "API Server starting on port " + std::to_string(port_));
        if (enable_https_ && ssl_context_) {
            logger_->log_events("Debug", "Using HTTPS/TLS encryption");
        } else if (enable_https_) {
            logger_->log_events("Error", "HTTPS enabled but SSL context failed to initialize - using HTTP");
        }
    }

    server_ = std::make_unique<HTTPServer>(port_, logger_, ssl_context_.get());

    server_->start([this](const std::string& request, const std::string& body) -> std::string {
        std::istringstream iss(request);
        std::string method, path;
        iss >> method >> path;

        // Extract query string
        size_t query_pos = path.find('?');
        std::string query_string;
        if (query_pos != std::string::npos) {
            query_string = path.substr(query_pos + 1);
            path = path.substr(0, query_pos);
        }


        // Extract API key from X-API-Key header (case-insensitive) or query string
        std::string api_key;
        bool found_header = false;
        std::istringstream req_stream(request);
        std::string line;
        while (std::getline(req_stream, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty()) continue;
            size_t colon = line.find(":");
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            std::string key_lower = key;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            if (key_lower == "x-api-key") {
                api_key = value;
                found_header = true;
                break;
            }
        }
        if (!found_header && !query_string.empty()) {
            size_t key_pos = query_string.find("api_key=");
            if (key_pos != std::string::npos) {
                api_key = query_string.substr(key_pos + 8);
                api_key = api_key.substr(0, api_key.find('&'));
            }
        }

        // Extract client IP for rate limiting
        std::string client_ip = extract_client_ip(request);

        // Check rate limit (applies to all endpoints)
        if (!rate_limiter_->is_allowed(client_ip, api_key)) {
            int remaining = rate_limiter_->get_remaining_requests(client_ip);
            int reset_in = rate_limiter_->get_reset_time(client_ip);

            std::string error_body = "{\"error\":\"Rate limit exceeded\",\"remaining\":\"0\",\"reset_in_seconds\":" + std::to_string(reset_in) + "}";
            std::string response = "HTTP/1.1 429 Too Many Requests\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Content-Length: " + std::to_string(error_body.length()) + "\r\n";
            response += "Retry-After: " + std::to_string(reset_in) + "\r\n";
            response += "Access-Control-Allow-Origin: *\r\n";
            response += "Connection: close\r\n";
            response += "\r\n";
            response += error_body;

            if (logger_) {
                logger_->log_events("Error", "API: Rate limit exceeded for IP " + client_ip);
            }

            return response;
        }

        // Validate API key for all endpoints except /health
        if (path != "/health" && !validate_api_key(api_key)) {
            return get_error_response(401, "Invalid or missing API key");
        }

        json response_json;
        int http_code = 200;

        try {
            // Health check endpoint (no auth required)
            if (path == "/health" || path == "/api/v1/health") {
                response_json["status"] = "ok";
                response_json["timestamp"] = std::time(nullptr);
            }
            // Status endpoints
            else if (path == "/api/v1/status") {
                response_json = handle_status_request();
            }
            else if (path == "/api/v1/relays") {
                response_json = handle_relay_status_request();
            }
            else if (path == "/api/v1/sensors") {
                response_json = handle_sensor_status_request();
            }
            // Setpoint endpoints
            else if (path == "/api/v1/setpoint" && method == "GET") {
                response_json = handle_setpoint_get_request();
            }
            else if (path == "/api/v1/setpoint" && method == "POST") {
                try {
                    json body_json = json::parse(body);
                    if (body_json.contains("setpoint") && body_json["setpoint"].is_number()) {
                        float new_sp = body_json["setpoint"];
                        response_json = handle_setpoint_set_request(new_sp);
                    } else {
                        http_code = 400;
                        response_json["error"] = "Missing or invalid 'setpoint' field";
                    }
                } catch (const std::exception& e) {
                    http_code = 400;
                    response_json["error"] = "Invalid JSON body";
                }
            }
            // Control endpoints
            else if (path == "/api/v1/alarms/reset" && method == "POST") {
                response_json = handle_alarm_reset_request();
            }
            else if (path == "/api/v1/defrost/trigger" && method == "POST") {
                response_json = handle_defrost_trigger_request();
            }
            else if (path == "/api/v1/demo-mode" && method == "POST") {
                try {
                    json body_json = json::parse(body);
                    if (body_json.contains("enable") && body_json["enable"].is_boolean()) {
                        bool enable = body_json["enable"].get<bool>();
                        response_json = handle_demo_mode_request(enable);
                    } else {
                        http_code = 400;
                        response_json["error"] = "Missing or invalid 'enable' boolean field";
                    }
                } catch (const std::exception& e) {
                    http_code = 400;
                    response_json["error"] = "Invalid JSON body";
                }
            }
            else if (path == "/api/v1/demo-mode" && method == "GET") {
                response_json["demo_mode"] = demo_mode.load();
                response_json["timestamp"] = std::time(nullptr);
            }
            // System info
            else if (path == "/api/v1/system-info") {
                response_json = handle_system_info_request();
            }
            // Config update
            else if (path == "/api/v1/config" && method == "POST") {
                try {
                    json body_json = json::parse(body);
                    response_json = handle_config_update_request(body_json);
                } catch (const std::exception& e) {
                    http_code = 400;
                    response_json["error"] = true;
                    response_json["message"] = "Invalid JSON body";
                    if (logger_) {
                        logger_->log_events("Debug", "API: Invalid JSON in config update - " + std::string(e.what()));
                    }
                }
            }
            // Download endpoints
            else if (path.find("/api/v1/logs/events") == 0) {
                // Extract date from query string: /api/v1/logs/events?date=2025-12-05
                std::string date;
                size_t date_pos = query_string.find("date=");
                if (date_pos != std::string::npos) {
                    date = query_string.substr(date_pos + 5);
                    // Remove any trailing parameters or spaces
                    size_t amp_pos = date.find('&');
                    if (amp_pos != std::string::npos) {
                        date = date.substr(0, amp_pos);
                    }
                    size_t space_pos = date.find(' ');
                    if (space_pos != std::string::npos) {
                        date = date.substr(0, space_pos);
                    }
                } else {
                    return get_error_response(400, "Missing 'date' parameter. Use ?date=YYYY-MM-DD");
                }
                return handle_download_events_request(date);
            }
            else if (path.find("/api/v1/logs/conditions") == 0) {
                // Extract date from query string: /api/v1/logs/conditions?date=2025-12-05
                std::string date;
                size_t date_pos = query_string.find("date=");
                if (date_pos != std::string::npos) {
                    date = query_string.substr(date_pos + 5);
                    // Remove any trailing parameters or spaces
                    size_t amp_pos = date.find('&');
                    if (amp_pos != std::string::npos) {
                        date = date.substr(0, amp_pos);
                    }
                    size_t space_pos = date.find(' ');
                    if (space_pos != std::string::npos) {
                        date = date.substr(0, space_pos);
                    }
                } else {
                    return get_error_response(400, "Missing 'date' parameter. Use ?date=YYYY-MM-DD");
                }
                return handle_download_conditions_request(date);
            }
            else {
                http_code = 404;
                response_json["error"] = "Endpoint not found";
            }
        } catch (const std::exception& e) {
            http_code = 500;
            response_json["error"] = e.what();
        }

        response_json["timestamp"] = std::time(nullptr);

        std::string body_str = response_json.dump();
        std::string http_response = "HTTP/1.1 " + std::to_string(http_code) + " OK\r\n";
        http_response += "Content-Type: application/json\r\n";
        http_response += "Content-Length: " + std::to_string(body_str.length()) + "\r\n";
        http_response += "Access-Control-Allow-Origin: *\r\n";
        http_response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        http_response += "Connection: close\r\n";
        http_response += "\r\n";
        http_response += body_str;

        return http_response;
    });
}

void RefrigerationAPI::stop() {
    running_ = false;
    if (server_) {
        server_->stop();
    }
    if (logger_) {
        logger_->log_events("Debug", "API Server stopped");
    }
}
