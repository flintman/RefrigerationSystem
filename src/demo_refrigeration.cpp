#include "demo_refrigeration.h"
#include <algorithm>
#include <chrono>

DemoRefrigeration::DemoRefrigeration()
    : current_status("Null"),
      setpoint(40.0f),
      return_temp(60.0f),
      supply_temp(60.0f),
      coil_temp(60.0f),
      rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count())),
      noise(0.0f, 0.3f),
      last_update(std::chrono::steady_clock::now())
{}

void DemoRefrigeration::setStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(mtx);
    current_status = status;
}

void DemoRefrigeration::setSetpoint(float sp) {
    std::lock_guard<std::mutex> lock(mtx);
    setpoint = sp;
}

float DemoRefrigeration::readReturnTemp() {
    std::lock_guard<std::mutex> lock(mtx);
    return return_temp + noise(rng);
}

float DemoRefrigeration::readSupplyTemp() {
    std::lock_guard<std::mutex> lock(mtx);
    return supply_temp + noise(rng);
}

float DemoRefrigeration::readCoilTemp() {
    std::lock_guard<std::mutex> lock(mtx);
    return coil_temp + noise(rng);
}

void DemoRefrigeration::setRefreshInterval(double seconds) {
    std::lock_guard<std::mutex> lock(mtx);
    refresh_interval_sec = seconds;
}

void DemoRefrigeration::update() {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_update).count();
    if (elapsed < refresh_interval_sec) {
        return; // Not enough time has passed
    }
    last_update = now;

    if (current_status == "Cooling") {
        simulateCooling();
    } else if (current_status == "Heating") {
        simulateHeating();
    } else if (current_status == "Defrost") {
        simulateDefrost();
    } else {
        simulateNull();
    }
}

void DemoRefrigeration::simulateCooling() {
    return_temp = std::max(setpoint - 2.0f, return_temp - 0.20f);
    supply_temp = std::max(setpoint - 5.0f, supply_temp - 0.25f);
    coil_temp   = std::max(setpoint - 10.0f, coil_temp - 0.35f);
}

void DemoRefrigeration::simulateHeating() {
    return_temp = std::min(setpoint + 2.0f, return_temp + 0.15f);
    supply_temp = std::min(setpoint + 5.0f, supply_temp + 0.25f);
    coil_temp   = std::min(setpoint + 10.0f, coil_temp + 0.35f);
}

void DemoRefrigeration::simulateNull() {
    float ambient = 60.0f;
    return_temp += (ambient - return_temp) * 0.01f;
    supply_temp += (ambient - supply_temp) * 0.01f;
    coil_temp   += (ambient - coil_temp) * 0.01f;
}

void DemoRefrigeration::simulateDefrost() {
    coil_temp   = std::min(50.0f, coil_temp + 0.5f);
    return_temp = std::min(55.0f, return_temp + 0.1f);
    supply_temp = std::min(55.0f, supply_temp + 0.1f);
}