/*
 * Unit Poller
 * Collects data from refrigeration units via API
 */

#ifndef UNIT_POLLER_H
#define UNIT_POLLER_H

#include "config_manager.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class EmailNotifier;  // Forward declaration

class UnitPoller {
public:
    UnitPoller();
    ~UnitPoller();

    void start(const std::vector<Unit>& units);
    void stop();
    bool is_running() const { return running_; }
    void set_email_notifier(EmailNotifier* notifier) { email_notifier_ = notifier; }

    // Data access
    json get_unit_data(const std::string& unit_id) const;
    json get_all_unit_data() const;
    json get_active_alarms(const std::string& unit_id) const;

    // API calls
    json call_unit_api(const Unit& unit, const std::string& endpoint);

private:
    std::vector<Unit> units_;
    std::map<std::string, json> unit_data_;
    std::map<std::string, std::vector<int>> active_alarms_;
    std::map<std::string, std::string> last_status_;
    EmailNotifier* email_notifier_;

    std::thread polling_thread_;
    bool running_;
    mutable std::mutex data_mutex_;

    void polling_loop();
    json fetch_unit_status(const Unit& unit);
    json fetch_unit_logs(const Unit& unit);

    void write_log(const std::string& message);
};

#endif // UNIT_POLLER_H
