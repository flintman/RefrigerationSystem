#ifndef REGFRIGERATION_H
#define REGFRIGERATION_H

#include <string>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>

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

#endif