#ifndef GPIOMANAGER_H
#define GPIOMANAGER_H

#include <string>
#include <unordered_map>
#include <cstdint>

class GpioManager {
public:
    GpioManager();
    ~GpioManager();

    void write(const std::string& name, bool value);
    bool read(const std::string& name);

private:
    int mem_fd;
    volatile uint32_t* gpioMap;
    std::unordered_map<std::string, int> outputPins;
    std::unordered_map<std::string, int> inputPins;

    void setOutput(int pin);
    void setInput(int pin);
    void mapGPIO();
    void unmapGPIO();
};

#endif // GPIOMANAGER_H
