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

#include "sensor_manager.h"
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <string>
#include <vector>
#include <cmath>

SensorManager::SensorManager() {

}

float SensorManager::celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

std::vector<std::string> SensorManager::readOneWireTempSensors() {
    std::vector<std::string> results;
    const std::string baseDir = "/sys/bus/w1/devices/";
    const std::string sensorPrefix = "28-"; // Common prefix for DS18B20 sensors

    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(baseDir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            std::string name = ent->d_name;
            if (name.find(sensorPrefix) == 0) {
                std::string sensorPath = baseDir + name + "/w1_slave";
                std::ifstream sensorFile(sensorPath);
                if (sensorFile.is_open()) {
                    std::string line;
                    std::getline(sensorFile, line); // First line - check CRC
                    if (line.find("YES") != std::string::npos) {
                        std::getline(sensorFile, line); // Second line - temperature
                        size_t pos = line.find("t=");
                        if (pos != std::string::npos) {
                            std::string tempStr = line.substr(pos + 2);
                            float tempC = std::stof(tempStr) / 1000.0f;
                            float tempF = celsiusToFahrenheit(tempC);
                            char buf[128];
                            snprintf(buf, sizeof(buf), "Sensor: %s - Temperature: %.1f°F", name.c_str(), tempF);
                            results.emplace_back(buf);
                        }
                    }
                    sensorFile.close();
                }
            }
        }
        closedir(dir);
    } else {
        results.emplace_back("Could not open directory: " + baseDir);
    }
    return results;
}

float SensorManager::readSensor(const std::string& sensor_id) {
    const std::string sensor_path = "/sys/bus/w1/devices/" + sensor_id + "/w1_slave";

    std::ifstream file(sensor_path);
    if (!file) {
        std::cerr <<"Failed to open sensor: " << sensor_id;
        return -327.0f; // Return an invalid temperature value
    }

    std::string line;

    // First line - CRC check
    std::getline(file, line);
    if (line.find("YES") == std::string::npos) {
        std::cerr << "Invalid CRC for sensor: " << sensor_id << std::endl;
        return -327.0f; // Return an invalid temperature value
    }

    // Second line - temperature value
    std::getline(file, line);
    size_t pos = line.find("t=");
    if (pos == std::string::npos) {
        std::cerr << "Temperature data not found for sensor: " << sensor_id << std::endl;
        return -327.0f; // Return an invalid temperature value
    }

    // Extract and convert temperature value
    float temp = std::stof(line.substr(pos + 2)) / 1000.0f;
    float tempF = celsiusToFahrenheit(temp);
    tempF = std::round(tempF * 10.0f) / 10.0f;
    return tempF;
}