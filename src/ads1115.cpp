#include "ads1115.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdexcept>
#include <iostream>

ADS1115::ADS1115(uint8_t i2c_addr, const std::string& i2c_bus)
    : address(i2c_addr)
{
    i2c_fd = open(i2c_bus.c_str(), O_RDWR);
    if (i2c_fd < 0) throw std::runtime_error("Failed to open I2C bus");

    if (ioctl(i2c_fd, I2C_SLAVE, address) < 0)
        throw std::runtime_error("Failed to connect to ADS1115");
}

ADS1115::~ADS1115() {
    if (i2c_fd >= 0) close(i2c_fd);
}

void ADS1115::writeRegister(uint8_t reg, uint16_t value) {
    uint8_t buffer[3] = { reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF) };
    if (write(i2c_fd, buffer, 3) != 3)
        throw std::runtime_error("Failed to write to ADS1115");
}

uint16_t ADS1115::readRegister(uint8_t reg) {
    if (write(i2c_fd, &reg, 1) != 1)
        throw std::runtime_error("Failed to select ADS1115 register");

    uint8_t buffer[2];
    if (read(i2c_fd, buffer, 2) != 2)
        throw std::runtime_error("Failed to read from ADS1115");

    return (buffer[0] << 8) | buffer[1];
}

float ADS1115::readVoltage(uint8_t channel) {
    if (channel > 3) throw std::invalid_argument("Channel must be 0-3");

    const float gainMultiplier = 0.000125f; // ±4.096V range (4.096/32768)

    // Config register settings
    uint16_t config = 0x8000; // Start single conversion
    config |= (0x4000 | (channel << 12)); // MUX: AINx vs GND
    config |= 0x0200; // +/-4.096V gain
    config |= 0x0100; // Single-shot mode
    config |= 0x0080; // 128SPS
    config |= 0x0003; // Disable comparator

    writeRegister(0x01, config);

    // Wait for conversion to complete
    uint16_t status;
    int attempts = 0;
    do {
        usleep(100);
        status = readRegister(0x01);
        if (++attempts > 100) throw std::runtime_error("Conversion timeout");
    } while (!(status & 0x8000));

    uint16_t result = readRegister(0x00);
    int16_t signedResult = static_cast<int16_t>(result);

    return signedResult * gainMultiplier;
}
