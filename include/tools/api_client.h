#pragma once

#include <string>
#include <nlohmann/json.hpp>

/**
 * APIClient handles all HTTP API communication with the refrigeration system
 * Provides methods for health checks, status queries, and control operations
 */
class APIClient {
public:
    APIClient(const std::string& host, int port, const std::string& api_key);

    /**
     * Initialize API configuration
     */
    void Initialize(const std::string& host, int port);

    /**
     * Perform a health check on the API
     * Returns formatted health string with checkmark/X and timestamp
     */
    std::string CheckHealth();

    /**
     * Make a GET request to an API endpoint and return JSON response
     */
    nlohmann::json GetStatus(const std::string& endpoint);

    /**
     * Make a POST control call to an API endpoint
     * Returns response as string
     */
    std::string PostControl(const std::string& endpoint);

    /**
     * Set demo mode on/off
     * Returns JSON response with status
     */
    nlohmann::json SetDemoMode(bool enable);

    /**
     * Get current demo mode status
     * Returns JSON response with demo_mode boolean
     */
    nlohmann::json GetDemoMode();

    /**
     * Set the API key for authentication
     */
    void SetAPIKey(const std::string& api_key);

private:
    std::string api_base_url;
    std::string api_key;

    /**
     * Internal helper to execute curl commands
     */
    std::string ExecuteCurl(const std::string& method, const std::string& endpoint, bool json_output = false);
};
