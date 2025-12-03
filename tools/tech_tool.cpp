/*
 * Refrigeration Server
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * This project includes third-party software:
 * - OpenSSL (Apache License 2.0)
 * - ws2811 (MIT License)
 * - nlohmann/json (MIT License)
 */

#include "config_manager.h"
#include "sensor_manager.h"
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
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/wait.h>
#include <chrono>

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

// Helper to kill refrigeration process (unchanged)
int KillRefrigerationProcess() {
    system("sudo systemctl stop refrigeration.service");
    FILE* pipe = popen("pgrep -x refrigeration", "r");
    if (!pipe) return 0;
    char buffer[128];
    bool killed = false;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        int pid = atoi(buffer);
        if (pid > 0) {
            kill(pid, SIGINT);
            killed = true;
        }
    }
    pclose(pipe);
    if (killed) {
        while (true) {
            FILE* check = popen("pgrep -x refrigeration", "r");
            bool running = (fgets(buffer, sizeof(buffer), check) != nullptr);
            pclose(check);
            if (!running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    return killed ? 1 : 0;
}

std::string RunCommandAndGetOutput(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "[Failed to run command]";
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// Helper to get today's events log file
std::string GetTodaysEventLogPath() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return std::string("/var/log/refrigeration/events-") + buffer + ".log";
}

// Helper to read events log file (read-only stream if service active)
std::vector<std::string> ReadEventsLog() {
    std::string log_path = GetTodaysEventLogPath();
    std::vector<std::string> lines;
    FILE* file = fopen(log_path.c_str(), "r");
    if (!file) {
        lines.push_back("[Log file not found: " + log_path + "]");
        return lines;
    }
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), file)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        lines.push_back(buffer);
    }
    fclose(file);
    return lines;
}

int main(int argc, char* argv[]) {
    // Refrigeration service dashboard state
    static bool show_service_dashboard = false;
    static int log_scroll = 0;
    static std::vector<std::string> log_lines;
    static std::atomic<bool> dashboard_log_polling{false};
    static std::thread dashboard_log_thread;
    static std::string service_status;
    static std::string dashboard_message;
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

    SensorManager sensors;
    std::atomic<bool> pollingActive{true};
    std::vector<std::string> latestSensorLines;
    std::mutex sensorMutex;
    auto screen = ScreenInteractive::Fullscreen();  // Create screen early for posting events
    std::thread polling_thread([&] {
        while (pollingActive) {
            auto lines = sensors.readOneWireTempSensors();
            {
                std::lock_guard<std::mutex> lock(sensorMutex);
                latestSensorLines = std::move(lines);
            }
            screen.PostEvent(Event::Custom);  // Force UI refresh when sensors update
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    ConfigManager config(argv[1]);
    auto schema = config.getSchema();
    int selected = 0;
    int config_scroll = 0;  // Scroll offset for config table
    int sensor_suggestion_selected = 0;  // For sensor ID selection
    std::string edit_value;
    enum class Mode { View, Edit };
    Mode mode = Mode::View;
    std::string status_message;
    bool confirm_revert = false;
    Component config_table = Renderer([&] {
        std::vector<Element> rows;
        std::vector<std::string> keys;
        for (const auto& [key, _] : schema) keys.push_back(key);

        const int config_height = 20;  // Visible height
        int total_configs = keys.size();
        int max_scroll = std::max(0, total_configs - config_height);

        // Adjust scroll if selected is out of view
        if (selected < config_scroll) {
            config_scroll = selected;
        } else if (selected >= config_scroll + config_height) {
            config_scroll = selected - config_height + 1;
        }

        // Clamp scroll to valid range
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
                // Blinking cursor logic
                static auto last_blink = std::chrono::steady_clock::now();
                static bool cursor_on = true;
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_blink).count() > 500) {
                    cursor_on = !cursor_on;
                    last_blink = now;
                }
                std::string display_value = edit_value.empty() ? value : edit_value;
                if (cursor_on) {
                    display_value += "\u2588"; // Unicode full block as cursor
                } else {
                    display_value += " ";
                }
                // Update the row in the visible list if it's the selected one
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
        return hbox({
            text(mode_str) | bold | color(mode == Mode::Edit ? Color::RedLight : Color::GreenLight) | bgcolor(Color::Black),
            text("  "),
            text(status_message) | color(Color::White) | bgcolor(Color::Black)
        }) | hcenter | bgcolor(Color::Black);
    });

    Component help_panel = Renderer([&] {
        return vbox({
            text("Refrigeration System Technician Tool") | bold | color(Color::White) | bgcolor(Color::Blue),
            separator(),
            text("Navigation:") | bold | color(Color::White),
            text("  ↑/↓  Up/Down arrows: Move selection") | color(Color::White),
            text("  E: Toggle EDIT/VIEW MODE (shuts down refrigeration, allows editing)") | color(Color::YellowLight),
            text("  e: Edit selected value (in EDIT MODE only)") | color(Color::YellowLight),
            text("  d: Reset to default (in EDIT MODE only)") | color(Color::YellowLight),
            text("  Enter: Save edit") | color(Color::GreenLight),
            text("  Esc: Cancel edit or quit") | color(Color::RedLight),
            text("  m: Service dashboard") | color(Color::White),
            text("  q: Quit") | color(Color::RedLight),
            separator(),
            text("Tip: Sensor data updates live. Config changes are saved instantly.") | color(Color::White),
        }) | border | bgcolor(Color::Blue);
    });
    Component main_container = CatchEvent(Renderer([&] {
        if (show_service_dashboard) {
            // Service status block
            Element status_block = vbox({
                text("refrigeration.service status:") | bold | color(Color::White),
                text(service_status) | color(service_status.find("running") != std::string::npos ? Color::GreenLight : Color::RedLight),
                separator(),
                hbox({
                    text("[S] Start  ") | color(Color::GreenLight) | bold,
                    text("[T] Stop  ") | color(Color::RedLight) | bold,
                    text("[R] Restart  ") | color(Color::YellowLight) | bold,
                    text("[Q] Quit dashboard") | color(Color::White) | bold
                }),
                separator(),
                text(dashboard_message) | color(Color::White)
            }) | border | bgcolor(Color::Blue);

            // Log block (scrollable)
            int log_height = 15;
            int total_lines = log_lines.size();
            int start_line = std::max(0, total_lines - log_height - log_scroll);
            int end_line = std::min(total_lines, start_line + log_height);
            std::vector<Element> log_elems;
            log_elems.push_back(text("Recent Logs:") | bold | color(Color::White));
            for (int i = start_line; i < end_line; ++i) {
                log_elems.push_back(text(log_lines[i]) | color(Color::White));
            }
            log_elems.push_back(separator());
            log_elems.push_back(text("Up/Down: Scroll log") | color(Color::White));
            Element log_block = vbox(log_elems) | border | bgcolor(Color::Blue);

            return vbox({
                text("Refrigeration Service Dashboard") | bold | color(Color::White) | bgcolor(Color::Blue) | hcenter,
                separator(),
                status_block,
                separator(),
                log_block
            }) | bgcolor(Color::Blue);
        } else {
            return vbox({
                help_panel->Render(),
                status_bar->Render(),
                separatorEmpty(),
                hbox({
                    config_table->Render(),
                    separatorEmpty(),
                    sensor_panel->Render()
                }),
                separatorEmpty()
            }) | bgcolor(Color::Black);
        }
    }), [&](Event event) {
        std::vector<std::string> keys;
        for (const auto& [key, _] : schema) keys.push_back(key);
        static bool editing_line = false;

        // Always handle service dashboard events first
        if (show_service_dashboard) {
            // Service dashboard event handling
            if (event == Event::Character('q') || event == Event::Character('Q') || event == Event::Escape) {
                show_service_dashboard = false;
                dashboard_message.clear();
                // Stop log polling thread
                dashboard_log_polling = false;
                if (dashboard_log_thread.joinable()) dashboard_log_thread.join();
                // Swallow the event so it does not propagate to the main dashboard
                return true;
            }
            if (event == Event::ArrowDown) {
                if (log_scroll + 1 < (int)log_lines.size()) log_scroll++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (log_scroll > 0) log_scroll--;
                return true;
            }
            auto refresh_logs = [&]() {
                log_lines = ReadEventsLog();
                int log_height = 15;
                int total_lines = log_lines.size();
                int max_scroll = std::max(0, total_lines - log_height);
                if (log_scroll == 0 || log_scroll >= max_scroll - 1) {
                    log_scroll = 0;
                }
            };
            if (event == Event::Character('s') || event == Event::Character('S')) {
                dashboard_message = RunCommandAndGetOutput("sudo systemctl start refrigeration.service 2>&1");
                service_status = RunCommandAndGetOutput("systemctl is-active refrigeration.service 2>&1");
                refresh_logs();
                return true;
            }
            if (event == Event::Character('t') || event == Event::Character('T')) {
                dashboard_message = RunCommandAndGetOutput("sudo systemctl stop refrigeration.service 2>&1");
                service_status = RunCommandAndGetOutput("systemctl is-active refrigeration.service 2>&1");
                refresh_logs();
                return true;
            }
            if (event == Event::Character('r') || event == Event::Character('R')) {
                dashboard_message = RunCommandAndGetOutput("sudo systemctl restart refrigeration.service 2>&1");
                service_status = RunCommandAndGetOutput("systemctl is-active refrigeration.service 2>&1");
                refresh_logs();
                return true;
            }
            return false;
        }
        // Main dashboard and edit mode logic follows
        if (mode == Mode::Edit) {
            // Block service dashboard in edit mode
            if (event == Event::Character('m')) {
                status_message = "Service dashboard only available in VIEW MODE.";
                return true;
            }
            // Handle confirmation prompt for reverting to default
            if (confirm_revert) {
                if (event == Event::Character('y') || event == Event::Character('Y')) {
                    const auto& key = keys[selected];
                    config.set(key, schema.at(key).defaultValue);
                    config.save();
                    status_message = "Reverted to default value.";
                    confirm_revert = false;
                    return true;
                } else if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
                    status_message = "Revert cancelled.";
                    confirm_revert = false;
                    return true;
                }
                return false;  // Only accept y/n/Esc while confirming
            }
            if (editing_line) {
                if (event == Event::Return) {
                    // Save edit
                    const auto& key = keys[selected];
                    bool changed = false;
                    if (edit_value == "d") {
                        changed = config.set(key, schema.at(key).defaultValue);
                        if (changed) config.save();
                        status_message = "Reset to default value.";
                    } else if (!edit_value.empty()) {
                        if (config.set(key, edit_value)) {
                            config.save();
                            status_message = "Value updated successfully.";
                        } else {
                            status_message = "Invalid value for this configuration item.";
                        }
                    }
                    edit_value.clear();
                    editing_line = false;
                    sensor_suggestion_selected = 0;
                    return true;
                } else if (event == Event::Escape) {
                    edit_value.clear();
                    editing_line = false;
                    status_message = "Edit cancelled.";
                    sensor_suggestion_selected = 0;
                    return true;
                } else if (event == Event::ArrowDown) {
                    // Check if we're editing a sensor.* config
                    const auto& key = keys[selected];
                    if (key.find("sensor.") == 0) {
                        // Extract sensor IDs from the sensor panel
                        std::vector<std::string> sensor_ids;
                        {
                            std::lock_guard<std::mutex> lock(sensorMutex);
                            for (const auto& line : latestSensorLines) {
                                if (line.find("28-") != std::string::npos) {
                                    size_t start = line.find("28-");
                                    size_t end = start;
                                    while (end < line.length() && (std::isalnum(line[end]) || line[end] == '-')) {
                                        end++;
                                    }
                                    sensor_ids.push_back(line.substr(start, end - start));
                                }
                            }
                        }
                        if (!sensor_ids.empty() && sensor_suggestion_selected < (int)sensor_ids.size() - 1) {
                            sensor_suggestion_selected++;
                            edit_value = sensor_ids[sensor_suggestion_selected];
                        }
                    }
                    return true;
                } else if (event == Event::ArrowUp) {
                    // Check if we're editing a sensor.* config
                    const auto& key = keys[selected];
                    if (key.find("sensor.") == 0) {
                        std::vector<std::string> sensor_ids;
                        {
                            std::lock_guard<std::mutex> lock(sensorMutex);
                            for (const auto& line : latestSensorLines) {
                                if (line.find("28-") != std::string::npos) {
                                    size_t start = line.find("28-");
                                    size_t end = start;
                                    while (end < line.length() && (std::isalnum(line[end]) || line[end] == '-')) {
                                        end++;
                                    }
                                    sensor_ids.push_back(line.substr(start, end - start));
                                }
                            }
                        }
                        if (!sensor_ids.empty() && sensor_suggestion_selected > 0) {
                            sensor_suggestion_selected--;
                            edit_value = sensor_ids[sensor_suggestion_selected];
                        }
                    }
                    return true;
                } else if (event == Event::Tab) {
                    // Auto-complete with selected sensor ID
                    const auto& key = keys[selected];
                    if (key.find("sensor.") == 0) {
                        std::vector<std::string> sensor_ids;
                        {
                            std::lock_guard<std::mutex> lock(sensorMutex);
                            for (const auto& line : latestSensorLines) {
                                if (line.find("28-") != std::string::npos) {
                                    size_t start = line.find("28-");
                                    size_t end = start;
                                    while (end < line.length() && (std::isalnum(line[end]) || line[end] == '-')) {
                                        end++;
                                    }
                                    sensor_ids.push_back(line.substr(start, end - start));
                                }
                            }
                        }
                        if (!sensor_ids.empty() && sensor_suggestion_selected < (int)sensor_ids.size()) {
                            edit_value = sensor_ids[sensor_suggestion_selected];
                            sensor_suggestion_selected = 0;
                        }
                    }
                    return true;
                } else if (event.is_character()) {
                    edit_value += event.character();
                    sensor_suggestion_selected = 0;  // Reset when typing
                    return true;
                } else if (event == Event::Backspace && !edit_value.empty()) {
                    edit_value.pop_back();
                    sensor_suggestion_selected = 0;
                    return true;
                }
                return false;
            }
            // In edit mode, 'e' edits the selected line
            if (event == Event::Character('e')) {
                if (!keys.empty()) {
                    editing_line = true;
                    edit_value = config.get(keys[selected]);
                    const auto& key = keys[selected];
                    if (key.find("sensor.") == 0) {
                        status_message = "Editing sensor ID. Use ↑/↓ to browse sensors, Tab to select, Enter to save.";
                    } else {
                        status_message = "Editing value. Press Enter to save, Esc to cancel.";
                    }
                    sensor_suggestion_selected = 0;
                }
                return true;
            }
            // In edit mode, 'd' resets to default
            if (event == Event::Character('d')) {
                if (!keys.empty()) {
                    confirm_revert = true;
                    status_message = "Revert to default? (y/n)";
                }
                return true;
            }
            // In edit mode, allow navigation
            if (event == Event::ArrowDown) {
                int sz = schema.size();
                if (selected + 1 < sz) selected++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (selected > 0) selected--;
                return true;
            }
            // Capital E toggles back to view mode
            if (event == Event::Character('E') || event == Event::Escape || event == Event::Character('q')) {
                mode = Mode::View;
                editing_line = false;
                edit_value.clear();
                status_message = "Returned to VIEW MODE.";
                return true;
            }
            return false;
        } else {
            // VIEW MODE: allow entering edit mode with capital E
            if (event == Event::Character('E')) {
                status_message = "Stopping refrigeration service... Please wait.";
                // Spawn thread to kill process while UI refreshes
                std::thread kill_thread([&]() {
                    KillRefrigerationProcess();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    mode = Mode::Edit;
                    status_message = "Refrigeration service stopped. Entered EDIT MODE. Press E/q/Esc to exit.";
                });
                kill_thread.detach();
                return true;
            }
            // VIEW MODE: allow quitting with 'q'
            if (event == Event::Character('q')) {
                screen.Exit();
                return true;
            }
        }
        // View mode
        if (event == Event::Character('m')) {
            // Fetch status and logs
            service_status = RunCommandAndGetOutput("systemctl is-active refrigeration.service 2>&1");
            auto fetch_logs = [&]() {
                log_lines = ReadEventsLog();
            };
            fetch_logs();
            // Start background polling thread for logs
            dashboard_log_polling = true;
            dashboard_log_thread = std::thread([&]() {
                while (dashboard_log_polling) {
                    fetch_logs();
                    // Force UI refresh for live effect
                    screen.PostEvent(Event::Custom);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            });
            // Always scroll to bottom when opening dashboard
            log_scroll = 0;
            dashboard_message.clear();
            show_service_dashboard = true;
            return true;
        }
        return false;
    });

    screen.Loop(main_container);
    pollingActive = false;
    if (polling_thread.joinable()) polling_thread.join();
    // Ensure dashboard log thread is stopped if program exits from dashboard
    dashboard_log_polling = false;
    if (dashboard_log_thread.joinable()) dashboard_log_thread.join();
    return 0;
}
