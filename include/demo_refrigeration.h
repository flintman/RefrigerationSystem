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
    void setRefreshInterval(double seconds);
    void enableAutoRefreshRamp(double from, double to, double rate);
    void update();

private:
    void simulateCooling();
    void simulateHeating();
    void simulateDefrost();
    void simulateNull();
    float approachTarget(float current, float target, float rate);

    std::string current_status;
    float setpoint;
    float return_temp;
    float supply_temp;
    float coil_temp;

    std::default_random_engine rng;
    std::normal_distribution<float> noise;

    std::mutex mtx;
    std::chrono::steady_clock::time_point last_update;
    double refresh_interval_sec = 10.0;

    // Auto refresh ramping
    bool auto_refresh_enabled = false;
    double initial_refresh = 40.0;
    double target_refresh = 10.0;
    double decay_rate = 0.98;
};

#endif // DEMO_REFRIGERATION_H