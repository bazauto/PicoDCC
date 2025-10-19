/* lib/PicoDCCDisplay/lcd_driver.cpp */
#include "lcd_driver.h"

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

// ST7789T3 command definitions
#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_INVOFF  0x20
#define ST7789_INVON   0x21
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3A

LcdDriver::LcdDriver() : initialized_(false) {
}

LcdDriver::~LcdDriver() {
    if (initialized_) {
        displayOff();
    }
}

bool LcdDriver::init() {
    if (initialized_) {
        return true;
    }
    
    // Initialize GPIO pins
    initGPIO();
    
    // Initialize SPI0 hardware
    initSPI();
    
    // Reset display
    reset();
    
    // Send ST7789T3 initialization sequence
    sendInitSequence();
    
    initialized_ = true;
    return true;
}

void LcdDriver::initGPIO() {
    // Initialize control pins
    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    
    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_put(LCD_PIN_RST, 1);  // Not in reset initially
    
    gpio_init(LCD_PIN_CS);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);  // CS high (inactive)
}

void LcdDriver::initSPI() {
    // Initialize SPI0 at 62.5MHz (max for ST7789)
    spi_init(spi0, 62500000);
    
    // Configure SPI pins
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
    // Note: MISO (GP4) not configured - display is write-only
    gpio_set_function(LCD_PIN_CS, GPIO_FUNC_SPI);
}

void LcdDriver::reset() {
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(10);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(10);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(120);  // Wait for reset to complete
}

void LcdDriver::writeCommand(uint8_t cmd) {
    gpio_put(LCD_PIN_DC, 0);  // Command mode
    gpio_put(LCD_PIN_CS, 0);  // Select display
    spi_write_blocking(spi0, &cmd, 1);
    gpio_put(LCD_PIN_CS, 1);  // Deselect
}

void LcdDriver::writeData(uint8_t data) {
    gpio_put(LCD_PIN_DC, 1);  // Data mode
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(spi0, &data, 1);
    gpio_put(LCD_PIN_CS, 1);
}

void LcdDriver::writeData(const uint8_t* buffer, size_t len) {
    if (len == 0) return;
    
    gpio_put(LCD_PIN_DC, 1);  // Data mode
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(spi0, buffer, len);
    gpio_put(LCD_PIN_CS, 1);
}

void LcdDriver::sendInitSequence() {
    // ST7789T3 initialization sequence
    writeCommand(ST7789_SWRESET);  // Software reset
    sleep_ms(150);
    
    writeCommand(ST7789_SLPOUT);   // Exit sleep mode
    sleep_ms(10);
    
    writeCommand(ST7789_COLMOD);   // Set color mode
    writeData(0x55);               // 16-bit RGB565
    
    writeCommand(ST7789_MADCTL);   // Memory access control
    writeData(0xA0);               // Landscape mode (270°): MX=1, MY=0, MV=1, RGB=0
    
    writeCommand(ST7789_INVON);    // Display inversion ON (typical for ST7789)
    
    writeCommand(ST7789_DISPON);   // Display ON
    sleep_ms(10);
}

void LcdDriver::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Set column address (CASET)
    writeCommand(ST7789_CASET);
    writeData(x0 >> 8);
    writeData(x0 & 0xFF);
    writeData(x1 >> 8);
    writeData(x1 & 0xFF);
    
    // Set row address (RASET)
    writeCommand(ST7789_RASET);
    writeData(y0 >> 8);
    writeData(y0 & 0xFF);
    writeData(y1 >> 8);
    writeData(y1 & 0xFF);
    
    // Write to RAM
    writeCommand(ST7789_RAMWR);
}

void LcdDriver::fillScreen(uint16_t color) {
    setWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    
    // Convert color to bytes (big-endian for ST7789)
    uint8_t color_bytes[2] = {
        static_cast<uint8_t>(color >> 8),
        static_cast<uint8_t>(color & 0xFF)
    };
    
    // Send color for all pixels
    gpio_put(LCD_PIN_DC, 1);  // Data mode
    gpio_put(LCD_PIN_CS, 0);
    for (uint32_t i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        spi_write_blocking(spi0, color_bytes, 2);
    }
    gpio_put(LCD_PIN_CS, 1);
}

void LcdDriver::pushPixels(const uint16_t* pixels, size_t count) {
    if (count == 0) return;
    
    gpio_put(LCD_PIN_DC, 1);  // Data mode
    gpio_put(LCD_PIN_CS, 0);
    
    // Write pixels in chunks (SPI DMA could be used here in future)
    spi_write_blocking(spi0, reinterpret_cast<const uint8_t*>(pixels), count * 2);
    
    gpio_put(LCD_PIN_CS, 1);
}

void LcdDriver::displayOn() {
    writeCommand(ST7789_DISPON);
}

void LcdDriver::displayOff() {
    writeCommand(ST7789_DISPOFF);
}

void LcdDriver::sleep() {
    writeCommand(ST7789_SLPIN);
    sleep_ms(5);
}

void LcdDriver::wakeup() {
    writeCommand(ST7789_SLPOUT);
    sleep_ms(120);
}

#endif // !TEST_BUILD
