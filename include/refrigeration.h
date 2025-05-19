#ifndef REGFRIGERATION_H
#define REGFRIGERATION_H

#include <string>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include "lcd_manager.h"

// Define variables
std::string config_file_name = "config.env";
float return_temp = -327.0;
float supply_temp = -327.0;
float coil_temp = -327.0;
float setpoint = 55.0;
std::string system_status = "Null";

ConfigManager cfg(config_file_name);
SensorManager sensors;
std::mutex mtx;

void refrigeration_system();
void display_system();

// Initialize multiplexer
auto mux = std::make_shared<TCA9548A_SMBus>();

// Create LCD instances
LCD2004_SMBus display1(mux, 1); // Channel 1
LCD2004_SMBus display2(mux, 2); // Channel 2

#endif