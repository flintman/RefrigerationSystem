#include "lcd_manager.h"
#include <stdexcept>
#include <cstring>
#include <array>
#include <mutex>

// SMBusDevice implementation
SMBusDevice::SMBusDevice(const char *bus, uint8_t addr) : address(addr)
{
    if ((fd = open(bus, O_RDWR)) < 0)
    {
        throw std::runtime_error("Failed to open I2C bus");
    }

    if (ioctl(fd, I2C_SLAVE, address) < 0)
    {
        close(fd);
        throw std::runtime_error("Failed to acquire bus access");
    }
}

SMBusDevice::~SMBusDevice()
{
    close(fd);
}

void SMBusDevice::smbusWriteByte(uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    if (write(fd, buffer, 2) != 2)
    {
        throw std::runtime_error("SMBus write failed");
    }
}

void SMBusDevice::smbusWriteBlock(uint8_t reg, const uint8_t *data, uint8_t length)
{
    uint8_t *buffer = new uint8_t[length + 1];
    buffer[0] = reg;
    memcpy(buffer + 1, data, length);

    if (write(fd, buffer, length + 1) != length + 1)
    {
        delete[] buffer;
        throw std::runtime_error("SMBus block write failed");
    }
    delete[] buffer;
}

// TCA9548A implementation
TCA9548A_SMBus::TCA9548A_SMBus(const char *bus, uint8_t address)
    : SMBusDevice(bus, address) {}

void TCA9548A_SMBus::selectChannel(uint8_t channel)
{
    if (channel > 7)
        throw std::out_of_range("Channel must be 0-7");
    smbusWriteByte(0, 1 << channel);
    usleep(1);
}

void TCA9548A_SMBus::disableAllChannels()
{
    smbusWriteByte(0, 0x00);
}

// LCD2004 implementation
LCD2004_SMBus::LCD2004_SMBus(std::shared_ptr<TCA9548A_SMBus> multiplexer,
                             uint8_t ch,
                             uint8_t address)
    : SMBusDevice("/dev/i2c-1", address), mux(multiplexer), channel(ch), backlightState(true)
{
}

void LCD2004_SMBus::initiate()
{
    mux->selectChannel(channel);
    usleep(5000);

    // Initialize LCD in 4-bit mode
    write4bits(0x03 << 4);
    usleep(4500);
    write4bits(0x03 << 4);
    usleep(4500);
    write4bits(0x03 << 4);
    usleep(150);
    write4bits(0x02 << 4);

    // Function set
    send(0x28, LCD_CMD);
    // Display control
    send(0x0C, LCD_CMD);
    // Clear display
    send(0x01, LCD_CMD);
    usleep(5000);
    // Entry mode set
    send(0x06, LCD_CMD);

    // Initialize line buffers
    for (auto &line : currentLines)
    {
        line.fill(' ');
    }
}

LCD2004_SMBus::~LCD2004_SMBus()
{
    try
    {
        mux->selectChannel(channel);
        clear();
        backlight(false);
    }
    catch (...)
    {
        // Suppress errors during destruction
    }
}

void LCD2004_SMBus::write4bits(uint8_t value)
{
    uint8_t data[1];
    data[0] = value | LCD_ENABLE | (backlightState ? LCD_BACKLIGHT : 0);
    smbusWriteBlock(0, data, 1);
    usleep(.5);

    data[0] = (value & ~LCD_ENABLE) | (backlightState ? LCD_BACKLIGHT : 0);
    smbusWriteBlock(0, data, 1);
    usleep(.5);
}

void LCD2004_SMBus::send(uint8_t value, uint8_t mode)
{
    uint8_t highnib = value & 0xF0;
    uint8_t lownib = (value << 4) & 0xF0;

    write4bits(highnib | mode);
    usleep(.5);
    write4bits(lownib | mode);
    usleep(.5);
}

void LCD2004_SMBus::clear()
{
    mux->selectChannel(channel);
    send(0x01, LCD_CMD);
    usleep(.5);

    // Reset line buffers
    for (auto &line : currentLines)
    {
        line.fill(' ');
    }
}

void LCD2004_SMBus::setCursor(uint8_t col, uint8_t row)
{
    const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (row > 3)
        row = 3;
    send(0x80 | (col + row_offsets[row]), LCD_CMD);
}

void LCD2004_SMBus::display(const std::string &text, uint8_t line)
{
    if (line >= 4)
        return;

    bool needsUpdate = false;
    std::array<char, 20> newLine;
    size_t i = 0;
    for (; i < text.size() && i < 20; i++)
        newLine[i] = text[i];
    for (; i < 20; i++)
        newLine[i] = ' ';

    mux->selectChannel(channel);

    for (size_t col = 0; col < 20; col++)
    {
        if (currentLines[line][col] != newLine[col])
        {
            setCursor(col, line);
            send(newLine[col], LCD_DATA);
            currentLines[line][col] = newLine[col];
        }
    }
}

void LCD2004_SMBus::backlight(bool on)
{
    backlightState = on;
    mux->selectChannel(channel);
    uint8_t data[1] = {on ? LCD_BACKLIGHT : 0x00};
    smbusWriteBlock(0, data, 1);
}