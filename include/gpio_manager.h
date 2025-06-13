#ifndef GPIOMANAGER_H
#define GPIOMANAGER_H

#include <string>
#include <unordered_map>
#include <cstdint>
#include <chrono>
#include <mutex>

class GpioManager {
public:
    GpioManager();
    ~GpioManager();

    void write(const std::string& name, bool value);
    bool read(const std::string& name, int debounce_ms = 30);

private:
    int mem_fd;
    volatile uint32_t* gpioMap;
    std::unordered_map<std::string, int> outputPins;
    std::unordered_map<std::string, int> inputPins;

    // Debounce state for each input pin
    struct DebounceState {
        bool lastStableState = false;
        bool lastReadState = false;
        std::chrono::steady_clock::time_point lastChangeTime = std::chrono::steady_clock::now();
    };
    std::unordered_map<std::string, DebounceState> debounceStates;
    std::mutex debounceMutex;

    void setOutput(int pin);
    void setInput(int pin);
    void mapGPIO();
    void unmapGPIO();
};

#endif // GPIOMANAGER_H