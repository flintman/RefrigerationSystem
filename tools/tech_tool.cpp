/*
 * Refrigeration Server Technician Tool
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * This project includes third-party software:
 * - OpenSSL (Apache License 2.0)
 * - FTXUI (MIT License)
 * - nlohmann/json (MIT License)
 */

#include "config_manager.h"
#include "sensor_manager.h"
#include "tools/api_client.h"
#include "tools/system_executor.h"
#include "tools/log_reader.h"
#include "tools/temperature_data_table.h"
#include "tools/dashboard_state.h"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/loop.hpp>

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <limits>
#include <chrono>
#include <unistd.h>

using namespace ftxui;

struct AppState {
    ConfigManager manager;
    SensorManager sensors;
    std::vector<std::string> latestSensorLines;
    std::mutex sensorMutex;
    std::atomic<bool> pollingActive{false};
    std::atomic<bool> polling1stfetch{false};
    std::string status_message;
};

void StartSensorPolling(AppState& state) {
    state.pollingActive = true;
    std::thread([&state]() {
        while (state.pollingActive) {
            auto lines = state.sensors.readOneWireTempSensors();
            {
                std::lock_guard<std::mutex> lock(state.sensorMutex);
                state.latestSensorLines = std::move(lines);
            }
            if (!state.polling1stfetch) {
                state.polling1stfetch = true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }).detach();
}

void StopSensorPolling(AppState& state) {
    state.pollingActive = false;
    state.polling1stfetch = false;
}

int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        std::cerr << "This tool must be run as root (sudo).\n";
        return 1;
    }

    const char* defaultConfig = "/etc/refrigeration/config.env";
    if (argc != 2) {
        FILE* file = fopen(defaultConfig, "r");
        if (!file) {
            std::cerr << "Usage: " << argv[0] << " <config_file_path>\n";
            std::cerr << "Default config file not found at " << defaultConfig << "\n";
            return 1;
        }
        fclose(file);
        argv[1] = const_cast<char*>(defaultConfig);
    }

    ConfigManager config(argv[1]);

    // Initialize API client
    std::string api_port_str = config.get("api.port");
    int api_port = 8095;
    try {
        api_port = std::stoi(api_port_str);
    } catch (...) {
        api_port = 8095;
    }

    APIClient api_client("localhost", api_port, config.get("api.key"));
    SystemCommandExecutor system_executor;
    LogReader log_reader;
    DashboardState dashboard_state;

    SensorManager sensors;
    std::atomic<bool> pollingActive{true};
    std::vector<std::string> latestSensorLines;
    std::mutex sensorMutex;
    auto screen = ScreenInteractive::Fullscreen();

    std::thread polling_thread([&] {
        while (pollingActive) {
            auto lines = sensors.readOneWireTempSensors();
            {
                std::lock_guard<std::mutex> lock(sensorMutex);
                latestSensorLines = std::move(lines);
            }
            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    auto schema = config.getSchema();
    int selected = 0;
    int config_scroll = 0;
    std::string edit_value;
    enum class Mode { View, Edit };
    Mode mode = Mode::View;
    std::string status_message;
    bool awaiting_confirmation = false;
    std::string confirmation_prompt;

    // Tab system
    enum class TabPage { Config, Dashboard };
    TabPage current_tab = TabPage::Config;

    Component config_table = Renderer([&] {
        std::vector<Element> rows;
        std::vector<std::string> keys;
        for (const auto& [key, _] : schema) keys.push_back(key);

        const int config_height = 20;
        int total_configs = keys.size();
        int max_scroll = std::max(0, total_configs - config_height);

        if (selected < config_scroll) {
            config_scroll = selected;
        } else if (selected >= config_scroll + config_height) {
            config_scroll = selected - config_height + 1;
        }

        config_scroll = std::max(0, std::min(config_scroll, max_scroll));

        if (schema.empty()) {
            rows.push_back(text("[No config schema loaded]") | color(Color::White));
        } else {
            int start_idx = config_scroll;
            int end_idx = std::min(start_idx + config_height, (int)keys.size());

            for (int i = start_idx; i < end_idx; ++i) {
                const auto& [key, entry] = *std::next(schema.begin(), i);
                bool is_selected = (i == selected);
                std::string value = config.get(key);
                std::vector<Element> row_cells;
                if (is_selected) {
                    row_cells.push_back(text("> ") | color(Color::GreenLight));
                    row_cells.push_back(text(key) | bold);
                } else {
                    row_cells.push_back(text("  "));
                    row_cells.push_back(text(key));
                }
                row_cells.push_back(text(" = ") | color(Color::White));
                row_cells.push_back(text(value) | color(Color::YellowLight));
                row_cells.push_back(text(" (default: " + entry.defaultValue + ")") | color(Color::White));
                Element row = hbox(row_cells) | (is_selected ? bgcolor(Color::GrayDark) : nothing);
                rows.push_back(row);
            }

            if (mode == Mode::Edit && !keys.empty()) {
                std::string value = config.get(keys[selected]);
                static auto last_blink = std::chrono::steady_clock::now();
                static bool cursor_on = true;
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_blink).count() > 500) {
                    cursor_on = !cursor_on;
                    last_blink = now;
                }
                std::string display_value = edit_value.empty() ? value : edit_value;
                if (cursor_on) {
                    display_value += "\u2588";
                } else {
                    display_value += " ";
                }
                int vis_idx = selected - config_scroll;
                if (vis_idx >= 0 && vis_idx < (int)rows.size()) {
                    rows[vis_idx] = hbox({
                        text("> ") | color(Color::GreenLight),
                        text(keys[selected]) | bold,
                        text(" = "),
                        text(display_value) | color(Color::YellowLight),
                        text(" (default: " + schema.at(keys[selected]).defaultValue + ")") | color(Color::White)
                    }) | bgcolor(Color::GrayDark);
                }
            }
        }
        return vbox({
            text("Configuration") | bold | color(Color::Cyan) | bgcolor(Color::Blue),
            separatorEmpty(),
            vbox(std::move(rows)),
        }) | border | bgcolor(Color::Blue) | size(HEIGHT, GREATER_THAN, config_height);
    });

    Component sensor_panel = Renderer([&] {
        std::vector<Element> lines;
        lines.push_back(text("Sensors") | bold | color(Color::White) | bgcolor(Color::Blue));
        std::lock_guard<std::mutex> lock(sensorMutex);
        if (latestSensorLines.empty()) {
            lines.push_back(text("[No sensor data yet]") | color(Color::White));
        } else {
            for (const auto& line : latestSensorLines) {
                lines.push_back(text(line) | color(Color::White));
            }
        }
        return vbox({
            text("Sensors") | bold | color(Color::White) | bgcolor(Color::Blue),
            separatorEmpty(),
            vbox(std::move(lines)),
        }) | border | bgcolor(Color::Blue) | size(HEIGHT, LESS_THAN, 20);
    });

    Component status_bar = Renderer([&] {
        std::string mode_str = (mode == Mode::Edit) ? "[EDIT MODE]" : "[VIEW MODE]";
        std::string display_msg = awaiting_confirmation ? confirmation_prompt : status_message;
        return hbox({
            text(mode_str) | bold | color(mode == Mode::Edit ? Color::RedLight : Color::GreenLight) | bgcolor(Color::Black),
            text("  "),
            text(display_msg) | color(Color::White) | bgcolor(Color::Black)
        }) | hcenter | bgcolor(Color::Black);
    });

    Component help_panel = Renderer([&] {
        return vbox({
            text("Refrigeration System Technician Tool") | bold | color(Color::White) | bgcolor(Color::Blue),
            separator(),
            text("Navigation:") | bold | color(Color::White),
            text("  Tab: Switch between tabs (Config / Dashboard)") | color(Color::YellowLight),
            text("  ↑/↓  Up/Down arrows: Move selection") | color(Color::White),
            text("  E: Toggle EDIT/VIEW MODE (shuts down refrigeration, allows editing)") | color(Color::YellowLight),
            text("  e: Edit selected value (in EDIT MODE only)") | color(Color::YellowLight),
            text("  d: Reset to default (in EDIT MODE only)") | color(Color::YellowLight),
            text("  Enter: Save edit") | color(Color::GreenLight),
            text("  Esc: Cancel edit or quit") | color(Color::RedLight),
            text("  q: Quit") | color(Color::RedLight),
            separator(),
            text("Tip: Sensor data updates live. Config changes are saved instantly.") | color(Color::White),
        }) | border | bgcolor(Color::Blue);
    });

    Component tab_header = Renderer([&] {
        Element config_tab = text(" Config Editor ") | bold |
            (current_tab == TabPage::Config ? bgcolor(Color::GreenLight) | color(Color::Black) : bgcolor(Color::GrayDark));
        Element dashboard_tab = text(" Dashboard ") | bold |
            (current_tab == TabPage::Dashboard ? bgcolor(Color::GreenLight) | color(Color::Black) : bgcolor(Color::GrayDark));

        return hbox({
            config_tab,
            text(" | ") | color(Color::White),
            dashboard_tab
        }) | bgcolor(Color::Blue) | hcenter;
    });

    Component main_container = CatchEvent(Renderer([&] {
        if (current_tab == TabPage::Dashboard) {
            // API Health Status block
            Color api_status_color = dashboard_state.dashboard_message.find("✓") != std::string::npos ? Color::GreenLight : Color::RedLight;
            Element health_block = vbox({
                text("API Health Check:") | bold | color(Color::White),
                text(dashboard_state.dashboard_message) | bold | color(api_status_color),
                separator(),
                hbox({
                    text("[R] Refresh Page  ") | color(Color::YellowLight) | bold,
                    text("[Q] Quit dashboard") | color(Color::White) | bold
                })
            }) | border | bgcolor(Color::Blue);

            // Service control block
            Element service_block = vbox({
                text("Service Controls:") | bold | color(Color::White),
                separator(),
                hbox({
                    text("[S] Start  ") | color(Color::GreenLight) | bold,
                    text("[T] Stop  ") | color(Color::RedLight) | bold,
                    text("[X] Restart") | color(Color::YellowLight) | bold
                }),
                hbox({
                    text("[D] Demo: ") | bold | color(Color::White),
                    text(dashboard_state.demo_mode ? "ON " : "OFF") |
                        color(dashboard_state.demo_mode ? Color::CyanLight : Color::GreenLight) | bold
                })
            }) | border | bgcolor(Color::Blue);            // System status and control block
            std::vector<Element> status_elems;
            status_elems.push_back(text("System Status:") | bold | color(Color::White));
            Color service_color = dashboard_state.api_is_healthy ? Color::GreenLight : Color::RedLight;
            std::string service_text = "[Unknown]";
            if (dashboard_state.cached_status.contains("system_status") &&
                dashboard_state.cached_status["system_status"].is_string()) {
                service_text = dashboard_state.cached_status["system_status"].get<std::string>();
            }
            status_elems.push_back(text("Service: " + service_text) | bold | color(service_color));

            // Display relay status
            if (dashboard_state.cached_status.contains("relays") && dashboard_state.cached_status["relays"].is_object()) {
                auto relays = dashboard_state.cached_status["relays"];
                std::vector<Element> relay_line;
                relay_line.push_back(text("Relays: ") | bold | color(Color::White));

                if (relays.contains("compressor")) {
                    bool comp = relays["compressor"].get<bool>();
                    relay_line.push_back(text("Comp:" + std::string(comp ? "ON" : "OFF")) |
                        color(comp ? Color::GreenLight : Color::RedLight));
                    relay_line.push_back(text(" "));
                }
                if (relays.contains("fan")) {
                    bool fan = relays["fan"].get<bool>();
                    relay_line.push_back(text("Fan:" + std::string(fan ? "ON" : "OFF")) |
                        color(fan ? Color::GreenLight : Color::RedLight));
                    relay_line.push_back(text(" "));
                }
                if (relays.contains("valve")) {
                    bool valve = relays["valve"].get<bool>();
                    relay_line.push_back(text("Valve:" + std::string(valve ? "ON" : "OFF")) |
                        color(valve ? Color::GreenLight : Color::RedLight));
                    relay_line.push_back(text(" "));
                }
                if (relays.contains("electric_heater")) {
                    bool heater = relays["electric_heater"].get<bool>();
                    relay_line.push_back(text("Heat:" + std::string(heater ? "ON" : "OFF")) |
                        color(heater ? Color::GreenLight : Color::RedLight));
                }
                status_elems.push_back(hbox(relay_line));
            }

            // Display sensor readings
            if (dashboard_state.cached_status.contains("sensors") && dashboard_state.cached_status["sensors"].is_object()) {
                auto sensors = dashboard_state.cached_status["sensors"];
                std::vector<Element> sensor_line;
                sensor_line.push_back(text("Sensors (°F): ") | bold | color(Color::White));

                if (sensors.contains("return_temp")) {
                    float rt = sensors["return_temp"].get<float>();
                    sensor_line.push_back(text("Ret:" + std::to_string(rt).substr(0, 5)) | color(Color::YellowLight));
                    sensor_line.push_back(text(" "));
                }
                if (sensors.contains("supply_temp")) {
                    float st = sensors["supply_temp"].get<float>();
                    sensor_line.push_back(text("Sup:" + std::to_string(st).substr(0, 5)) | color(Color::YellowLight));
                    sensor_line.push_back(text(" "));
                }
                if (sensors.contains("coil_temp")) {
                    float ct = sensors["coil_temp"].get<float>();
                    sensor_line.push_back(text("Coil:" + std::to_string(ct).substr(0, 5)) | color(Color::YellowLight));
                }
                status_elems.push_back(hbox(sensor_line));
            }

            // Display setpoint
            if (dashboard_state.cached_status.contains("setpoint")) {
                float sp = dashboard_state.cached_status["setpoint"].get<float>();
                status_elems.push_back(text("Setpoint: " + std::to_string(sp).substr(0, 5) + "°F") |
                    color(Color::YellowLight) | bold);
            }

            status_elems.push_back(text("Controls:") | bold | color(Color::White));
            if (dashboard_state.api_is_healthy &&
                dashboard_state.cached_status.contains("sensors") &&
                dashboard_state.cached_status["sensors"].contains("coil_temp") &&
                schema.find("defrost.coil_temperature") != schema.end())
            {
                float coil_temp = dashboard_state.cached_status["sensors"]["coil_temp"].get<float>();
                float defrost_temp = 0.0f;
                try {
                    defrost_temp = std::stof(config.get("defrost.coil_temperature"));
                } catch (...) {
                    defrost_temp = 0.0f;
                }
                if (coil_temp < defrost_temp) {
                    status_elems.push_back(text("[F] Trigger Defrost") | color(Color::YellowLight) | bold);
                }
            }

            if (dashboard_state.api_is_healthy && dashboard_state.has_alarm) {
                status_elems.push_back(text("[A] Reset Alarm") | color(Color::RedLight) | bold);
            } else if (dashboard_state.api_is_healthy &&
                       dashboard_state.cached_status.contains("system_status") &&
                       dashboard_state.cached_status["system_status"].is_string() &&
                       dashboard_state.cached_status["system_status"].get<std::string>() == "Alarm") {
                status_elems.push_back(text("[A] Reset Alarm") | color(Color::RedLight) | bold);
            }

            if (!dashboard_state.control_response.empty()) {
                status_elems.push_back(separator());
                status_elems.push_back(text(dashboard_state.control_response) | color(Color::White));
            }

            Element status_block = vbox(status_elems) | border | bgcolor(Color::Blue);

            // Temperature data table block
            std::vector<Element> graph_elems;
            graph_elems.push_back(text("Temp History (Last 6h) [ ]") | bold | color(Color::White));

            // Regenerate table with current scroll position
            auto temp_condition_data = TemperatureDataTable::ReadLast6Hours();
            auto temp_table = TemperatureDataTable::FormatAsTable(temp_condition_data, 4, dashboard_state.temp_data_scroll);

            for (const auto& table_line : temp_table) {
                graph_elems.push_back(text(table_line) | color(Color::White));
            }
            Element graph_block = vbox(graph_elems) | border | bgcolor(Color::Blue);

            // Event log block (scrollable)
            int log_height = 4;
            int total_lines = dashboard_state.log_lines.size();
            int start_line = std::max(0, total_lines - log_height - dashboard_state.log_scroll);
            int end_line = std::min(total_lines, start_line + log_height);
            std::vector<Element> log_elems;
            log_elems.push_back(text("Event Logs:") | bold | color(Color::White));
            for (int i = start_line; i < end_line; ++i) {
                log_elems.push_back(text(dashboard_state.log_lines[i]) | color(Color::White));
            }
            log_elems.push_back(text("↑/↓: Scroll") | color(Color::White));
            Element log_block = vbox(log_elems) | border | bgcolor(Color::Blue);

            // Build dashboard with conditional status block
            std::vector<Element> dashboard_items;
            dashboard_items.push_back(text("Refrigeration System - API & Service Dashboard") | bold | color(Color::White) | bgcolor(Color::Blue) | hcenter);
            dashboard_items.push_back(separator());

            // Health check and service controls side-by-side
            dashboard_items.push_back(hbox({
                health_block | flex,
                separatorEmpty(),
                service_block | flex
            }) | flex);

            dashboard_items.push_back(separator());

            // System status and temperature graph side-by-side
            if (dashboard_state.api_is_healthy) {
                dashboard_items.push_back(hbox({
                    status_block,
                    separatorEmpty(),
                    graph_block
                }) | flex);
                dashboard_items.push_back(separator());
            }

            dashboard_items.push_back(log_block);

            return vbox({
                tab_header->Render(),
                separator(),
                vbox(dashboard_items) | bgcolor(Color::Blue) | flex
            }) | bgcolor(Color::Blue);
        } else {
            return vbox({
                tab_header->Render(),
                separator(),
                hbox({
                    help_panel->Render() | flex,
                    separatorEmpty(),
                    sensor_panel->Render() | flex
                }),
                separator(),
                status_bar->Render(),
                separatorEmpty(),
                config_table->Render(),
                separatorEmpty()
            }) | bgcolor(Color::Black);
        }
    }), [&](Event event) {
        std::vector<std::string> keys;
        for (const auto& [key, _] : schema) keys.push_back(key);
        static bool editing_line = false;

        // Tab key to switch between tabs (not allowed in Edit mode)
        if (event == Event::Tab) {
            if (mode == Mode::Edit) {
                status_message = "Cannot switch tabs in EDIT MODE. Press E to exit first.";
                return true;
            }
            if (current_tab == TabPage::Config) {
                current_tab = TabPage::Dashboard;
                dashboard_state.dashboard_message = "Switching to Dashboard...";
                dashboard_state.UpdateHealthStatus(api_client.CheckHealth());
                dashboard_state.log_lines = log_reader.ReadEventsLog();
                auto condition_data = TemperatureDataTable::ReadLast6Hours();
                dashboard_state.temperature_graph = TemperatureDataTable::FormatAsTable(condition_data, 6, dashboard_state.temp_data_scroll);
                if (dashboard_state.api_is_healthy) {
                    dashboard_state.cached_status = api_client.GetStatus("/status");
                }
            } else {
                current_tab = TabPage::Config;
            }
            return true;
        }

        if (current_tab == TabPage::Dashboard) {
            if (event == Event::Character('q') || event == Event::Character('Q') || event == Event::Escape) {
                screen.Exit();
                return true;
            }
            if (event == Event::Character('r') || event == Event::Character('R')) {
                dashboard_state.dashboard_message = "Checking API health...";
                dashboard_state.UpdateHealthStatus(api_client.CheckHealth());
                dashboard_state.log_lines = log_reader.ReadEventsLog();
                auto condition_data = TemperatureDataTable::ReadLast6Hours();
                dashboard_state.temperature_graph = TemperatureDataTable::FormatAsTable(condition_data, 6, dashboard_state.temp_data_scroll);
                if (dashboard_state.api_is_healthy) {
                    dashboard_state.cached_status = api_client.GetStatus("/status");
                }
                dashboard_state.control_response.clear();
                return true;
            }
            if (event == Event::Character('s') || event == Event::Character('S')) {
                dashboard_state.dashboard_message = system_executor.StartService();
                return true;
            }
            if (event == Event::Character('t') || event == Event::Character('T')) {
                dashboard_state.dashboard_message = system_executor.StopService();
                return true;
            }
            if (event == Event::Character('x') || event == Event::Character('X')) {
                dashboard_state.dashboard_message = system_executor.RestartService();
                return true;
            }
            if (event == Event::Character('d') || event == Event::Character('D')) {
                // Toggle demo mode
                dashboard_state.demo_mode = !dashboard_state.demo_mode;
                auto response = api_client.SetDemoMode(dashboard_state.demo_mode);
                if (response.contains("success") && response["success"].is_boolean()) {
                    dashboard_state.control_response = dashboard_state.demo_mode ? "Demo mode ENABLED" : "Demo mode DISABLED";
                } else {
                    dashboard_state.control_response = "Failed to toggle demo mode";
                }
                return true;
            }
            if (event == Event::Character('f') || event == Event::Character('F')) {
                dashboard_state.control_response = api_client.PostControl("/defrost/trigger");
                return true;
            }
            if (event == Event::Character('a') || event == Event::Character('A')) {
                dashboard_state.control_response = api_client.PostControl("/alarms/reset");
                return true;
            }
            if (event == Event::ArrowUp) {
                if (dashboard_state.log_scroll < (int)dashboard_state.log_lines.size() - 8)
                    dashboard_state.log_scroll++;
                return true;
            }
            if (event == Event::ArrowDown) {
                if (dashboard_state.log_scroll > 0) dashboard_state.log_scroll--;
                return true;
            }
            // Use [ and ] for temperature data scrolling
            if (event == Event::Character('[')) {
                auto condition_data = TemperatureDataTable::ReadLast6Hours();
                int max_scroll = std::max(0, (int)condition_data.size() - 6);
                if (dashboard_state.temp_data_scroll < max_scroll) {
                    dashboard_state.temp_data_scroll++;
                }
                return true;
            }
            if (event == Event::Character(']')) {
                if (dashboard_state.temp_data_scroll > 0) {
                    dashboard_state.temp_data_scroll--;
                }
                return true;
            }
            return false;
        }

        if (mode == Mode::Edit) {
            if (event == Event::Character('m')) {
                status_message = "Service dashboard only available in VIEW MODE.";
                return true;
            }
            if (event == Event::ArrowDown) {
                int sz = schema.size();
                if (selected + 1 < sz) selected++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (selected > 0) selected--;
                return true;
            }
            // Handle confirmation prompt during edit mode
            if (awaiting_confirmation) {
                if (event == Event::Character('y') || event == Event::Character('Y')) {
                    // User confirmed - reset to default
                    if (!keys.empty() && selected < (int)keys.size()) {
                        config.set(keys[selected], schema.at(keys[selected]).defaultValue);
                        status_message = "Reset to default: " + schema.at(keys[selected]).defaultValue;
                    }
                    awaiting_confirmation = false;
                    confirmation_prompt.clear();
                    return true;
                } else if (event == Event::Character('n') || event == Event::Character('N')) {
                    // User declined confirmation
                    awaiting_confirmation = false;
                    confirmation_prompt.clear();
                    status_message = "Cancelled.";
                    return true;
                }
                return false;  // Ignore other keys during confirmation
            }

            // Edit mode: 'e' to edit value
            if (event == Event::Character('e')) {
                edit_value.clear();
                editing_line = true;
                status_message = "Editing " + ((!keys.empty() && selected < (int)keys.size()) ? keys[selected] : "value") + ": Type new value, press Enter to save, Backspace to delete";
                return true;
            }
            // Edit mode: 'd' to reset to default
            if (event == Event::Character('d')) {
                if (!keys.empty() && selected < (int)keys.size()) {
                    awaiting_confirmation = true;
                    confirmation_prompt = "Reset " + keys[selected] + " to default? (y/n)";
                }
                return true;
            }
            // Handle text input when editing
            if (editing_line) {
                if (event == Event::Character('\n') || event == Event::Character('\r')) {
                    // Enter - save the value
                    if (!keys.empty() && selected < (int)keys.size() && !edit_value.empty()) {
                        config.set(keys[selected], edit_value);
                        status_message = "Saved: " + edit_value;
                    } else if (edit_value.empty()) {
                        status_message = "Empty value, not saved.";
                    }
                    edit_value.clear();
                    editing_line = false;
                    return true;
                } else if (event == Event::Character('\b')) {
                    // Backspace - delete last character
                    if (!edit_value.empty()) {
                        edit_value.pop_back();
                    }
                    return true;
                } else {
                    // Try to handle printable ASCII characters
                    for (char c = 32; c < 127; c++) {
                        if (event == Event::Character(c)) {
                            edit_value.push_back(c);
                            return true;
                        }
                    }
                }
            }
            if (event == Event::Character('E') || event == Event::Escape ) {
                mode = Mode::View;
                edit_value.clear();
                editing_line = false;
                status_message = "Returned to VIEW MODE.";
                return true;
            }
            return false;
        } else {
            if (event == Event::Character('E')) {
                status_message = "Stopping refrigeration service... Please wait.";
                std::thread kill_thread([&]() {
                    system_executor.KillRefrigerationProcess();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    mode = Mode::Edit;
                    status_message = "Refrigeration service stopped. Entered EDIT MODE. Press E/q/Esc to exit.";
                });
                kill_thread.detach();
                return true;
            }
            if (event == Event::Character('q')) {
                screen.Exit();
                return true;  // Already handled above for Dashboard tab
            }
        }

        return false;
    });

    screen.Loop(main_container);
    pollingActive = false;
    if (polling_thread.joinable()) polling_thread.join();
    return 0;
}
