#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

/**
 * DashboardState manages all state variables for the service dashboard
 * Provides a centralized way to access and update dashboard data
 */
class DashboardState {
public:
    // Show/hide dashboard
    bool show_service_dashboard;

    // API status
    std::string dashboard_message;
    bool api_is_healthy;
    nlohmann::json cached_status;
    bool demo_mode;

    // Log display
    int log_scroll;
    std::vector<std::string> log_lines;
    std::vector<std::string> temperature_graph;
    int temp_data_scroll;  // Scroll position for temperature data table

    // System status
    float current_coil_temp;
    float defrost_coil_threshold;
    bool has_alarm;
    std::string current_mode;

    // Control responses
    std::string control_response;

    /**
     * Create new dashboard state with default values
     */
    DashboardState();

    /**
     * Reset all state to initial values
     */
    void Reset();

    /**
     * Update health status and related flags
     */
    void UpdateHealthStatus(const std::string& health_message);

    /**
     * Update has_alarm based on API alarm fields (active_alarms, alarm_warning, alarm_shutdown)
     */
    void UpdateAlarmStatus();
};
