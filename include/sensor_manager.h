#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <iostream>
#include <vector>
#include <fstream>
#include <string>


using namespace std;

// Define your sensor structure to hold the data
struct SensorData {
    string name;
    double value;
};

class SensorManager {
public:
    SensorManager();

    std::vector<std::string> readOneWireTempSensors();
    float celsiusToFahrenheit(float celsius);
    float readSensor(const std::string& sensor_id);

private:

};

#endif // SENSOR_MANAGER_H