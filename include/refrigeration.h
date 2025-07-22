#ifndef REFRIGERATION_H
#define REFRIGERATION_H

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <ctime>
#include <memory>
#include "lcd_manager.h"
#include "gpio_manager.h"
#include "config_manager.h"
#include "sensor_manager.h"
#include "log_manager.h"
#include "ads1115.h"
#include "WS2811Controller.h"
#include "wifi_manager.h"
#include "alarm.h"
#include "demo_refrigeration.h"
#include "secure_client.h"

// Version and config
inline const std::string version = "1.0.5"; //Make sure you update the version in Makefile.
inline const std::string config_file_name = "/etc/refrigeration/config.env";

// Global state and synchronization
inline std::atomic<bool> running{true};
inline std::mutex status_mutex;


// Managers and hardware
inline ConfigManager cfg(config_file_name);
inline std::string ip_address = cfg.get("client.ip_address");
inline GpioManager gpio;
inline ADS1115 adc;
inline WS2811Controller ws2811(2, 18);
inline auto mux = std::make_shared<TCA9548A_SMBus>();
inline LCD2004_SMBus display1(mux, 1);
inline LCD2004_SMBus display2(mux, 2);
inline SensorManager sensors;
inline Logger logger(stoi(cfg.get("debug.code")));
inline WiFiManager wifi_manager;
inline Alarm systemAlarm;
inline DemoRefrigeration demo;
inline SecureClient secure_client(ip_address);

// Alarm state
inline std::atomic<bool> isShutdownAlarm(false);
inline std::atomic<bool> isWarningAlarm(false);

// Refrigeration state
inline std::atomic<bool> demo_mode(false);
inline std::atomic<bool> trigger_defrost{false};
inline std::atomic<bool> pretrip_enable(false);
inline std::atomic<bool> anti_timer{false};
inline std::atomic<time_t> defrost_start_time = 0;
inline std::atomic<time_t> defrost_button_press_start_time{0};
inline std::atomic<time_t> defrost_last_time{time(nullptr)};
inline std::atomic<time_t> compressor_last_stop_time{time(nullptr) - 400};
inline std::atomic<time_t> alarm_reset_button_press_start_time{0};
inline std::atomic<time_t> state_timer{0};
inline std::atomic<time_t> pretrip_stage_start = 0;
inline std::atomic<int> pretrip_stage{0};
inline std::atomic<time_t> compressor_on_start_time{0};
inline std::atomic<long> compressor_on_total_seconds{cfg.get("unit.compressor_run_seconds") == "0" ? 0 : std::stol(cfg.get("unit.compressor_run_seconds"))};
inline std::string last_compressor_status = "False";

// Status map
inline std::map<std::string, std::string> status = {
    {"status", "Null"},
    {"compressor", "False"},
    {"fan", "False"},
    {"valve", "False"},
    {"electric_heater", "False"}
};

// Sensor data
inline std::atomic<float>  return_temp = -327.0f;
inline std::atomic<float>  supply_temp = -327.0f;
inline std::atomic<float> coil_temp = -327.0f;
inline std::atomic<float> setpoint{55.0f};

// Logging config
inline int log_retention_period = stoi(cfg.get("logging.retention_period"));
inline int log_interval = stoi(cfg.get("logging.interval_sec"));
extern time_t last_log_timestamp;

// Function declarations
bool secure_client_send();
void secureclient_loop();
void refrigeration_system(float return_temp_, float supply_temp_, float coil_temp_, float setpoint_);
void display_system_thread();
void setpoint_system_thread();
void cleanup_all();
void update_gpio_from_status();
void null_mode();
void cooling_mode();
void heating_mode();
void defrost_mode();
void display_all_variables();
void ws8211_system_thread();
void pretrip_mode();
void hotspot_start();
void signalHandler(int signal);
void interruptible_sleep(int total_seconds);
void update_compressor_on_time(const std::string& new_status);

#endif // REFRIGERATION_H