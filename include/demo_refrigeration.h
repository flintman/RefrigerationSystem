#ifndef DEMO_REFRIGERATION_H
#define DEMO_REFRIGERATION_H
#include <string>
#include <random>
#include <mutex>
#include <chrono>

class DemoRefrigeration {
public:
    DemoRefrigeration();
    void setStatus(const std::string& status);
    void setSetpoint(float sp);
    float readReturnTemp();
    float readSupplyTemp();
    float readCoilTemp();
    void update(); // Call periodically to advance simulation
    void setRefreshInterval(double seconds); // New: set refresh interval

private:
    std::string current_status;
    float setpoint;
    float return_temp;
    float supply_temp;
    float coil_temp;
    std::mutex mtx;
    std::default_random_engine rng;
    std::normal_distribution<float> noise;

    double refresh_interval_sec = 10.0; // New: default 10 seconds
    std::chrono::steady_clock::time_point last_update; // New

    void simulateCooling();
    void simulateHeating();
    void simulateNull();
    void simulateDefrost();
};
#endif // DEMO_REFRIGERATION_H