#include "alarm.h"
#include <iostream>

Alarm::Alarm() :
    isShutdownAlarm(false),
    isWarningAlarm(false),
    coolingTimerActive(false),
    heatingTimerActive(false)
{}

void Alarm::clearTimers() {
    coolingTimerActive = false;
    heatingTimerActive = false;
}

void Alarm::coolingAlarm(float returnTemp, float supplyTemp, float offsetTemp) {
    constexpr int duration = 30 * 60; // 30 minutes
    heatingTimerActive = false; // reset opposite

    if ((returnTemp - offsetTemp <= supplyTemp) && (returnTemp > 30)) {
        if (!coolingTimerActive) {
            coolingAlarmStartTime = std::chrono::steady_clock::now();
            coolingTimerActive = true;
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - coolingAlarmStartTime
            ).count();
            if (elapsed >= duration) {
                activateAlarm(1, "1001: Unit not cooling.");
                addAlarmCode(1001);
            }
        }
    } else {
        coolingTimerActive = false;
    }
}

void Alarm::heatingAlarm(float returnTemp, float supplyTemp, float offsetTemp) {
    constexpr int duration = 30 * 60; // 30 minutes
    coolingTimerActive = false; // reset opposite

    if ((returnTemp + offsetTemp >= supplyTemp) && (returnTemp < 60)) {
        if (!heatingTimerActive) {
            heatingAlarmStartTime = std::chrono::steady_clock::now();
            heatingTimerActive = true;
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - heatingAlarmStartTime
            ).count();
            if (elapsed >= duration) {
                activateAlarm(1, "1002: Unit not heating.");
                addAlarmCode(1002);
            }
        }
    } else {
        heatingTimerActive = false;
    }
}

void Alarm::activateAlarm(int alarmType, const std::string& message) {
    std::cout << "ALARM TRIGGERED: " << message << std::endl;
    if (alarmType == 1) {
        isShutdownAlarm = true;
    } else {
        isWarningAlarm = true;
    }
}

void Alarm::addAlarmCode(int code) {
    if (std::find(alarmCodes.begin(), alarmCodes.end(), code) == alarmCodes.end()) {
        alarmCodes.push_back(code);
        std::cout << "Alarm code " << code << " added." << std::endl;
    }
}

bool Alarm::alarmAnyStatus() const {
    for (int code : alarmCodes) std::cout << code << " ";
    std::cout << std::endl;
    return isShutdownAlarm || isWarningAlarm;
}

bool Alarm::getShutdownStatus() const {
    return isShutdownAlarm;
}

bool Alarm::getWarningStatus() const {
    return isWarningAlarm;
}

std::vector<int> Alarm::getAlarmCodes() const {
    return alarmCodes;
}

void Alarm::resetAlarm() {
    isShutdownAlarm = false;
    isWarningAlarm = false;
    clearTimers();
    alarmCodes.clear();
    std::cout << "All alarms reset." << std::endl;
}
