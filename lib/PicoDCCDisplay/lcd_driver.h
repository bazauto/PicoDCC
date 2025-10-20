/* lib/PicoDCCDisplay/lcd_driver.h */
#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef TEST_BUILD
#include "hardware/spi.h"
#include "hardware/gpio.h"
#endif

// GPIO pin definitions (from gpio-pinout-reference.md)
#define LCD_PIN_DC   2   // GP2 - Data/Command select
#define LCD_PIN_RST  3   // GP3 - Reset
#define LCD_PIN_CS   5   // GP5 - Chip Select
#define LCD_PIN_SCK  6   // GP6 - SPI Clock
#define LCD_PIN_MOSI 7   // GP7 - SPI Data Out

// Display dimensions
#define LCD_WIDTH  240
#define LCD_HEIGHT 320

// LCD Driver class (hardware abstraction)
class LcdDriver {
public:
    LcdDriver();
    ~LcdDriver();
    
    // Initialization
    bool init();
    void reset();
    
    // Low-level communication
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void writeData(const uint8_t* buffer, size_t len);
    
    // Display control
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void fillScreen(uint16_t color);
    void pushPixels(const uint16_t* pixels, size_t count);
    
    // Display state
    void displayOn();
    void displayOff();
    void sleep();
    void wakeup();
    
    // Dimensions
    uint16_t getWidth() const { return LCD_WIDTH; }
    uint16_t getHeight() const { return LCD_HEIGHT; }
    
private:
    void initGPIO();
    void initSPI();
    void sendInitSequence();
    
    bool initialized_;
};

#endif // LCD_DRIVER_H
