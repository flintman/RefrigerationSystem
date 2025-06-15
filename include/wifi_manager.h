#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <string>
#include <vector>

class WiFiManager {
public:
    WiFiManager(const std::string& ssid = "RPiZero_Hotspot", const std::string& password = "raspberry");

    bool is_hotspot_active();
    bool start_hotspot();
    bool stop_hotspot();
    bool is_interface_exist(const std::string& iface);
    std::vector<std::string> check_hotspot_clients();
    std::string get_ip_address(const std::string& iface = "wlan0");
    bool is_connected(const std::string& host = "8.8.8.8", int port = 53, int timeout = 3);
    void set_credentials(const std::string& new_ssid, const std::string& new_password);

private:
    std::string ssid;
    std::string password;
    std::string hotspot_interface;
    std::string client_interface;

    bool run_command(const std::string& cmd);
    std::string exec_command(const std::string& cmd);
};

#endif // WIFI_MANAGER_H
