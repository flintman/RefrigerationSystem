#ifndef WS2811_CONTROLLER_H
#define WS2811_CONTROLLER_H

#include <cstdint>
#include <vector>
#include "ws2811.h"

class WS2811Controller {
public:
    /**
     * @brief Construct a new WS2811Controller object
     * 
     * @param ledCount Number of LEDs in the strip
     * @param gpioPin GPIO pin number (using BCM numbering)
     * @param brightness Initial brightness (0-255)
     */
    WS2811Controller(int ledCount, int gpioPin, uint8_t brightness = 255);
    
    /**
     * @brief Destroy the WS2811Controller object
     */
    ~WS2811Controller();
    
    /**
     * @brief Initialize the LED strip
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Set the color of a single LED
     * 
     * @param index LED index (0-based)
     * @param red Red component (0-255)
     * @param green Green component (0-255)
     * @param blue Blue component (0-255)
     */
    void setLED(int index, uint8_t red, uint8_t green, uint8_t blue);
    
    /**
     * @brief Set the color of all LEDs
     * 
     * @param red Red component (0-255)
     * @param green Green component (0-255)
     * @param blue Blue component (0-255)
     */
    void setAll(uint8_t red, uint8_t green, uint8_t blue);
    
    /**
     * @brief Render the changes to the LED strip
     * @return true if rendering succeeded, false otherwise
     */
    bool render();
    
    /**
     * @brief Clear all LEDs (set to black/off)
     */
    void clear();
    
    /**
     * @brief Set the brightness of the LED strip
     * 
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(uint8_t brightness);
    
private:
    ws2811_t m_ledString;
    int m_ledCount;
    bool m_initialized;
};

#endif // WS2811_CONTROLLER_H