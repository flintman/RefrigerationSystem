#ifndef REGFRIGERATION_H
#define REGFRIGERATION_H

#include <string>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <ctime>
#include "lcd_manager.h"
#include "gpio_manager.h"
#include "config_manager.h"
#include "sensor_manager.h"
#include "log_manager.h"
#include "ads1115.h"


// Define variables
std::string version = "1.0.0";
std::string config_file_name = "config.env";

time_t last_log_timestamp = time(NULL) - 400;

// Refrigertion data
std::mutex  refrigeration_mutex;
bool trigger_defrost = false;
time_t defrost_start_time = 0;
time_t defrost_last_time = time(NULL);
time_t compressor_last_stop_time = time(NULL) - 400;
bool anti_timer = false;

// Sets up the array for logging
std::map<std::string, std::string> status = {
    {"status", "Null"},
    {"compressor", "False"},
    {"fan", "False"},
    {"valve", "False"},
    {"electric_heater", "False"}
};

std::atomic<bool> running(true);

ConfigManager cfg(config_file_name);
std::mutex status_mutex;
GpioManager gpio;
ADS1115 adc;

// Setup Sensor data
SensorManager sensors;
float return_temp = -327.0;
float supply_temp = -327.0;
float coil_temp = -327.0;
std::atomic<float> setpoint = 55.0;
std::mutex sensor_mutex;

// Setup Logging
int debug = stoi(cfg.get("debug.code"));
int log_retention_period = stoi(cfg.get("logging.retention_period"));
int log_interval = stoi(cfg.get("logging.interval_sec"));
Logger logger(debug);

// My Functions
void refrigeration_system(float return_temp_, float supply_temp_, float coil_temp_, float setpoint_);
void display_system();
void gpio_system();
void cleanup_all();
void update_gpio_from_status();

// Initialize multiplexer
auto mux = std::make_shared<TCA9548A_SMBus>();

// Create LCD instances
LCD2004_SMBus display1(mux, 1); // Channel 1
LCD2004_SMBus display2(mux, 2); // Channel 2

#endif