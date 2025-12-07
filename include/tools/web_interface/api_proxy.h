/*
 * API Proxy
 * Handles API calls to refrigeration units
 */

#ifndef API_PROXY_H
#define API_PROXY_H

#include "config_manager.h"
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class APIProxy {
public:
    APIProxy();
    ~APIProxy();

    // API operations
    json call_unit_api(const Unit& unit, const std::string& endpoint);
    json get_system_info(const Unit& unit);
    json get_status(const Unit& unit);
    json get_demo_mode(const Unit& unit);
    json get_logs(const Unit& unit);

    // POST operations
    json set_demo_mode(const Unit& unit, const json& data);
    json set_setpoint(const Unit& unit, const json& data);
    json set_config(const Unit& unit, const json& data);
    json reset_alarms(const Unit& unit);
    json trigger_defrost(const Unit& unit);

    // Response helpers
    std::string format_error_response(const std::string& error);
    std::string format_success_response(const json& data);

private:
    std::string base_url_;

    json perform_http_request(const std::string& url, const std::string& method,
                             const std::string& body, const std::string& api_key);

    void write_log(const std::string& message);
};

#endif // API_PROXY_H
