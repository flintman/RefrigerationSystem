#include "../include/tools/web_interface/unit_poller.h"
#include "../include/tools/web_interface/email_notifier.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>

using json = nlohmann::json;

// CURL callback for writing response data
static size_t unit_poller_curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

UnitPoller::UnitPoller() : running_(false), email_notifier_(nullptr) {
}

UnitPoller::~UnitPoller() {
    stop();
}

void UnitPoller::start(const std::vector<Unit>& units) {
    if (running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        units_ = units;
    }

    running_ = true;
    polling_thread_ = std::thread(&UnitPoller::polling_loop, this);
    write_log("UnitPoller: Started polling thread with " + std::to_string(units.size()) + " units");
}

void UnitPoller::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
    write_log("UnitPoller: Stopped polling thread");
}

json UnitPoller::get_unit_data(const std::string& unit_id) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = unit_data_.find(unit_id);
    if (it != unit_data_.end()) {
        return it->second;
    }
    return json::object();
}

json UnitPoller::get_all_unit_data() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    json all_data = json::object();
    for (const auto& entry : unit_data_) {
        all_data[entry.first] = entry.second;
    }
    return all_data;
}

json UnitPoller::get_active_alarms(const std::string& unit_id) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = active_alarms_.find(unit_id);
    if (it != active_alarms_.end()) {
        json alarms = json::array();
        for (int alarm_code : it->second) {
            alarms.push_back(alarm_code);
        }
        return alarms;
    }
    return json::array();
}

json UnitPoller::call_unit_api(const Unit& unit, const std::string& endpoint) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        write_log("UnitPoller: Failed to initialize CURL for unit " + unit.id);
        return json::object();
    }

    std::string url = "https://" + unit.api_address + ":" + std::to_string(unit.api_port) + "/api/v1" + endpoint;
    std::string response_string;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-API-Key: " + unit.api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, unit_poller_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Accept self-signed certs
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        write_log("UnitPoller: ERROR - Failed to call API for unit " + unit.id + ": " + std::string(curl_easy_strerror(res)));
        return json::object();
    }

    try {
        return json::parse(response_string);
    } catch (const std::exception& e) {
        write_log("UnitPoller: ERROR - Invalid JSON response from unit " + unit.id + ": " + std::string(e.what()));
        return json::object();
    }
}

void UnitPoller::polling_loop() {
    write_log("UnitPoller: Polling loop started");

    while (running_) {
        write_log("UnitPoller: === Polling cycle started ===");

        std::vector<Unit> current_units;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            current_units = units_;
        }

        for (const auto& unit : current_units) {
            if (!running_) break;

            write_log("UnitPoller: Polling unit: " + unit.id);

            // Fetch current status
            json status = call_unit_api(unit, "/status");

            write_log("UnitPoller: Unit " + unit.id + " response: " + status.dump().substr(0, 200));

            if (status.is_object() && status.contains("system_status")) {
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    unit_data_[unit.id] = status;
                    write_log("UnitPoller: Stored data for unit " + unit.id + " with status: " + std::string(status["system_status"]));
                }

                // Check for alarms using new API fields
                bool alarm_warning = status.value("alarm_warning", false);
                bool alarm_shutdown = status.value("alarm_shutdown", false);
                auto current_alarms = status.value("active_alarms", json::array());

                bool has_alarm = alarm_warning || alarm_shutdown || (current_alarms.is_array() && !current_alarms.empty());
                bool alarm_changed = false;

                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    auto it = active_alarms_.find(unit.id);
                    if (has_alarm) {
                        // Only update if alarm codes changed
                        std::vector<int> alarm_ints;
                        for (const auto& a : current_alarms) {
                            if (a.is_number()) {
                                alarm_ints.push_back(a.get<int>());
                            }
                        }
                        if (it == active_alarms_.end() || it->second != alarm_ints) {
                            alarm_changed = true;
                            active_alarms_[unit.id] = alarm_ints;
                        }
                    } else {
                        if (it != active_alarms_.end()) {
                            alarm_changed = true;
                            active_alarms_.erase(unit.id);
                        }
                    }
                }

                if (alarm_changed && has_alarm) {
                    write_log("UnitPoller: ALARM detected on unit: " + unit.id);
                    // Send email notification if notifier is configured
                    if (email_notifier_) {
                        email_notifier_->send_alarm_email(unit.id, status);
                    }
                }

                write_log("UnitPoller: Unit " + unit.id + " status: " + status.value("system_status", "Unknown"));
            } else {
                write_log("UnitPoller: WARNING - Failed to get status for unit " + unit.id);
            }

            // Small delay between unit calls
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        write_log("UnitPoller: === Polling cycle completed ===");

        // Poll every 30 seconds
        for (int i = 0; i < 30 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    write_log("UnitPoller: Polling loop stopped");
}

json UnitPoller::fetch_unit_status(const Unit& unit) {
    return call_unit_api(unit, "/status");
}

json UnitPoller::fetch_unit_logs(const Unit& unit) {
    return call_unit_api(unit, "/logs");
}

void UnitPoller::write_log(const std::string& message) {
    std::string timestamp;
    {
        time_t now = std::time(nullptr);
        struct tm* timeinfo = std::localtime(&now);
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        timestamp = buffer;
    }

    std::string log_message = "[" + timestamp + "] [UnitPoller] " + message;

    // Log to console
    std::cout << log_message << std::endl;

    // Log to file
    std::ofstream log_file("/var/log/refrigeration-api.log", std::ios::app);
    if (log_file.is_open()) {
        log_file << log_message << std::endl;
        log_file.close();
    }
}
