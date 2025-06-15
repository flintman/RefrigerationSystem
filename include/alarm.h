#ifndef ALARM_H
#define ALARM_H

#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

class Alarm {
public:
    Alarm();

    void clearTimers();
    void coolingAlarm(float returnTemp = -327, float supplyTemp = -327, float offsetTemp = 4);
    void heatingAlarm(float returnTemp = -327, float supplyTemp = -327, float offsetTemp = 4);

    bool alarmAnyStatus() const;
    bool getShutdownStatus() const;
    bool getWarningStatus() const;
    std::vector<int> getAlarmCodes() const;
    void resetAlarm();

    void activateAlarm(int alarmType = 1, const std::string& message = "");
    void addAlarmCode(int code);

private:
    bool isShutdownAlarm;
    bool isWarningAlarm;
    std::vector<int> alarmCodes;

    std::chrono::steady_clock::time_point coolingAlarmStartTime;
    std::chrono::steady_clock::time_point heatingAlarmStartTime;
    bool coolingTimerActive;
    bool heatingTimerActive;
};

#endif // ALARM_H
