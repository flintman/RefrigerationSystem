#include "tools/dashboard_state.h"

DashboardState::DashboardState()
    : show_service_dashboard(false),
      api_is_healthy(false),
      demo_mode(false),
      log_scroll(0),
      temp_data_scroll(0),
      current_coil_temp(0.0f),
      defrost_coil_threshold(0.0f),
      has_alarm(false) {
    cached_status = nlohmann::json::object();
}

void DashboardState::Reset() {
    show_service_dashboard = false;
    dashboard_message.clear();
    api_is_healthy = false;
    demo_mode = false;
    cached_status = nlohmann::json::object();
    log_scroll = 0;
    temp_data_scroll = 0;
    log_lines.clear();
    temperature_graph.clear();
    current_coil_temp = 0.0f;
    defrost_coil_threshold = 0.0f;
    has_alarm = false;
    current_mode.clear();
    control_response.clear();
}

void DashboardState::UpdateHealthStatus(const std::string& health_message) {
    dashboard_message = health_message;
    api_is_healthy = (health_message.find("✓") != std::string::npos);

}

void DashboardState::UpdateAlarmStatus() {
    has_alarm = false;
    if (cached_status.contains("alarm_warning") && cached_status["alarm_warning"].is_boolean()) {
        has_alarm = has_alarm || cached_status["alarm_warning"].get<bool>();
    }
    if (cached_status.contains("alarm_shutdown") && cached_status["alarm_shutdown"].is_boolean()) {
        has_alarm = has_alarm || cached_status["alarm_shutdown"].get<bool>();
    }
    if (cached_status.contains("active_alarms") && cached_status["active_alarms"].is_array()) {
        has_alarm = has_alarm || !cached_status["active_alarms"].empty();
    }
}
