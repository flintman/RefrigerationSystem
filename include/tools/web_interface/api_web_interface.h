/*
 * API-Based Refrigeration Web Interface
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * Web interface that aggregates data from multiple refrigeration units via REST API
 * Orchestrates ConfigManager, WebServer, UnitPoller, and EmailNotifier components
 */

#ifndef API_WEB_INTERFACE_H
#define API_WEB_INTERFACE_H

#include "config_manager.h"
#include "web_server.h"
#include "unit_poller.h"
#include "email_notifier.h"
#include "api_proxy.h"

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class APIWebInterface {
public:
    APIWebInterface(const std::string& config_file = "web_interface_config.env");
    ~APIWebInterface();

    void start();
    void stop();

private:
    // Components
    std::unique_ptr<ConfigManager> config_manager_;
    std::unique_ptr<WebServer> web_server_;
    std::unique_ptr<UnitPoller> unit_poller_;
    std::unique_ptr<EmailNotifier> email_notifier_;
    std::unique_ptr<APIProxy> api_proxy_;

    bool running_;
    std::string config_file_;

    // Request handlers
    std::string handle_get_request(const std::string& path);
    std::string handle_post_request(const std::string& path, const std::string& body);

    // Download handlers
    std::string handle_download_events_request(const std::string& date);
    std::string handle_download_conditions_request(const std::string& date);

    // Utilities
    void write_log(const std::string& message);
    bool verify_password(const std::string& password);
    std::string base64_encode(const std::string& input);
    void on_config_changed();
    void send_startup_email();
};

#endif // API_WEB_INTERFACE_H
