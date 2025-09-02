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

#include "WS2811Controller.h"
#include <stdexcept>
#include <iostream>

WS2811Controller::WS2811Controller(int ledCount, int gpioPin, uint8_t brightness)
    : m_ledCount(ledCount), m_initialized(false) {

    // Initialize the LED string configuration
    m_ledString.freq = WS2811_TARGET_FREQ;
    m_ledString.dmanum = 10; // DMA channel (5 or 10 are safe choices)

    // Channel 0 configuration
    m_ledString.channel[0].gpionum = gpioPin;
    m_ledString.channel[0].count = ledCount;
    m_ledString.channel[0].invert = 0;
    m_ledString.channel[0].brightness = brightness;
    m_ledString.channel[0].strip_type = WS2811_STRIP_RGB;

    // Channel 1 is unused
    m_ledString.channel[1].gpionum = 0;
    m_ledString.channel[1].count = 0;
    m_ledString.channel[1].invert = 0;
    m_ledString.channel[1].brightness = 0;
}

WS2811Controller::~WS2811Controller() {
    if (m_initialized) {
        clear();
        render();
        ws2811_fini(&m_ledString);
    }
}

bool WS2811Controller::initialize() {
    if (ws2811_init(&m_ledString) != WS2811_SUCCESS) {
        std::cerr << "Failed to initialize WS2811 controller" << std::endl;
        return false;
    }
    m_initialized = true;
    return true;
}

void WS2811Controller::setLED(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index < 0 || index >= m_ledCount) {
        throw std::out_of_range("LED index out of range");
    }

    m_ledString.channel[0].leds[index] = (green << 16) | (red << 8) | blue;
}

void WS2811Controller::setAll(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < m_ledCount; i++) {
        setLED(i, red, green, blue);
    }
}

bool WS2811Controller::render() {
    if (!m_initialized) {
        return false;
    }
    return ws2811_render(&m_ledString) == WS2811_SUCCESS;
}

void WS2811Controller::clear() {
    setAll(0, 0, 0);
}

void WS2811Controller::setBrightness(uint8_t brightness) {
    m_ledString.channel[0].brightness = brightness;
}