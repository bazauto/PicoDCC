/* lib/PicoDCCDisplay/touch_driver.h */
#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// CST328 capacitive touch controller driver (I2C)
// Hardware: GP8 (SDA), GP9 (SCL), GP10 (INT), GP11 (RST)

// Touch point data structure
struct TouchPoint {
    uint16_t x;           // X coordinate (0-319 for landscape 320x240)
    uint16_t y;           // Y coordinate (0-239 for landscape 320x240)
    uint8_t event;        // Event type (0=down, 1=up, 2=contact)
    uint8_t id;           // Touch point ID (multi-touch support)
    bool valid;           // True if this touch point is active
};

// Maximum touch points supported by CST328
#define MAX_TOUCH_POINTS 5

class TouchDriver {
public:
    TouchDriver();
    ~TouchDriver();
    
    // Initialize I2C and touch controller
    bool init();
    
    // Read current touch state (returns number of active touch points)
    uint8_t readTouchPoints(TouchPoint* points, uint8_t max_points);
    
    // Check if screen is currently touched
    bool isTouched();
    
    // Get last touch event (for LVGL integration)
    bool getLastTouch(uint16_t* x, uint16_t* y);
    
    // Enable/disable touch interrupts
    void enableInterrupt(bool enable);
    
    // Clear any pending interrupt
    void clearInterrupt();
    
private:
    bool initialized_;
    bool interrupt_enabled_;
    
    // Last valid touch point (for LVGL)
    TouchPoint last_touch_;
    bool has_touch_;
    
    // CST328 I2C communication
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t* value);
    bool readMultipleRegisters(uint8_t reg, uint8_t* buffer, uint8_t length);
    
    // Hardware reset sequence
    void hardwareReset();
    
    // Parse touch data from CST328 registers
    void parseTouchData(const uint8_t* raw_data, TouchPoint* points, uint8_t* num_points);
};

#endif // TOUCH_DRIVER_H
