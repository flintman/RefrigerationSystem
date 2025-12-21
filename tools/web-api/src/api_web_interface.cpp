/*
 * API-Based Refrigeration Web Interface Implementation
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * This is the orchestrator component that coordinates:
 * - ConfigManager: Configuration file loading and monitoring
 * - WebServer: HTTP server and request handling
 * - UnitPoller: Unit data collection
 * - EmailNotifier: Email notifications
 * - APIProxy: API calls to refrigeration units
 */

#include "../include/tools/web_interface/api_web_interface.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <curl/curl.h>

// CURL callback for downloading file data
static size_t web_interface_curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

APIWebInterface::APIWebInterface(const std::string& config_file)
    : config_file_(config_file), running_(false) {

    // Initialize components
    config_manager_ = std::make_unique<ConfigManager>(config_file);
    web_server_ = std::make_unique<WebServer>(config_manager_->get_web_port());
    unit_poller_ = std::make_unique<UnitPoller>();
    email_notifier_ = std::make_unique<EmailNotifier>(
        config_manager_->get_email_server(),
        config_manager_->get_email_port(),
        config_manager_->get_email_address(),
        config_manager_->get_email_password()
    );
    // Connect email notifier to unit poller for alarm notifications
    unit_poller_->set_email_notifier(email_notifier_.get());

    api_proxy_ = std::make_unique<APIProxy>();

    write_log("APIWebInterface: Initialized with config from " + config_file);
}

APIWebInterface::~APIWebInterface() {
    stop();
}

void APIWebInterface::start() {
    if (running_) {
        return;
    }

    running_ = true;
    write_log("APIWebInterface: Starting components...");

    // Get units from config manager
    auto& units = config_manager_->get_units();
    unit_poller_->start(units);

    // Set up web server handlers
    web_server_->set_get_handler([this](const std::string& path) {
        return handle_get_request(path);
    });

    web_server_->set_post_handler([this](const std::string& path, const std::string& body) {
        return handle_post_request(path, body);
    });

    web_server_->set_login_verifier([this](const std::string& password) {
        return verify_password(password);
    });

    // Start config file watcher
    config_manager_->start_watch_thread();

    // Start web server
    web_server_->start();

    write_log("APIWebInterface: All components started successfully");

    // Send initialization email with startup information
    send_startup_email();
}

void APIWebInterface::stop() {
    running_ = false;

    write_log("APIWebInterface: Stopping components...");

    unit_poller_->stop();
    config_manager_->stop_watch_thread();
    web_server_->stop();

    write_log("APIWebInterface: All components stopped");
}

bool APIWebInterface::verify_password(const std::string& password) {
    return password == config_manager_->get_web_password();
}

std::string APIWebInterface::handle_get_request(const std::string& path) {
    write_log("APIWebInterface: GET request to " + path);

    // Handle static file requests
    if (path.find("/static/") == 0) {
        std::string file_path = path.substr(8);  // Remove "/static/"

        // Security: prevent directory traversal
        if (file_path.find("..") != std::string::npos) {
            return "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nForbidden";
        }

        // Try multiple locations for the static file
        std::string file_locations[] = {
            "/usr/share/web-api/static/" + file_path
        };

        for (const auto& loc : file_locations) {
            std::ifstream file(loc, std::ios::binary);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                file.close();

                // Determine content type
                std::string content_type = "application/octet-stream";
                if (file_path.find(".css") != std::string::npos) {
                    content_type = "text/css; charset=utf-8";
                } else if (file_path.find(".js") != std::string::npos) {
                    content_type = "application/javascript; charset=utf-8";
                }

                std::ostringstream response;
                response << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: " << content_type << "\r\n"
                        << "Content-Length: " << content.length() << "\r\n"
                        << "Connection: close\r\n"
                        << "\r\n"
                        << content;
                return response.str();
            }
        }

        write_log("Static file not found: " + path);
        return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
    }

    if (path == "/" || path == "/index.html") {
        std::string template_locations[] = {
            "/usr/share/web-api/templates/index.html"
        };

        std::string html;
        bool template_found = false;

        for (const auto& loc : template_locations) {
            std::ifstream file(loc);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                html = buffer.str();
                file.close();
                template_found = true;
                break;
            }
        }

        if (!template_found) {
            std::string error_page = "<html><body><h1>Error</h1><p>HTML template not found</p></body></html>";
            std::ostringstream response;
            response << "HTTP/1.1 500 Internal Server Error\r\n"
                    << "Content-Type: text/html; charset=utf-8\r\n"
                    << "Content-Length: " << error_page.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << error_page;
            return response.str();
        }

        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/html; charset=utf-8\r\n"
                 << "Content-Length: " << html.length() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << html;
        return response.str();
    } else if (path == "/api/units") {
        json response;
        auto& units = config_manager_->get_units();

        // Use cached data from UnitPoller for fast response
        json all_units = unit_poller_->get_all_unit_data();
        json unit_configs;

        for (const auto& unit : units) {
            // If no cached data, mark as offline
            if (!all_units.contains(unit.id) || all_units[unit.id].empty()) {
                all_units[unit.id]["system_status"] = "Offline";
            }
            // Optionally, you can cache configs in UnitPoller as well, but for now, leave empty
            unit_configs[unit.id] = json::object();
        }

        response["unit_data"] = all_units;
        response["unit_configs"] = unit_configs;
        response["unit_count"] = units.size();
        response["timestamp"] = std::time(nullptr);

        std::string body = response.dump();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.length() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;
        return oss.str();
    }

    // Handle /api/unit/{unitId}/* endpoints
    if (path.substr(0, 10) == "/api/unit/") {
        size_t next_slash = path.find('/', 10);
        if (next_slash != std::string::npos) {
            std::string unit_id = path.substr(10, next_slash - 10);
            std::string endpoint = path.substr(next_slash);

            // Find the unit
            auto& units = config_manager_->get_units();
            Unit target_unit{"", "", 0, ""};
            bool found = false;
            for (const auto& unit : units) {
                if (unit.id == unit_id) {
                    target_unit = unit;
                    found = true;
                    break;
                }
            }

            if (!found) {
                json error_response;
                error_response["error"] = "Unit not found";
                std::string body = error_response.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 404 Not Found\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << body;
                return oss.str();
            }

            // Route to specific endpoints
            if (endpoint == "/system-info") {
                json system_info = api_proxy_->get_system_info(target_unit);
                if (system_info.is_null() || system_info.empty()) {
                    system_info = json::object();
                }

                // Also add demo mode status and current status
                json demo_mode = api_proxy_->get_demo_mode(target_unit);
                if (!demo_mode.is_null() && demo_mode.contains("demo_mode")) {
                    system_info["demo_mode"] = demo_mode["demo_mode"];
                }

                json status = api_proxy_->get_status(target_unit);
                if (!status.is_null() && status.is_object()) {
                    for (auto& el : status.items()) {
                        if (system_info.find(el.key()) == system_info.end()) {
                            system_info[el.key()] = el.value();
                        }
                    }
                }

                std::string response_body = system_info.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << response_body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << response_body;
                return oss.str();
            } else if (endpoint == "/demo-mode") {
                json demo_info = api_proxy_->get_demo_mode(target_unit);
                if (demo_info.is_null()) {
                    demo_info = json::object();
                    demo_info["demo_mode"] = false;
                }

                std::string response_body = demo_info.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << response_body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << response_body;
                return oss.str();
            } else if (endpoint == "/status") {
                json status = api_proxy_->get_status(target_unit);
                if (status.is_null()) {
                    status = json::object();
                    status["system_status"] = "Offline";
                }

                std::string response_body = status.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << response_body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << response_body;
                return oss.str();
            }
        }
    }

    // Handle /api/v1/logs/events download endpoint
    if (path.find("/api/v1/logs/events") == 0) {
        // Extract date from query string: /api/v1/logs/events?date=2025-12-05
        std::string date;
        size_t query_start = path.find('?');
        if (query_start != std::string::npos) {
            std::string query_string = path.substr(query_start + 1);
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
            }
        }

        if (date.empty()) {
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Missing 'date' parameter. Use ?date=YYYY-MM-DD\"}";
        }

        return handle_download_events_request(date);
    }

    // Handle /api/v1/logs/conditions download endpoint
    if (path.find("/api/v1/logs/conditions") == 0) {
        // Extract date from query string: /api/v1/logs/conditions?date=2025-12-05
        std::string date;
        size_t query_start = path.find('?');
        if (query_start != std::string::npos) {
            std::string query_string = path.substr(query_start + 1);
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
            }
        }

        if (date.empty()) {
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Missing 'date' parameter. Use ?date=YYYY-MM-DD\"}";
        }

        return handle_download_conditions_request(date);
    }

    return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
}

std::string APIWebInterface::handle_post_request(const std::string& path, const std::string& body) {
    write_log("APIWebInterface: POST request to " + path);

    if (path == "/api/login") {
        try {
            auto data = json::parse(body);
            std::string password = data.value("password", "");

            json response;
            if (verify_password(password)) {
                response["authenticated"] = true;
            } else {
                response["authenticated"] = false;
            }

            std::string resp_body = response.dump();
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: application/json\r\n"
                << "Content-Length: " << resp_body.length() << "\r\n"
                << "Connection: close\r\n"
                << "\r\n"
                << resp_body;
            return oss.str();
        } catch (...) {
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
        }
    }

    // Handle /api/unit/{unitId}/* POST endpoints
    if (path.substr(0, 10) == "/api/unit/") {
        size_t next_slash = path.find('/', 10);
        if (next_slash != std::string::npos) {
            std::string unit_id = path.substr(10, next_slash - 10);
            std::string endpoint = path.substr(next_slash);

            // Find the unit
            auto& units = config_manager_->get_units();
            Unit target_unit{"", "", 0, ""};
            bool found = false;
            for (const auto& unit : units) {
                if (unit.id == unit_id) {
                    target_unit = unit;
                    found = true;
                    break;
                }
            }

            if (!found) {
                json error_response;
                error_response["error"] = "Unit not found";
                std::string response_body = error_response.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 404 Not Found\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << response_body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << response_body;
                return oss.str();
            }

            // Route to specific endpoints
            if (endpoint == "/alarms/reset") {
                json result = api_proxy_->reset_alarms(target_unit);
                if (result.is_null()) {
                    result = json::object();
                    result["success"] = true;
                    result["message"] = "Alarms reset successfully";
                }
                std::string response_body = result.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << response_body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << response_body;
                return oss.str();
            } else if (endpoint == "/defrost/trigger") {
                json result = api_proxy_->trigger_defrost(target_unit);
                if (result.is_null()) {
                    result = json::object();
                    result["success"] = true;
                    result["message"] = "Defrost triggered";
                }
                std::string response_body = result.dump();
                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: application/json\r\n"
                    << "Content-Length: " << response_body.length() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << response_body;
                return oss.str();
            } else if (endpoint == "/demo-mode") {
                try {
                    auto data = json::parse(body);
                    json result = api_proxy_->set_demo_mode(target_unit, data);
                    if (result.is_null()) {
                        result = json::object();
                        result["status"] = "sent";
                    }
                    std::string response_body = result.dump();
                    std::ostringstream oss;
                    oss << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: application/json\r\n"
                        << "Content-Length: " << response_body.length() << "\r\n"
                        << "Connection: close\r\n"
                        << "\r\n"
                        << response_body;
                    return oss.str();
                } catch (...) {
                    return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
                }
            } else if (endpoint == "/setpoint") {
                try {
                    auto data = json::parse(body);
                    json result = api_proxy_->set_setpoint(target_unit, data);
                    if (result.is_null()) {
                        result = json::object();
                        result["status"] = "setpoint_updated";
                    }
                    std::string response_body = result.dump();
                    std::ostringstream oss;
                    oss << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: application/json\r\n"
                        << "Content-Length: " << response_body.length() << "\r\n"
                        << "Connection: close\r\n"
                        << "\r\n"
                        << response_body;
                    return oss.str();
                } catch (...) {
                    return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
                }
            } else if (endpoint == "/config") {
                try {
                    auto data = json::parse(body);
                    json result = api_proxy_->set_config(target_unit, data);
                    if (result.is_null()) {
                        result = json::object();
                        result["status"] = "config_updated";
                    }
                    std::string response_body = result.dump();
                    std::ostringstream oss;
                    oss << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: application/json\r\n"
                        << "Content-Length: " << response_body.length() << "\r\n"
                        << "Connection: close\r\n"
                        << "\r\n"
                        << response_body;
                    return oss.str();
                } catch (...) {
                    return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
                }
            }
        }
    }

    return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
}

std::string APIWebInterface::base64_encode(const std::string& input) {
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (unsigned char c : input) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j <= i; j++)
            result += base64_chars[char_array_4[j]];

        while (i++ < 3)
            result += '=';
    }

    return result;
}

void APIWebInterface::on_config_changed() {
    write_log("APIWebInterface: Configuration changed, reloading...");
}

void APIWebInterface::send_startup_email() {
    try {
        auto& units = config_manager_->get_units();

        // Format current time as MM-DD-YYYY HH:MM:SS
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm* timeinfo = std::localtime(&time_t_now);
        char time_buffer[64];
        strftime(time_buffer, sizeof(time_buffer), "%m-%d-%Y %H:%M:%S", timeinfo);

        std::ostringstream body;
        body << "Refrigeration API Web Interface Started\n";
        body << "========================================\n\n";
        body << "Startup Time: " << time_buffer << "\n";
        body << "Web Server Port: " << config_manager_->get_web_port() << "\n";
        body << "Number of Units Configured: " << units.size() << "\n\n";

        if (!units.empty()) {
            body << "Configured Units:\n";
            body << "-----------------\n";
            for (const auto& unit : units) {
                body << "  - Unit ID: " << unit.id << "\n";
                body << "    Address: " << unit.api_address << "\n";
                body << "    Port: " << unit.api_port << "\n\n";
            }
        }

        body << "Email Notifications: ENABLED\n";
        body << "Email Server: " << config_manager_->get_email_server() << ":";
        body << config_manager_->get_email_port() << "\n";

        email_notifier_->send_email(
            config_manager_->get_email_address(),
            "Refrigeration API Web Interface Started",
            body.str()
        );

        write_log("APIWebInterface: Startup email sent successfully");
    } catch (const std::exception& e) {
        write_log("APIWebInterface: Error sending startup email: " + std::string(e.what()));
    }
}

void APIWebInterface::write_log(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time_t);

    std::ostringstream timestamp;
    timestamp << std::put_time(tm, "%Y-%m-%d %H:%M:%S");

    std::cout << "[" << timestamp.str() << "] [APIWebInterface] " << message << std::endl;
}
std::string APIWebInterface::handle_download_events_request(const std::string& date) {
    try {
        // Validate date format (YYYY-MM-DD)
        if (date.length() != 10 || date[4] != '-' || date[7] != '-') {
            write_log("APIWebInterface: Invalid date format provided: " + date);
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Invalid date format. Use YYYY-MM-DD\"}";
        }

        // Get the first unit to download logs from
        auto& units = config_manager_->get_units();
        if (units.empty()) {
            write_log("APIWebInterface: No units configured");
            return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\": \"No units configured\"}";
        }

        // Proxy the download request to the refrigeration API
        std::string endpoint = "/api/v1/logs/events?date=" + date;

        // Use curl to download the file from the refrigeration API
        CURL* curl = curl_easy_init();
        if (!curl) {
            write_log("APIWebInterface: Failed to initialize CURL");
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Failed to initialize download\"}";
        }

        const Unit& unit = units[0];
        std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + endpoint;
        std::string api_key_header = "X-API-Key: " + unit.api_key;

        std::string file_content;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, api_key_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, web_interface_curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file_content);

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            write_log("APIWebInterface: CURL error downloading events: " + std::string(curl_easy_strerror(res)));
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Download failed\"}";
        }

        if (http_code != 200) {
            write_log("APIWebInterface: HTTP " + std::to_string(http_code) + " downloading events from " + url);
            return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Log file not found for date: " + date + "\"}";
        }

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
        write_log("APIWebInterface: Exception downloading events - " + std::string(e.what()));
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"" + std::string(e.what()) + "\"}";
    }
}

std::string APIWebInterface::handle_download_conditions_request(const std::string& date) {
    try {
        // Validate date format (YYYY-MM-DD)
        if (date.length() != 10 || date[4] != '-' || date[7] != '-') {
            write_log("APIWebInterface: Invalid date format provided: " + date);
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Invalid date format. Use YYYY-MM-DD\"}";
        }

        // Get the first unit to download logs from
        auto& units = config_manager_->get_units();
        if (units.empty()) {
            write_log("APIWebInterface: No units configured");
            return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\": \"No units configured\"}";
        }

        // Proxy the download request to the refrigeration API
        std::string endpoint = "/api/v1/logs/conditions?date=" + date;

        // Use curl to download the file from the refrigeration API
        CURL* curl = curl_easy_init();
        if (!curl) {
            write_log("APIWebInterface: Failed to initialize CURL");
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Failed to initialize download\"}";
        }

        const Unit& unit = units[0];
        std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + endpoint;
        std::string api_key_header = "X-API-Key: " + unit.api_key;

        std::string file_content;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, api_key_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, web_interface_curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file_content);

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            write_log("APIWebInterface: CURL error downloading conditions: " + std::string(curl_easy_strerror(res)));
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Download failed\"}";
        }

        if (http_code != 200) {
            write_log("APIWebInterface: HTTP " + std::to_string(http_code) + " downloading conditions from " + url);
            return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\": \"Log file not found for date: " + date + "\"}";
        }

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
        write_log("APIWebInterface: Exception downloading conditions - " + std::string(e.what()));
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\": \"" + std::string(e.what()) + "\"}";
    }
}
