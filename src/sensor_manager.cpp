#include "sensor_manager.h"
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <string>
#include <vector>

SensorManager::SensorManager() {

}

float SensorManager::celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

void SensorManager::readOneWireTempSensors() {
    const std::string baseDir = "/sys/bus/w1/devices/";
    const std::string sensorPrefix = "28-"; // Common prefix for DS18B20 sensors
    
    // Open the directory
    DIR *dir;
    struct dirent *ent;
    
    if ((dir = opendir(baseDir.c_str())) != nullptr) {
        // Read all files in directory
        while ((ent = readdir(dir)) != nullptr) {
            std::string name = ent->d_name;
            
            // Check if it's a temperature sensor (starts with 28-)
            if (name.find(sensorPrefix) == 0) {
                std::string sensorPath = baseDir + name + "/w1_slave";
                
                // Open the sensor file
                std::ifstream sensorFile(sensorPath);
                if (sensorFile.is_open()) {
                    std::string line;
                    std::getline(sensorFile, line); // First line - check CRC
                    
                    // Check if CRC is valid
                    if (line.find("YES") != std::string::npos) {
                        std::getline(sensorFile, line); // Second line - temperature
                        size_t pos = line.find("t=");
                        if (pos != std::string::npos) {
                            std::string tempStr = line.substr(pos + 2);
                            float tempC = std::stof(tempStr) / 1000.0f;
                            float tempF = celsiusToFahrenheit(tempC);
                            // Display sensor name and temperature
                            std::cout << "Sensor: " << name 
                                      << " - Temperature: " << tempF 
                                      << "°F" << std::endl;
                        }
                    }
                    sensorFile.close();
                }
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Could not open directory: " << baseDir << std::endl;
    }
}

float SensorManager::readSensor(const std::string& sensor_id) {
    const std::string sensor_path = "/sys/bus/w1/devices/" + sensor_id + "/w1_slave";
    
    std::ifstream file(sensor_path);
    if (!file) {
        throw std::runtime_error("Failed to open sensor: " + sensor_id);
    }

    std::string line;
    
    // First line - CRC check
    std::getline(file, line);
    if (line.find("YES") == std::string::npos) {
        throw std::runtime_error("Invalid CRC for sensor: " + sensor_id);
    }

    // Second line - temperature value
    std::getline(file, line);
    size_t pos = line.find("t=");
    if (pos == std::string::npos) {
        throw std::runtime_error("Temperature data not found for sensor: " + sensor_id);
    }

    // Extract and convert temperature value
    float temp = std::stof(line.substr(pos + 2)) / 1000.0f;
    float tempF = celsiusToFahrenheit(temp);
    return tempF;
}