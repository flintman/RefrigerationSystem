/*
 * Refrigeration API Server
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * REST API for monitoring and controlling refrigeration system
 * Provides endpoints for relay status, sensor readings, and system control
 */

#ifndef REFRIGERATION_API_H
#define REFRIGERATION_API_H

#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "log_manager.h"

using json = nlohmann::json;

#define REFRIGERATION_API_VERSION "1.0.0"

class RefrigerationAPI {
public:
    /**
     * Initialize API server with port and config manager reference
     * @param port HTTP port to listen on (default 8080)
     * @param config_file Path to config file for API key
     * @param logger Reference to Logger instance for logging events
     */
    RefrigerationAPI(int port = 8080, const std::string& config_file = "/etc/refrigeration/config.env", Logger* logger = nullptr);

    ~RefrigerationAPI();

    /**
     * Start the API server (blocking call)
     */
    void start();

    /**
     * Stop the API server
     */
    void stop();

private:
    int port_;
    bool running_;
    std::string api_key_;
    std::string config_file_;
    Logger* logger_;
    std::unique_ptr<class HTTPServer> server_;

    // Helper methods
    void load_api_key();
    bool validate_api_key(const std::string& key);
    std::string get_error_response(int code, const std::string& message);

    // API Endpoint handlers
    json handle_status_request();
    json handle_relay_status_request();
    json handle_sensor_status_request();
    json handle_setpoint_get_request();
    json handle_setpoint_set_request(float new_setpoint);
    json handle_alarm_reset_request();
    json handle_defrost_trigger_request();
    json handle_demo_mode_request(bool enable);
    json handle_system_info_request();
    json handle_config_update_request(const json& config_updates);
    std::string handle_download_events_request(const std::string& date);
    std::string handle_download_conditions_request(const std::string& date);

    friend class HTTPServer;
};

#endif // REFRIGERATION_API_H
