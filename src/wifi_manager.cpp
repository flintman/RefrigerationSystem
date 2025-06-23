#include "wifi_manager.h"
#include "refrigeration.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <array>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

WiFiManager::WiFiManager(const std::string& ssid, const std::string& password)
    : ssid(ssid), password(password),
      hotspot_interface("wlan0_ap"), client_interface("wlan0") {}

bool WiFiManager::run_command(const std::string& cmd) {
    return system(cmd.c_str()) == 0;
}

std::string WiFiManager::exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        result += buffer.data();
    pclose(pipe);
    return result;
}

bool WiFiManager::is_hotspot_active() {
    std::string cmd = "iwconfig " + hotspot_interface + " 2>/dev/null";
    return system(cmd.c_str()) == 0;
}

bool WiFiManager::is_interface_exist(const std::string& iface) {
    std::string cmd = "ip link show " + iface + " > /dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}

bool WiFiManager::start_hotspot() {
    // Always delete any existing hotspot connection before starting
    run_command("nmcli connection delete MyHotspot");

    // Remove the virtual interface if it exists (cleanup)
    if (is_interface_exist(hotspot_interface)) {
        run_command("sudo iw dev " + hotspot_interface + " del");
        sleep(1);
    }

    // Create the virtual interface
    if (!run_command("sudo iw dev " + client_interface + " interface add " + hotspot_interface + " type __ap")) {
        logger.log_events("Error",  "Failed to create virtual interface");
        return false;
    }
    sleep(2);

    // Add the hotspot connection
    std::stringstream ss;
    ss << "nmcli con add type wifi ifname " << hotspot_interface
       << " con-name MyHotspot autoconnect no ssid " << ssid
       << " 802-11-wireless.mode ap ipv4.method shared";
    if (!run_command(ss.str())) {
        logger.log_events("Error", "Failed to add hotspot connection");
        run_command("sudo iw dev " + hotspot_interface + " del");
        return false;
    }

    ss.str(""); ss.clear();
    ss << "nmcli con modify MyHotspot "
       << "802-11-wireless-security.key-mgmt wpa-psk "
       << "802-11-wireless-security.psk " << password;
    if (!run_command(ss.str())) {
        logger.log_events("Error", "Failed to modify hotspot security");
        run_command("sudo iw dev " + hotspot_interface + " del");
        run_command("nmcli connection delete MyHotspot");
        return false;
    }

    ss.str(""); ss.clear();
    ss << "nmcli con up MyHotspot ifname " << hotspot_interface;
    if (!run_command(ss.str())) {
        std::cerr << "Failed to bring up hotspot\n";
        run_command("sudo iw dev " + hotspot_interface + " del");
        run_command("nmcli connection delete MyHotspot");
        return false;
    }

    logger.log_events("Debug", "Hotspot started successfully");
    return true;
}

bool WiFiManager::stop_hotspot() {
    // Try to bring down the connection if it exists
    run_command("nmcli con down MyHotspot");

    // Delete the connection if it exists
    run_command("nmcli connection delete MyHotspot");

    // Remove the virtual interface if it exists
    if (is_interface_exist(hotspot_interface))
        run_command("sudo iw dev " + hotspot_interface + " del");

    logger.log_events("Debug", "Hotspot stopped successfully");
    return true;
}

std::vector<std::string> WiFiManager::check_hotspot_clients() {
    std::vector<std::string> clients;
    std::string output = exec_command("iw dev " + hotspot_interface + " station dump");
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("Station") != std::string::npos) {
            std::istringstream lss(line);
            std::string tmp, mac;
            lss >> tmp >> mac;
            clients.push_back(mac);
        }
    }
    return clients;
}

std::string WiFiManager::get_ip_address(const std::string& iface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) return "xxx.xxx.xxx.xxx";

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        close(fd);
        return "xxx.xxx.xxx.xxx";
    }
    close(fd);

    struct sockaddr_in* ipaddr = (struct sockaddr_in*)&ifr.ifr_addr;
    return inet_ntoa(ipaddr->sin_addr);
}

bool WiFiManager::is_connected(const std::string& host, int port, int timeout) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    struct timeval tv{};
    tv.tv_sec = timeout;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool result = connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    close(sock);
    return result;
}

void WiFiManager::set_credentials(const std::string& new_ssid, const std::string& new_password) {
    ssid = new_ssid;
    password = new_password;
    logger.log_events("Debug", "Hotspot credntials updated" + std::string("SSID: ") + ssid + ", Password: " + password);
}
