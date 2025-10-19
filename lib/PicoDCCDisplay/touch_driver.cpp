/* lib/PicoDCCDisplay/touch_driver.cpp */
#include "touch_driver.h"
#include <stdint.h>
#include <cstring>

#ifndef TEST_BUILD
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#endif

// CST328 I2C configuration
#define CST328_I2C_ADDR     0x1A        // 7-bit I2C address
#define CST328_I2C_FREQ     400000      // 400kHz I2C clock

// GPIO pin assignments (from hardware spec)
#define TOUCH_I2C_SDA       8           // GP8 = I2C0 SDA
#define TOUCH_I2C_SCL       9           // GP9 = I2C0 SCL
#define TOUCH_INT_PIN       10          // GP10 = INT (active low)
#define TOUCH_RST_PIN       11          // GP11 = RST (active low)

// CST328 register map
#define CST328_REG_STATUS   0x00        // Touch status register
#define CST328_REG_TOUCH    0x01        // First touch point data
#define CST328_REG_MODE     0xFA        // Operating mode
#define CST328_REG_CHIPID   0xFC        // Chip ID register

// Touch event types
#define EVENT_DOWN          0
#define EVENT_UP            1
#define EVENT_CONTACT       2

// Static member initialization
volatile bool TouchDriver::touch_interrupt_pending_ = false;
TouchDriver* TouchDriver::instance_ = nullptr;

TouchDriver::TouchDriver() 
    : initialized_(false)
    , interrupt_enabled_(false)
    , has_touch_(false)
{
    last_touch_.valid = false;
    instance_ = this;
}

TouchDriver::~TouchDriver() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

bool TouchDriver::init() {
#ifdef TEST_BUILD
    // Mock initialization for testing
    initialized_ = true;
    return true;
#else
    if (initialized_) {
        return true;
    }
    
    // Initialize I2C0 hardware
    i2c_init(i2c0, CST328_I2C_FREQ);
    
    // Configure GPIO pins for I2C function
    gpio_set_function(TOUCH_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TOUCH_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TOUCH_I2C_SDA);
    gpio_pull_up(TOUCH_I2C_SCL);
    
    // Configure interrupt pin (input, pull-up)
    gpio_init(TOUCH_INT_PIN);
    gpio_set_dir(TOUCH_INT_PIN, GPIO_IN);
    gpio_pull_up(TOUCH_INT_PIN);
    
    // Configure reset pin (output, initially high)
    gpio_init(TOUCH_RST_PIN);
    gpio_set_dir(TOUCH_RST_PIN, GPIO_OUT);
    gpio_put(TOUCH_RST_PIN, 1);
    
    // Perform hardware reset
    hardwareReset();
    
    // Wait for CST328 to boot (typical: 50-100ms)
    sleep_ms(100);
    
    // Verify communication by reading chip ID
    uint8_t chip_id = 0;
    if (!readRegister(CST328_REG_CHIPID, &chip_id)) {
        return false;
    }
    
    // CST328 should return chip ID (typically 0x28 or 0x32)
    // Accept any non-zero value as valid
    if (chip_id == 0) {
        return false;
    }
    
    // Enable GPIO interrupt for short INT pulses
    enableInterrupt(true);
    
    initialized_ = true;
    return true;
#endif
}

// GPIO interrupt handler (catches short INT pulses)
void TouchDriver::touchInterruptHandler(unsigned int gpio, uint32_t events) {
    if (gpio == TOUCH_INT_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        // Set flag - will be checked during next LVGL poll
        touch_interrupt_pending_ = true;
    }
}

bool TouchDriver::hasPendingTouch() {
    return touch_interrupt_pending_;
}

void TouchDriver::hardwareReset() {
#ifndef TEST_BUILD
    // Reset sequence: LOW for 10ms, then HIGH
    gpio_put(TOUCH_RST_PIN, 0);
    sleep_ms(10);
    gpio_put(TOUCH_RST_PIN, 1);
    sleep_ms(5);
#endif
}

bool TouchDriver::writeRegister(uint8_t reg, uint8_t value) {
#ifdef TEST_BUILD
    return true;
#else
    uint8_t data[2] = {reg, value};
    int result = i2c_write_blocking(i2c0, CST328_I2C_ADDR, data, 2, false);
    return (result == 2);
#endif
}

bool TouchDriver::readRegister(uint8_t reg, uint8_t* value) {
#ifdef TEST_BUILD
    *value = 0x28;  // Mock chip ID
    return true;
#else
    // Write register address
    int result = i2c_write_blocking(i2c0, CST328_I2C_ADDR, &reg, 1, true);
    if (result != 1) {
        return false;
    }
    
    // Read register value
    result = i2c_read_blocking(i2c0, CST328_I2C_ADDR, value, 1, false);
    return (result == 1);
#endif
}

bool TouchDriver::readMultipleRegisters(uint8_t reg, uint8_t* buffer, uint8_t length) {
#ifdef TEST_BUILD
    // Mock read - fill with zeros
    for (uint8_t i = 0; i < length; i++) {
        buffer[i] = 0;
    }
    return true;
#else
    // Write starting register address
    int result = i2c_write_blocking(i2c0, CST328_I2C_ADDR, &reg, 1, true);
    if (result != 1) {
        return false;
    }
    
    // Read multiple bytes
    result = i2c_read_blocking(i2c0, CST328_I2C_ADDR, buffer, length, false);
    return (result == (int)length);
#endif
}

uint8_t TouchDriver::readTouchPoints(TouchPoint* points, uint8_t max_points) {
    if (!initialized_ || points == nullptr || max_points == 0) {
        return 0;
    }
    
#ifdef TEST_BUILD
    // Mock: No touch in test mode
    return 0;
#else
    // Clear interrupt flag at start of read
    touch_interrupt_pending_ = false;
    
    // CST328 touch data format:
    // Byte 0: Number of touch points (0-5)
    // Bytes 1-6: Touch point 1 (6 bytes per point)
    //   [0]: XH (upper 4 bits) + event (lower 4 bits)
    //   [1]: XL (lower 8 bits)
    //   [2]: YH (upper 4 bits) + ID (lower 4 bits)
    //   [3]: YL (lower 8 bits)
    //   [4]: Pressure (not used)
    //   [5]: Area (not used)
    
    uint8_t raw_data[32];  // Buffer for status + 5 touch points
    
    // Read status register (number of touches)
    if (!readMultipleRegisters(CST328_REG_STATUS, raw_data, 32)) {
        return 0;
    }
    
    uint8_t num_touches = raw_data[0] & 0x0F;  // Lower 4 bits
    
    // Clear touch state when no touches detected
    if (num_touches == 0) {
        has_touch_ = false;
        last_touch_.valid = false;
        return 0;
    }
    
    // Validate number of touches
    if (num_touches > MAX_TOUCH_POINTS) {
        has_touch_ = false;
        last_touch_.valid = false;
        return 0;
    }
    
    // Limit to requested number of points
    if (num_touches > max_points) {
        num_touches = max_points;
    }
    
    // Parse touch data for each point
    for (uint8_t i = 0; i < num_touches; i++) {
        uint8_t offset = 1 + (i * 6);  // Each point is 6 bytes
        
        // Extract X coordinate (12 bits)
        uint16_t x = ((raw_data[offset] & 0x0F) << 8) | raw_data[offset + 1];
        
        // Extract Y coordinate (12 bits)
        uint16_t y = ((raw_data[offset + 2] & 0x0F) << 8) | raw_data[offset + 3];
        
        // Extract event type
        uint8_t event = (raw_data[offset] >> 4) & 0x03;
        
        // Extract touch ID
        uint8_t id = raw_data[offset + 2] & 0x0F;
        
        // Store touch point
        points[i].x = x;
        points[i].y = y;
        points[i].event = event;
        points[i].id = id;
        points[i].valid = true;
        
        // Update last touch (use first point for LVGL)
        if (i == 0) {
            last_touch_ = points[0];
            has_touch_ = true;
        }
    }
    
    return num_touches;
#endif
}

bool TouchDriver::isTouched() {
#ifdef TEST_BUILD
    return false;
#else
    if (!initialized_) {
        return false;
    }
    
    // Check INT pin (active low when touched)
    return (gpio_get(TOUCH_INT_PIN) == 0);
#endif
}

bool TouchDriver::getLastTouch(uint16_t* x, uint16_t* y) {
    if (x == nullptr || y == nullptr) {
        return false;
    }
    
    if (!has_touch_ || !last_touch_.valid) {
        return false;
    }
    
    *x = last_touch_.x;
    *y = last_touch_.y;
    
    // Return true only for down and contact events
    return (last_touch_.event == EVENT_DOWN || last_touch_.event == EVENT_CONTACT);
}

void TouchDriver::enableInterrupt(bool enable) {
#ifndef TEST_BUILD
    interrupt_enabled_ = enable;
    
    if (enable) {
        // Set up GPIO interrupt callback
        gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN, 
                                          GPIO_IRQ_EDGE_FALL, 
                                          true, 
                                          &touchInterruptHandler);
    } else {
        // Disable interrupt
        gpio_set_irq_enabled(TOUCH_INT_PIN, GPIO_IRQ_EDGE_FALL, false);
    }
#endif
}

void TouchDriver::clearInterrupt() {
#ifndef TEST_BUILD
    // Read status register to clear interrupt
    uint8_t status;
    readRegister(CST328_REG_STATUS, &status);
#endif
}
