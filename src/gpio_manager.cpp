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

#include "gpio_manager.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

#define GPIO_MAP_SIZE   4096
#define GPIO_BASE       0x0

#define GPFSEL_OFFSET   0x00
#define GPSET_OFFSET    0x1C
#define GPCLR_OFFSET    0x28
#define GPLEV_OFFSET    0x34

void GpioManager::mapGPIO() {
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
}

void GpioManager::unmapGPIO() {
    if (gpioMap != MAP_FAILED) {
        munmap((void*)gpioMap, GPIO_MAP_SIZE);
    }
    if (mem_fd >= 0) {
        close(mem_fd);
    }
}

GpioManager::GpioManager() {
    mapGPIO();

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
        {"defrost_pin", 6},
        {"up_button_pin", 25},
        {"down_button_pin", 16}

    };

    for (const auto& [name, pin] : outputPins)
        setOutput(pin);

    for (const auto& [name, pin] : inputPins)
        setInput(pin);

    // Initialize debounce states
    for (const auto& [name, pin] : inputPins) {
        debounceStates[name] = DebounceState{};
    }
}

GpioManager::~GpioManager() {
    unmapGPIO();
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

    // Enable pull-up resistor for the pin (BCM2835/6/7 only)
    volatile uint32_t* GPPUD = gpioMap + (0x94 / 4);
    volatile uint32_t* GPPUDCLK = gpioMap + (0x98 / 4);

    *GPPUD = 0x2; // 0x2 = Pull-up, 0x1 = Pull-down
    usleep(5);
    *GPPUDCLK = (1 << pin);
    usleep(5);
    *GPPUD = 0;
    *GPPUDCLK = 0;
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

bool GpioManager::read(const std::string& name, int debounce_ms) {
    auto it = inputPins.find(name);
    if (it == inputPins.end()) throw std::invalid_argument("Unknown input pin: " + name);

    int pin = it->second;
    bool rawState = gpioMap[GPLEV_OFFSET / 4 + (pin / 32)] & (1 << (pin % 32));

    // Invert logic: pressed == LOW
    rawState = !rawState;

    std::lock_guard<std::mutex> lock(debounceMutex);
    auto& state = debounceStates[name];
    auto now = std::chrono::steady_clock::now();

    if (rawState != state.lastReadState) {
        state.lastChangeTime = now;
        state.lastReadState = rawState;
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastChangeTime).count() >= debounce_ms) {
        state.lastStableState = state.lastReadState;
    }

    return state.lastStableState;
}