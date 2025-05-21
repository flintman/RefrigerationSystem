#ifndef ADS1115_H
#define ADS1115_H

#include <cstdint>
#include <string>

class ADS1115 {
public:
    ADS1115(uint8_t i2c_addr = 0x48, const std::string& i2c_bus = "/dev/i2c-1");
    ~ADS1115();

    float readVoltage(uint8_t channel); // channel = 0–3

private:
    int i2c_fd;
    uint8_t address;

    void writeRegister(uint8_t reg, uint16_t value);
    uint16_t readRegister(uint8_t reg);
    void selectDevice();

    static constexpr float gainMultiplier = 4.096f / 32768.0f; // ±4.096V range
};

#endif
