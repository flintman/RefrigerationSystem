#include "gpio_manager.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>

#define GPIO_MAP_SIZE   4096
#define GPIO_BASE       0x0

#define GPFSEL_OFFSET   0x00
#define GPSET_OFFSET    0x1C
#define GPCLR_OFFSET    0x28
#define GPLEV_OFFSET    0x34

GpioManager::GpioManager() {
    mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        throw std::runtime_error("Failed to open /dev/gpiomem");
    }

    gpioMap = (volatile uint32_t*) mmap(
        NULL,
        GPIO_MAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        GPIO_BASE
    );

    if (gpioMap == MAP_FAILED) {
        close(mem_fd);
        throw std::runtime_error("mmap failed");
    }

    // Define outputs
    outputPins = {
        {"compressor_pin", 17},
        {"fan_pin", 27},
        {"valve_pin", 22},
        {"electric_heater_pin", 23}
    };

    // Define inputs
    inputPins = {
        {"alarm_pin", 5},
        {"defrost_pin", 25}
    };

    for (const auto& [name, pin] : outputPins)
        setOutput(pin);

    for (const auto& [name, pin] : inputPins)
        setInput(pin);
}

GpioManager::~GpioManager() {
    if (gpioMap != MAP_FAILED) {
        munmap((void*)gpioMap, GPIO_MAP_SIZE);
    }
    if (mem_fd >= 0) {
        close(mem_fd);
    }
}

void GpioManager::setOutput(int pin) {
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    gpioMap[GPFSEL_OFFSET / 4 + reg] &= ~(7 << shift);
    gpioMap[GPFSEL_OFFSET / 4 + reg] |= (1 << shift); // Output (001)
}

void GpioManager::setInput(int pin) {
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    gpioMap[GPFSEL_OFFSET / 4 + reg] &= ~(7 << shift); // Input (000)
}

void GpioManager::write(const std::string& name, bool value) {
    auto it = outputPins.find(name);
    if (it == outputPins.end()) throw std::invalid_argument("Unknown output pin: " + name);

    int pin = it->second;
    if (value)
        gpioMap[GPSET_OFFSET / 4 + (pin / 32)] = 1 << (pin % 32);
    else
        gpioMap[GPCLR_OFFSET / 4 + (pin / 32)] = 1 << (pin % 32);
}

bool GpioManager::read(const std::string& name) {
    auto it = inputPins.find(name);
    if (it == inputPins.end()) throw std::invalid_argument("Unknown input pin: " + name);

    int pin = it->second;
    return gpioMap[GPLEV_OFFSET / 4 + (pin / 32)] & (1 << (pin % 32));
}
