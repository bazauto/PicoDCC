/* lib/PicoDCCDisplay/touch_driver.cpp */
#include "touch_driver.h"
#include <stdint.h>
#include <cstring>
#include <cstdio>

#ifndef TEST_BUILD
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include <hardware/uart.h>
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
#define CST328_REG_STATUS   0xD000      // Touch status register (16-bit address)
#define CST328_REG_TOUCH    0xD000      // First touch point data (same as status)
#define CST328_REG_MODE_DEBUG_INFO  0xD101  // Debug mode to read chip info
#define CST328_REG_CHIP_INFO    0xD1FC      // Chip info register (returns 0xCACAxxxx)
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
    
    // Wait for CST328 to boot (datasheet: TRON = 200ms)
    sleep_ms(200);
    
    // Verify CST328 is present by reading chip info
    // Reference driver shows we need to try a few times and enable debug mode
    bool chip_found = false;
    for (int attempt = 0; attempt < 3 && !chip_found; attempt++) {
        // Enable debug mode to read chip information
        uint8_t reg_addr[2] = {(CST328_REG_MODE_DEBUG_INFO >> 8) & 0xFF, 
                               CST328_REG_MODE_DEBUG_INFO & 0xFF};
        i2c_write_blocking(i2c0, CST328_I2C_ADDR, reg_addr, 2, false);
        
        sleep_ms(10);
        
        // Read firmware checksum (should be 0xCACAxxxx)
        uint8_t chip_info[4];
        uint8_t info_addr[2] = {(CST328_REG_CHIP_INFO >> 8) & 0xFF,
                                CST328_REG_CHIP_INFO & 0xFF};
        i2c_write_blocking(i2c0, CST328_I2C_ADDR, info_addr, 2, true);
        int result = i2c_read_blocking(i2c0, CST328_I2C_ADDR, chip_info, 4, false);
        
        if (result == 4) {
            // Check if high bytes are 0xCACA
            uint16_t fw_vc = (chip_info[3] << 8) | chip_info[2];
            if (fw_vc == 0xCACA) {
                chip_found = true;
            }
        }
    }
    
    if (!chip_found) {
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

bool TouchDriver::writeRegister(uint16_t reg, uint8_t value) {
#ifdef TEST_BUILD
    return true;
#else
    // CST328 uses 16-bit register addresses
    uint8_t data[3] = {
        static_cast<uint8_t>((reg >> 8) & 0xFF), 
        static_cast<uint8_t>(reg & 0xFF), 
        value
    };
    int result = i2c_write_blocking(i2c0, CST328_I2C_ADDR, data, 3, false);
    return (result == 3);
#endif
}

bool TouchDriver::readRegister(uint16_t reg, uint8_t* value) {
#ifdef TEST_BUILD
    *value = 0x28;  // Mock chip ID
    return true;
#else
    // CST328 uses 16-bit register addresses
    uint8_t reg_addr[2];
    reg_addr[0] = (reg >> 8) & 0xFF;  // High byte
    reg_addr[1] = reg & 0xFF;         // Low byte
    
    // Write 16-bit register address
    int result = i2c_write_blocking(i2c0, CST328_I2C_ADDR, reg_addr, 2, true);
    if (result != 2) {
        return false;
    }
    
    // Read register value
    result = i2c_read_blocking(i2c0, CST328_I2C_ADDR, value, 1, false);
    return (result == 1);
#endif
}

bool TouchDriver::readMultipleRegisters(uint16_t reg, uint8_t* buffer, uint8_t length) {
#ifdef TEST_BUILD
    // Mock read - fill with zeros
    for (uint8_t i = 0; i < length; i++) {
        buffer[i] = 0;
    }
    return true;
#else
    // CST328 uses 16-bit register addresses
    uint8_t reg_addr[2];
    reg_addr[0] = (reg >> 8) & 0xFF;  // High byte
    reg_addr[1] = reg & 0xFF;         // Low byte
    
    // Write 16-bit register address
    int result = i2c_write_blocking(i2c0, CST328_I2C_ADDR, reg_addr, 2, true);
    if (result != 2) {
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
    
    uint8_t raw_data[32];  // Buffer for status + 5 touch points
    
    // Read status register (number of touches)
    if (!readMultipleRegisters(CST328_REG_STATUS, raw_data, 32)) {
        return 0;
    }
    
    // CST328 data format (from reference driver):
    // Byte 0: [ID (4 bits)][State (4 bits)] - state == 6 means pressed
    // Byte 1: X high 8 bits  
    // Byte 2: Y high 8 bits
    // Byte 3: [X low 4 bits][Y low 4 bits]
    // Byte 4: Pressure/weight
    
    // Check if first touch point has valid state (pressed == 6)
    uint8_t state = raw_data[0] & 0x0F;
    
    // Extract coordinates
    uint16_t x_test = (raw_data[1] << 4) | ((raw_data[3] >> 4) & 0x0F);
    uint16_t y_test = (raw_data[2] << 4) | (raw_data[3] & 0x0F);
    
    uint8_t num_touches = 0;
    
    // Determine actual touch count based on state and coordinate validity
    if (state == 6 && x_test > 0 && x_test < 4096 && y_test > 0 && y_test < 4096) {
        num_touches = 1;  // We have at least one valid touch
    }
    
    // Clear touch state when no touches detected
    if (num_touches == 0) {
        has_touch_ = false;
        last_touch_.valid = false;
        return 0;
    }
    
    // We only support single touch for now
    if (num_touches > max_points) {
        num_touches = max_points;
    }
    
    // Limit to requested number of points
    if (num_touches > max_points) {
        num_touches = max_points;
    }
    
    // Parse touch data for each point
    for (uint8_t i = 0; i < num_touches; i++) {
        // CST328 data format: 5 bytes per touch point
        // Finger 1 starts at byte 0, Finger 2 at byte 7 (after 2-byte gap)
        uint8_t offset = (i == 0) ? 0 : (7 + (i - 1) * 5);
        
        // Byte 0: [ID (4 bits)][State (4 bits)]
        uint8_t finger_data = raw_data[offset];
        uint8_t finger_state = finger_data & 0x0F;
        
        // Byte 1: X high 8 bits
        // Byte 2: Y high 8 bits  
        // Byte 3: [X low 4 bits][Y low 4 bits]
        uint16_t x = (raw_data[offset + 1] << 4) | ((raw_data[offset + 3] >> 4) & 0x0F);
        uint16_t y = (raw_data[offset + 2] << 4) | (raw_data[offset + 3] & 0x0F);
        
        // Byte 4: Pressure
        uint8_t pressure = raw_data[offset + 4];
        
        // Store touch point
        points[i].x = x;
        points[i].y = y;
        points[i].event = (finger_state == 6) ? EVENT_CONTACT : EVENT_UP;
        points[i].id = (finger_data >> 4) & 0x0F;
        points[i].valid = (finger_state == 6);  // Valid only if pressed
        
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
