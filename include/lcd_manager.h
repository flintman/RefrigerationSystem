#ifndef LCD_SMBUS_H
#define LCD_SMBUS_H

#include <string>
#include <memory>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <array>
#include <mutex>
#include <memory>
#include <unistd.h>

class SMBusDevice {
protected:
    int fd;
    uint8_t address;

    void smbusWriteByte(uint8_t reg, uint8_t value);
    void smbusWriteBlock(uint8_t reg, const uint8_t* data, uint8_t length);
    
public:
    SMBusDevice(const char* bus, uint8_t addr);
    virtual ~SMBusDevice();
};

class TCA9548A_SMBus : public SMBusDevice {
public:
    TCA9548A_SMBus(const char* bus = "/dev/i2c-1", uint8_t address = 0x70);
    void selectChannel(uint8_t channel);
    void disableAllChannels();
};

class LCD2004_SMBus : public SMBusDevice {
private:
    std::shared_ptr<TCA9548A_SMBus> mux;
    std::array<std::array<char, 20>, 4> currentLines;
    std::mutex lcdMutex;
    uint8_t channel;
    bool backlightState;
    
    // LCD constants
    static const uint8_t LCD_ENABLE = 0x04;
    static const uint8_t LCD_BACKLIGHT = 0x08;
    static const uint8_t LCD_CMD = 0x00;
    static const uint8_t LCD_DATA = 0x01;
    
    void write4bits(uint8_t value);
    void send(uint8_t value, uint8_t mode);

public:
    LCD2004_SMBus(std::shared_ptr<TCA9548A_SMBus> multiplexer, 
                 uint8_t channel, 
                 uint8_t address = 0x27);
    ~LCD2004_SMBus();
    
    void clear();
    void setCursor(uint8_t col, uint8_t row);
    void display(const std::string& text, uint8_t line);
    void backlight(bool on);
};

#endif // LCD_SMBUS_H