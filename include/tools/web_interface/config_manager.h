/*
 * Configuration Manager
 * Handles loading and monitoring configuration files
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <vector>
#include <ctime>
#include <mutex>
#include <thread>
#include <functional>

struct Unit {
    std::string id;
    std::string api_address;
    int api_port;
    std::string api_key;
};

class ConfigManager {
public:
    ConfigManager(const std::string& config_file = "web_interface_config.env");
    ~ConfigManager();

    // Configuration access
    std::string get_email_server() const { return email_server_; }
    std::string get_email_address() const { return email_address_; }
    std::string get_email_password() const { return email_password_; }
    std::string get_web_password() const { return web_password_; }
    int get_email_port() const { return email_port_; }
    int get_web_port() const { return web_port_; }
    const std::vector<Unit>& get_units() const { return units_; }

    // Configuration management
    void load_config();
    void start_watch_thread();
    void stop_watch_thread();
    void reload_if_changed();

    // Callbacks
    void set_on_config_changed(std::function<void()> callback) {
        on_config_changed_ = callback;
    }

private:
    std::string config_file_;
    std::string email_server_;
    std::string email_address_;
    std::string email_password_;
    std::string web_password_;
    int email_port_;
    int web_port_;

    std::vector<Unit> units_;
    time_t config_file_mtime_;
    std::thread config_watch_thread_;
    bool watching_;
    std::mutex config_mutex_;
    std::function<void()> on_config_changed_;

    void load_units_from_config();
    void config_watch_loop();
    void write_log(const std::string& message);
};

#endif // CONFIG_MANAGER_H
