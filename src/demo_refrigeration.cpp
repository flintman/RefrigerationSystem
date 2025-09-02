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

#include "demo_refrigeration.h"
#include <algorithm>
#include <cmath>

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
    auto_refresh_enabled = false;
}

void DemoRefrigeration::enableAutoRefreshRamp(double from, double to, double rate) {
    std::lock_guard<std::mutex> lock(mtx);
    initial_refresh = from;
    target_refresh = to;
    decay_rate = rate;
    refresh_interval_sec = from;
    auto_refresh_enabled = true;
}

float DemoRefrigeration::approachTarget(float current, float target, float rate) {
        return current + (target - current) * rate;
    }

void DemoRefrigeration::update() {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_update).count();
    if (elapsed < refresh_interval_sec) return;

    last_update = now;

    if (auto_refresh_enabled && refresh_interval_sec > target_refresh) {
        refresh_interval_sec = std::max(target_refresh, refresh_interval_sec * decay_rate);
    }

    if (current_status == "Cooling") simulateCooling();
    else if (current_status == "Heating") simulateHeating();
    else if (current_status == "Defrost") simulateDefrost();
    else simulateNull();
}

void DemoRefrigeration::simulateCooling() {
    float cool_rate = 0.05f;
    float target_supply = setpoint - 10.0f;
    float target_coil   = setpoint - 15.0f;
    float target_return = setpoint - 2.0f;

    supply_temp = std::max(target_supply, approachTarget(supply_temp, target_supply, cool_rate));
    coil_temp   = std::max(target_coil, approachTarget(coil_temp, target_coil, cool_rate * 1.2f));

    if (return_temp > supply_temp)
        return_temp = std::max(target_return, approachTarget(return_temp, target_return, cool_rate * 0.5f));
}

void DemoRefrigeration::simulateHeating() {
    float heat_rate = 0.05f;
    float target_supply = setpoint + 10.0f;
    float target_coil   = setpoint + 15.0f;
    float target_return = setpoint - 2.0f;

    supply_temp = std::min(target_supply, approachTarget(supply_temp, target_supply, heat_rate));
    coil_temp   = std::min(target_coil, approachTarget(coil_temp, target_coil, heat_rate * 1.2f));

    if (return_temp < supply_temp)
        return_temp = std::min(target_supply, approachTarget(return_temp, target_supply, heat_rate * 0.5f));
}

void DemoRefrigeration::simulateDefrost() {
    float coil_target = 50.0f;
    float air_target  = 55.0f;

    coil_temp   = std::min(coil_target, approachTarget(coil_temp, coil_target, 0.08f));
    supply_temp = std::min(air_target, approachTarget(supply_temp, coil_temp, 0.04f));
    return_temp = std::min(air_target, approachTarget(return_temp, supply_temp, 0.02f));
}

void DemoRefrigeration::simulateNull() {
    float ambient = 60.0f + std::sin(std::chrono::duration<double>(last_update.time_since_epoch()).count() / 360.0) * 2.0f;

    supply_temp = approachTarget(supply_temp, return_temp, 0.01f);
    coil_temp   = approachTarget(coil_temp, return_temp, 0.01f);

    if (std::abs(supply_temp - return_temp) < 0.5f && std::abs(coil_temp - return_temp) < 0.5f) {
        return_temp = approachTarget(return_temp, ambient, 0.005f);
        supply_temp = approachTarget(supply_temp, ambient, 0.005f);
        coil_temp   = approachTarget(coil_temp, ambient, 0.005f);
    }
}
