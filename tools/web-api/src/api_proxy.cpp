#include "../include/tools/web_interface/api_proxy.h"
#include <curl/curl.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>

using json = nlohmann::json;

// CURL callback for writing response data
static size_t api_proxy_curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

APIProxy::APIProxy() {
}

APIProxy::~APIProxy() {
}

json APIProxy::call_unit_api(const Unit& unit, const std::string& endpoint) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1" + endpoint;
    return perform_http_request(url, "GET", "", unit.api_key);
}

json APIProxy::get_system_info(const Unit& unit) {
    // Fetch both status and system-info for complete data
    std::string base_url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1";

    json status = perform_http_request(base_url + "/status", "GET", "", unit.api_key);
    json config = perform_http_request(base_url + "/system-info", "GET", "", unit.api_key);
    json demo_data = perform_http_request(base_url + "/demo-mode", "GET", "", unit.api_key);

    if (status.is_object()) {
        // Merge config and demo data into status
        for (auto& el : config.items()) {
            status[el.key()] = el.value();
        }
        for (auto& el : demo_data.items()) {
            status[el.key()] = el.value();
        }
        return status;
    }

    return json::object();
}

json APIProxy::get_status(const Unit& unit) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/status";
    return perform_http_request(url, "GET", "", unit.api_key);
}

json APIProxy::get_demo_mode(const Unit& unit) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/demo-mode";
    return perform_http_request(url, "GET", "", unit.api_key);
}

json APIProxy::get_logs(const Unit& unit) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/logs";
    return perform_http_request(url, "GET", "", unit.api_key);
}

std::string APIProxy::format_error_response(const std::string& error) {
    json response;
    response["error"] = error;
    return response.dump();
}

std::string APIProxy::format_success_response(const json& data) {
    json response;
    response["success"] = true;
    response["data"] = data;
    return response.dump();
}

json APIProxy::perform_http_request(const std::string& url, const std::string& method,
                                    const std::string& body, const std::string& api_key) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        write_log("APIProxy: Failed to initialize CURL");
        return json::object();
    }

    std::string response_string;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-API-Key: " + api_key).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_proxy_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Accept self-signed certs
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Set HTTP method and body
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        }
    } else if (method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    write_log("APIProxy: Calling " + method + " " + url);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        write_log("APIProxy: ERROR - Failed to call API: " + std::string(curl_easy_strerror(res)));
        return json::object();
    }

    try {
        write_log("APIProxy: Response received, size: " + std::to_string(response_string.length()));
        return json::parse(response_string);
    } catch (const std::exception& e) {
        write_log("APIProxy: ERROR - Invalid JSON response: " + std::string(e.what()));
        return json::object();
    }
}

json APIProxy::set_demo_mode(const Unit& unit, const json& data) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/demo-mode";
    std::string body = data.dump();
    return perform_http_request(url, "POST", body, unit.api_key);
}

json APIProxy::set_setpoint(const Unit& unit, const json& data) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/setpoint";
    std::string body = data.dump();
    return perform_http_request(url, "POST", body, unit.api_key);
}

json APIProxy::set_config(const Unit& unit, const json& data) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/config";
    std::string body = data.dump();
    return perform_http_request(url, "POST", body, unit.api_key);
}

json APIProxy::reset_alarms(const Unit& unit) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/alarms/reset";
    return perform_http_request(url, "POST", "", unit.api_key);
}

json APIProxy::trigger_defrost(const Unit& unit) {
    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1/defrost/trigger";
    return perform_http_request(url, "POST", "", unit.api_key);
}

void APIProxy::write_log(const std::string& message) {
    std::string timestamp;
    {
        time_t now = std::time(nullptr);
        struct tm* timeinfo = std::localtime(&now);
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        timestamp = buffer;
    }

    std::string log_message = "[" + timestamp + "] [APIProxy] " + message;

    // Log to console
    std::cout << log_message << std::endl;

    // Log to file
    std::ofstream log_file("/var/log/refrigeration-api.log", std::ios::app);
    if (log_file.is_open()) {
        log_file << log_message << std::endl;
        log_file.close();
    }
}
