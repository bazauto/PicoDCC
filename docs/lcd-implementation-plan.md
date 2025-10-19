# LCD Integration Implementation Plan

**Date**: October 19, 2025  
**Branch**: `feature/lcd-display`  
**Status**: Phase 2 Complete (Landscape Mode)  
**Design Document**: `lcd-design-questionnaire.md`  
**Orientation Update**: See `lcd-landscape-orientation.md` for landscape configuration details

---

## ⚠️ Important: Landscape Orientation

**This plan was originally written for portrait mode (240×320).  
As of Phase 2, the display is configured in landscape mode (320×240).**

**All resolution-dependent code now uses**:
- **Horizontal**: 320 pixels (was 240)
- **Vertical**: 240 pixels (was 320)
- **MADCTL Register**: `0x60` (90° rotation)

See `docs/lcd-landscape-orientation.md` for:
- Complete configuration changes
- Layout design rationale
- Future phase guidelines for landscape mode

---

## Executive Summary

This document provides a detailed, phase-by-phase implementation plan for integrating the Waveshare WAV-27579 LCD display into PicoDCC. The plan is based on validated design decisions and includes task breakdowns, code structure, integration points, and testing checkpoints.

### Design Overview (Updated for Landscape)

```yaml
Hardware:
  Display Controller: ST7789T3
  Resolution: 320x240 pixels (landscape orientation)
  Color Depth: 16-bit RGB565 (262K colors)
  Touch Controller: CST328 capacitive I2C
  Interface: SPI0 (display) + I2C0 (touch)

Memory Allocation:
  Framebuffer: 150KB (320 × 240 × 2 bytes)
  LVGL Heap: ~20-30KB
  Total LCD RAM: ~165KB
  Available RAM: ~95KB remaining (264KB total)

Software Architecture:
  Graphics Library: LVGL 8.3
  Update Strategy: Polling in main loop (10Hz)
  Touch Input: Interrupt-driven (GP10 INT pin)
  Integration: New PicoDCCDisplay component

GPIO Assignments:
  Display SPI0: GP4-7 (SCK, MOSI, CS, MISO-unused)
  Display Control: GP2 (DC), GP3 (RST)
  Touch I2C0: GP8 (SDA), GP9 (SCL)
  Touch Control: GP10 (INT), GP11 (RST)
  Backlight: Tied to 3.3V (always on)

UI Design:
  Main Screen: Diagnostic status (multi-column layout)
  Color Scheme: Black background, white/green text, red/yellow alerts
  Touch Buttons: MAIN PWR, PROG PWR, RESET TRIPS, CALIBRATE, DIAGNOSTICS, SETTINGS
```

---

## Development Phases

### Timeline Overview

```
Week 1: Phase 1 - Hardware Bring-Up (Foundation)
Week 2: Phase 2 - Basic UI (Status Display)
Week 2-3: Phase 3 - Diagnostic Integration (Core Feature)
Week 3-4: Phase 4 - Touch Input (Interactivity)
Week 4+: Phase 5 - Advanced UI (Polish)
```

---

## Phase 1: Hardware Bring-Up (Week 1)

**Goal**: Initialize hardware, verify communication, display test pattern

### Task 1.1: Add LVGL as Git Submodule

**Objective**: Integrate LVGL library into PicoDCC repository

**Steps**:
```bash
# Navigate to repository root
cd /e/Development/PicoDCC

# Add LVGL as submodule in lib/external/
git submodule add https://github.com/lvgl/lvgl.git lib/external/lvgl
git submodule update --init --recursive

# Checkout stable version (LVGL 8.3.x or 9.x)
cd lib/external/lvgl
git checkout release/v8.3  # Or latest stable
cd ../../..

# Commit submodule addition
git add .gitmodules lib/external/lvgl
git commit -m "Add LVGL graphics library as submodule"
```

**Deliverable**: LVGL source code available in `lib/external/lvgl/`

**Testing**: Verify `lib/external/lvgl/lvgl.h` exists

**Time Estimate**: 10 minutes

---

### Task 1.2: Create Component Directory Structure

**Objective**: Set up PicoDCCDisplay component with proper organization

**Directory Structure**:
```
lib/PicoDCCDisplay/
├── CMakeLists.txt                 # Build configuration
├── pico_dcc_display.h             # Main display interface (public API)
├── pico_dcc_display.cpp           # Display implementation
├── lcd_driver.h                   # Hardware abstraction layer (private)
├── lcd_driver.cpp                 # ST7789T3 driver implementation
├── touch_driver.h                 # Touch controller abstraction (private)
├── touch_driver.cpp               # CST328 driver implementation
├── lv_conf.h                      # LVGL configuration
├── ui/
│   ├── ui_screens.h               # Screen definitions (private)
│   ├── ui_screens.cpp             # Screen implementations
│   ├── ui_diagnostic_screen.h     # Diagnostic log screen (private)
│   ├── ui_diagnostic_screen.cpp   # Diagnostic log implementation
│   ├── ui_calibration_screen.h    # Calibration screen (private)
│   └── ui_calibration_screen.cpp  # Calibration implementation
└── mocks/
    ├── lcd_driver_mock.h          # Mock for TEST_BUILD
    ├── lcd_driver_mock.cpp        # Mock implementations
    ├── touch_driver_mock.h        # Touch mock for TEST_BUILD
    └── touch_driver_mock.cpp      # Touch mock implementations
```

**Create Files**:
```bash
mkdir -p lib/PicoDCCDisplay/ui
mkdir -p lib/PicoDCCDisplay/mocks
touch lib/PicoDCCDisplay/CMakeLists.txt
touch lib/PicoDCCDisplay/pico_dcc_display.h
touch lib/PicoDCCDisplay/pico_dcc_display.cpp
touch lib/PicoDCCDisplay/lcd_driver.h
touch lib/PicoDCCDisplay/lcd_driver.cpp
touch lib/PicoDCCDisplay/touch_driver.h
touch lib/PicoDCCDisplay/touch_driver.cpp
touch lib/PicoDCCDisplay/lv_conf.h
touch lib/PicoDCCDisplay/ui/ui_screens.h
touch lib/PicoDCCDisplay/ui/ui_screens.cpp
touch lib/PicoDCCDisplay/ui/ui_diagnostic_screen.h
touch lib/PicoDCCDisplay/ui/ui_diagnostic_screen.cpp
touch lib/PicoDCCDisplay/mocks/lcd_driver_mock.h
touch lib/PicoDCCDisplay/mocks/lcd_driver_mock.cpp
```

**Deliverable**: Complete directory structure with empty files

**Testing**: `ls -R lib/PicoDCCDisplay/` shows all files

**Time Estimate**: 15 minutes

---

### Task 1.3: Configure LVGL (lv_conf.h)

**Objective**: Create LVGL configuration for PicoDCC requirements

**File**: `lib/PicoDCCDisplay/lv_conf.h`

**Key Configuration Settings**:
```c
/* lib/PicoDCCDisplay/lv_conf.h */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16          // 16-bit RGB565
#define LV_COLOR_16_SWAP 0         // No byte swap for RP2040 SPI

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0            // Use LVGL's built-in allocator
#define LV_MEM_SIZE (30U * 1024U)  // 30KB for LVGL heap (widgets, styles)

/*====================
   DISPLAY SETTINGS
 *====================*/
#define LV_HOR_RES_MAX 240         // Horizontal resolution
#define LV_VER_RES_MAX 320         // Vertical resolution
#define LV_DPI_DEF 100             // DPI (affects text rendering)

/*====================
   FRAMEBUFFER
 *====================*/
#define LV_USE_GPU_RP2040_RENDER 0 // No GPU acceleration (RP2040 has none)
#define LV_DISP_DEF_REFR_PERIOD 100 // 100ms = 10Hz refresh (adjust in runtime)

/*====================
   INPUT DEVICE SETTINGS
 *====================*/
#define LV_INDEV_DEF_READ_PERIOD 10 // 10ms touch polling (when active)

/*====================
   FEATURE USAGE
 *====================*/
#define LV_USE_ANIMATION 1         // Enable animations (smooth transitions)
#define LV_USE_SHADOW 0            // Disable shadows (save RAM)
#define LV_USE_BLEND_MODES 0       // Disable blend modes (save CPU)
#define LV_USE_OPA_SCALE 1         // Enable opacity scaling
#define LV_USE_IMG_TRANSFORM 0     // Disable image rotation (save CPU)

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_12 1    // Small text (status labels)
#define LV_FONT_MONTSERRAT_14 1    // Normal text (diagnostics)
#define LV_FONT_MONTSERRAT_16 1    // Medium text (buttons)
#define LV_FONT_MONTSERRAT_20 0    // Large text (disabled)
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGET USAGE
 *====================*/
#define LV_USE_BTN 1               // Buttons (for touch controls)
#define LV_USE_LABEL 1             // Text labels
#define LV_USE_LIST 1              // Scrollable list (for diagnostics)
#define LV_USE_TEXTAREA 0          // Text input (not needed)
#define LV_USE_CANVAS 0            // Drawing canvas (not needed)
#define LV_USE_CHART 0             // Charts (future: current graphs)
#define LV_USE_TABLE 0             // Tables (not needed)

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT 1     // Default theme
#define LV_THEME_DEFAULT_DARK 1    // Dark mode (black background)

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN  // Only warnings and errors
#define LV_LOG_PRINTF 1            // Use printf() for LVGL logs

#endif /* LV_CONF_H */
```

**Deliverable**: `lv_conf.h` configured for 240x320, 16-bit color, 30KB heap

**Testing**: Include `lvgl.h` in a test file, check for compilation errors

**Time Estimate**: 20 minutes

---

### Task 1.4: Create CMakeLists.txt for PicoDCCDisplay

**Objective**: Configure build system to compile PicoDCCDisplay and LVGL

**File**: `lib/PicoDCCDisplay/CMakeLists.txt`

**Content**:
```cmake
# lib/PicoDCCDisplay/CMakeLists.txt

# PicoDCCDisplay Library - LCD Integration
add_library(PicoDCCDisplay STATIC
    pico_dcc_display.cpp
    lcd_driver.cpp
    touch_driver.cpp
    ui/ui_screens.cpp
    ui/ui_diagnostic_screen.cpp
)

# Include directories
target_include_directories(PicoDCCDisplay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(PicoDCCDisplay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# LVGL library configuration
if(NOT TEST_BUILD)
    # Hardware build - include LVGL
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/external/lvgl ${CMAKE_BINARY_DIR}/lvgl)
    
    target_link_libraries(PicoDCCDisplay PUBLIC
        lvgl
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_gpio
    )
    
    # Add LVGL configuration path
    target_compile_definitions(PicoDCCDisplay PUBLIC
        LV_CONF_INCLUDE_SIMPLE
    )
    
    target_include_directories(PicoDCCDisplay PUBLIC
        ${CMAKE_SOURCE_DIR}/lib/external/lvgl
    )
else()
    # Test build - use mocks instead of real drivers
    target_sources(PicoDCCDisplay PRIVATE
        mocks/lcd_driver_mock.cpp
        mocks/touch_driver_mock.cpp
    )
    
    target_include_directories(PicoDCCDisplay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )
    
    # Mock LVGL for test compilation (minimal stubs)
    target_compile_definitions(PicoDCCDisplay PUBLIC
        TEST_BUILD
        LV_CONF_SKIP  # Skip LVGL config in test mode
    )
endif()

# Link diagnostic system
target_link_libraries(PicoDCCDisplay PUBLIC
    # Future: Link to diagnostic library when it's extracted
)
```

**Update**: `lib/CMakeLists.txt` to include PicoDCCDisplay:
```cmake
# lib/CMakeLists.txt
add_subdirectory(PicoDCCController)
add_subdirectory(PicoDCCEX)
add_subdirectory(PicoDCCLoco)
add_subdirectory(PicoDCCTrack)
add_subdirectory(PicoDCCDisplay)  # <-- ADD THIS LINE
```

**Deliverable**: CMakeLists configured for both TEST_BUILD and hardware modes

**Testing**: Run `cmake --build build` and verify no errors

**Time Estimate**: 25 minutes

---

### Task 1.5: Implement LCD Hardware Driver (lcd_driver.cpp)

**Objective**: Create ST7789T3 driver with SPI0 initialization

**File**: `lib/PicoDCCDisplay/lcd_driver.h`

**Header**:
```cpp
/* lib/PicoDCCDisplay/lcd_driver.h */
#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
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
```

**File**: `lib/PicoDCCDisplay/lcd_driver.cpp`

**Implementation** (ST7789T3 specific):
```cpp
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
    writeData(0x00);               // Row/column order (adjust for orientation)
    
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
```

**Deliverable**: Complete ST7789T3 driver with SPI0 communication

**Testing**: Compile in hardware mode, check for errors

**Time Estimate**: 60 minutes

---

### Task 1.6: Create Test Pattern Display

**Objective**: Verify hardware initialization with simple color test

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h` (initial version)

```cpp
/* lib/PicoDCCDisplay/pico_dcc_display.h */
#ifndef PICO_DCC_DISPLAY_H
#define PICO_DCC_DISPLAY_H

#include <stdint.h>
#include "lcd_driver.h"

// RGB565 color definitions
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

class PicoDCCDisplay {
public:
    PicoDCCDisplay();
    ~PicoDCCDisplay();
    
    // Initialization
    bool init();
    
    // Test methods (Phase 1 only)
    void displayTestPattern();
    void displayBootMessage();
    
    // Future: Full UI methods will be added in Phase 2+
    
private:
    LcdDriver lcd_;
    bool initialized_;
};

#endif // PICO_DCC_DISPLAY_H
```

**File**: `lib/PicoDCCDisplay/pico_dcc_display.cpp` (initial version)

```cpp
/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#include "pico_dcc_display.h"

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#endif

PicoDCCDisplay::PicoDCCDisplay() : initialized_(false) {
}

PicoDCCDisplay::~PicoDCCDisplay() {
}

bool PicoDCCDisplay::init() {
    if (initialized_) {
        return true;
    }
    
    // Initialize LCD hardware
    if (!lcd_.init()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void PicoDCCDisplay::displayTestPattern() {
    if (!initialized_) return;
    
    // Display color bars (8 colors, each 40 pixels tall)
    const uint16_t colors[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
        COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE, COLOR_BLACK
    };
    
    for (int i = 0; i < 8; i++) {
        lcd_.setWindow(0, i * 40, 239, (i + 1) * 40 - 1);
        lcd_.fillScreen(colors[i]);
    }
}

void PicoDCCDisplay::displayBootMessage() {
    if (!initialized_) return;
    
    // Black screen for now (text rendering comes in Phase 2 with LVGL)
    lcd_.fillScreen(COLOR_BLACK);
    
    // Note: "PicoDCC v1.0" text will be added once LVGL is integrated
}
```

**Deliverable**: Display shows 8-color test pattern

**Testing**: Flash to hardware, verify color bars appear on LCD

**Time Estimate**: 30 minutes

---

### Task 1.7: Integrate Display into Main Application

**Objective**: Call display initialization from main()

**File**: `src/pico_dcc.cpp` (modifications)

**Changes**:
```cpp
/* src/pico_dcc.cpp */
#include "pico_dcc_display.h"  // <-- ADD THIS

// ... existing includes and code ...

int main() {
    // Existing initialization
    stdio_init_all();
    
    // Initialize display (PHASE 1 - TEST PATTERN)
    PicoDCCDisplay display;
    if (!display.init()) {
        // Display failed - log error
        printf("ERROR: LCD initialization failed\n");
    } else {
        display.displayTestPattern();  // Show color bars
        sleep_ms(2000);                // Display for 2 seconds
        display.displayBootMessage();  // Clear to black
    }
    
    // ... rest of existing main() code ...
}
```

**Update**: `src/CMakeLists.txt` to link PicoDCCDisplay:
```cmake
target_link_libraries(PicoDCC
    PicoDCCController
    PicoDCCEX
    PicoDCCLoco
    PicoDCCTrack
    PicoDCCDisplay  # <-- ADD THIS
)
```

**Deliverable**: PicoDCC shows test pattern on boot

**Testing**: Flash firmware, verify LCD displays color bars then goes black

**Time Estimate**: 20 minutes

---

## Phase 1 Summary

**Completion Criteria**:
- ✅ LVGL library added as git submodule
- ✅ PicoDCCDisplay component structure created
- ✅ CMakeLists configured for hardware and test builds
- ✅ ST7789T3 driver implemented with SPI0
- ✅ Test pattern displays on LCD (8 color bars)
- ✅ Boot message clears screen to black
- ✅ Code compiles in both TEST_BUILD and hardware modes

**Validation**:
1. Run `cmake --build build` - no errors
2. Flash firmware to Pico
3. Observe LCD showing:
   - 8 horizontal color bars for 2 seconds
   - Then black screen
4. Confirm no compile errors in TEST_BUILD mode

**Time Estimate**: 3-4 hours total

**Git Commit**:
```bash
git add lib/PicoDCCDisplay/ lib/external/lvgl lib/CMakeLists.txt src/pico_dcc.cpp src/CMakeLists.txt
git commit -m "Phase 1: LCD hardware bring-up with test pattern

- Add LVGL 8.3 as git submodule
- Create PicoDCCDisplay component structure
- Implement ST7789T3 driver (SPI0, 62.5MHz)
- Display 8-color test pattern on boot
- Configure for 240x320, 16-bit RGB565
- Add mocks for TEST_BUILD compatibility"
```

---

## Phase 2: LVGL Integration & Basic UI (Week 2)

**Goal**: Integrate LVGL, create diagnostic screen, display real-time messages

### Task 2.1: LVGL Display Driver Integration

**Objective**: Connect LVGL to LcdDriver hardware abstraction

**File**: `lib/PicoDCCDisplay/pico_dcc_display.cpp` (expand)

**Add LVGL Flush Callback**:
```cpp
/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#ifndef TEST_BUILD
#include "lvgl/lvgl.h"

// Static callback for LVGL to write to display
static void lvgl_flush_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
    LcdDriver* lcd = static_cast<LcdDriver*>(disp_drv->user_data);
    
    // Set drawing window
    lcd->setWindow(area->x1, area->y1, area->x2, area->y2);
    
    // Calculate pixel count
    uint32_t width = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;
    uint32_t pixel_count = width * height;
    
    // Push pixels to display
    lcd->pushPixels(reinterpret_cast<uint16_t*>(color_p), pixel_count);
    
    // Tell LVGL flush is complete
    lv_disp_flush_ready(disp_drv);
}

// Framebuffer (76KB for 240x320x16-bit)
static lv_color_t framebuffer[240 * 320];

bool PicoDCCDisplay::init() {
    if (initialized_) {
        return true;
    }
    
    // Initialize LCD hardware
    if (!lcd_.init()) {
        return false;
    }
    
    // Initialize LVGL
    lv_init();
    
    // Register display driver
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, framebuffer, nullptr, 240 * 320);
    
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = &lcd_;
    lv_disp_drv_register(&disp_drv);
    
    initialized_ = true;
    return true;
}
#endif
```

**Deliverable**: LVGL connected to ST7789T3 hardware

**Testing**: LVGL hello world (simple label) displays

**Time Estimate**: 45 minutes

---

### Task 2.2: Add LVGL Update Method

**Objective**: Call LVGL tick handler in main loop

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h`

**Add Update Method**:
```cpp
class PicoDCCDisplay {
public:
    // ... existing methods ...
    
    // Runtime updates
    void update();  // Call in main loop (~10Hz)
    
private:
    uint32_t last_update_ms_;
    static const uint32_t UPDATE_INTERVAL_MS = 100;  // 10Hz refresh
};
```

**File**: `lib/PicoDCCDisplay/pico_dcc_display.cpp`

**Implementation**:
```cpp
void PicoDCCDisplay::update() {
    if (!initialized_) return;
    
#ifndef TEST_BUILD
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    
    // Throttle updates to ~10Hz
    if (now_ms - last_update_ms_ < UPDATE_INTERVAL_MS) {
        return;
    }
    last_update_ms_ = now_ms;
    
    // Update LVGL tick counter
    lv_tick_inc(now_ms - last_update_ms_);
    
    // Handle LVGL tasks (rendering, animations)
    lv_task_handler();
#endif
}
```

**Update Main Loop**: `src/pico_dcc.cpp`
```cpp
int main() {
    // ... initialization ...
    
    while (true) {
        // Existing controller loop
        controller.loop();
        
        // Update display (10Hz throttled internally)
        display.update();
    }
}
```

**Deliverable**: LVGL updates integrated into main loop

**Testing**: Display refreshes without blocking DCC operations

**Time Estimate**: 30 minutes

---

### Task 2.3: Create Diagnostic Screen UI

**Objective**: Build scrolling message log screen

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.h`

```cpp
/* lib/PicoDCCDisplay/ui/ui_diagnostic_screen.h */
#ifndef UI_DIAGNOSTIC_SCREEN_H
#define UI_DIAGNOSTIC_SCREEN_H

#ifndef TEST_BUILD
#include "lvgl/lvgl.h"
#endif

#include <stdint.h>

// Diagnostic message severity (matches pico_diagnostic.h)
enum DiagnosticSeverity {
    DIAG_INFO = 0,
    DIAG_WARNING = 1,
    DIAG_ERROR = 2,
    DIAG_CRITICAL = 3
};

class DiagnosticScreen {
public:
    DiagnosticScreen();
    ~DiagnosticScreen();
    
    // Screen lifecycle
    void create();
    void show();
    void hide();
    
    // Message handling
    void addMessage(DiagnosticSeverity severity, const char* message);
    void clearMessages();
    
    // Configuration
    void setMaxMessages(uint16_t max) { max_messages_ = max; }
    
private:
#ifndef TEST_BUILD
    lv_obj_t* screen_;       // Main screen object
    lv_obj_t* title_label_;  // "System Messages" header
    lv_obj_t* msg_list_;     // Scrollable message list
    lv_obj_t* clear_btn_;    // Clear log button
#endif
    
    uint16_t max_messages_;
    uint16_t message_count_;
    
    const char* getSeverityIcon(DiagnosticSeverity severity);
    uint32_t getSeverityColor(DiagnosticSeverity severity);
};

#endif // UI_DIAGNOSTIC_SCREEN_H
```

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.cpp`

```cpp
/* lib/PicoDCCDisplay/ui/ui_diagnostic_screen.cpp */
#include "ui_diagnostic_screen.h"

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#include <cstdio>

// LVGL color definitions (RGB565)
#define LV_COLOR_DCC_BLACK   lv_color_make(0x00, 0x00, 0x00)
#define LV_COLOR_DCC_WHITE   lv_color_make(0xFF, 0xFF, 0xFF)
#define LV_COLOR_DCC_GREEN   lv_color_make(0x00, 0xFF, 0x00)
#define LV_COLOR_DCC_YELLOW  lv_color_make(0xFF, 0xFF, 0x00)
#define LV_COLOR_DCC_ORANGE  lv_color_make(0xFF, 0xA5, 0x00)
#define LV_COLOR_DCC_RED     lv_color_make(0xFF, 0x00, 0x00)

DiagnosticScreen::DiagnosticScreen()
    : screen_(nullptr)
    , title_label_(nullptr)
    , msg_list_(nullptr)
    , clear_btn_(nullptr)
    , max_messages_(50)
    , message_count_(0) {
}

DiagnosticScreen::~DiagnosticScreen() {
    // LVGL cleans up children automatically when parent is deleted
}

void DiagnosticScreen::create() {
    // Create screen object
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, LV_COLOR_DCC_BLACK, 0);
    
    // Create title label at top
    title_label_ = lv_label_create(screen_);
    lv_label_set_text(title_label_, "System Messages");
    lv_obj_set_style_text_color(title_label_, LV_COLOR_DCC_WHITE, 0);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 5);
    
    // Create scrollable list for messages
    msg_list_ = lv_list_create(screen_);
    lv_obj_set_size(msg_list_, 230, 250);  // Leave room for title and button
    lv_obj_align(msg_list_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(msg_list_, LV_COLOR_DCC_BLACK, 0);
    lv_obj_set_style_border_width(msg_list_, 1, 0);
    lv_obj_set_style_border_color(msg_list_, LV_COLOR_DCC_WHITE, 0);
    
    // Create clear button at bottom
    clear_btn_ = lv_btn_create(screen_);
    lv_obj_set_size(clear_btn_, 120, 30);
    lv_obj_align(clear_btn_, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    lv_obj_t* btn_label = lv_label_create(clear_btn_);
    lv_label_set_text(btn_label, "CLEAR LOG");
    lv_obj_center(btn_label);
    
    // Button click handler (clear messages)
    lv_obj_add_event_cb(clear_btn_, [](lv_event_t* e) {
        DiagnosticScreen* screen = static_cast<DiagnosticScreen*>(lv_event_get_user_data(e));
        screen->clearMessages();
    }, LV_EVENT_CLICKED, this);
}

void DiagnosticScreen::show() {
    if (screen_) {
        lv_scr_load(screen_);
    }
}

void DiagnosticScreen::hide() {
    // Screen switching handled by LVGL
}

void DiagnosticScreen::addMessage(DiagnosticSeverity severity, const char* message) {
    if (!msg_list_) return;
    
    // Limit message count (remove oldest if over limit)
    if (message_count_ >= max_messages_) {
        lv_obj_t* first_child = lv_obj_get_child(msg_list_, 0);
        if (first_child) {
            lv_obj_del(first_child);
            message_count_--;
        }
    }
    
    // Format message with timestamp and severity
    char formatted_msg[128];
    uint32_t timestamp_ms = to_ms_since_boot(get_absolute_time());
    uint32_t hours = (timestamp_ms / 3600000) % 24;
    uint32_t minutes = (timestamp_ms / 60000) % 60;
    uint32_t seconds = (timestamp_ms / 1000) % 60;
    
    snprintf(formatted_msg, sizeof(formatted_msg), "%s %02u:%02u:%02u %s",
             getSeverityIcon(severity), hours, minutes, seconds, message);
    
    // Add list item
    lv_obj_t* item = lv_list_add_text(msg_list_, formatted_msg);
    lv_obj_set_style_text_color(item, lv_color_hex(getSeverityColor(severity)), 0);
    lv_obj_set_style_text_font(item, &lv_font_montserrat_12, 0);
    
    message_count_++;
    
    // Scroll to bottom
    lv_obj_scroll_to_y(msg_list_, LV_COORD_MAX, LV_ANIM_ON);
}

void DiagnosticScreen::clearMessages() {
    if (!msg_list_) return;
    
    lv_obj_clean(msg_list_);
    message_count_ = 0;
}

const char* DiagnosticScreen::getSeverityIcon(DiagnosticSeverity severity) {
    switch (severity) {
        case DIAG_INFO:     return "✓";  // Checkmark
        case DIAG_WARNING:  return "⚠";  // Warning triangle
        case DIAG_ERROR:    return "✗";  // X mark
        case DIAG_CRITICAL: return "🔥"; // Fire (critical)
        default:            return "•";  // Bullet
    }
}

uint32_t DiagnosticScreen::getSeverityColor(DiagnosticSeverity severity) {
    switch (severity) {
        case DIAG_INFO:     return 0x00FF00;  // Green
        case DIAG_WARNING:  return 0xFFA500;  // Orange
        case DIAG_ERROR:    return 0xFFFF00;  // Yellow
        case DIAG_CRITICAL: return 0xFF0000;  // Red
        default:            return 0xFFFFFF;  // White
    }
}

#endif // !TEST_BUILD
```

**Deliverable**: Diagnostic screen with scrollable message list

**Testing**: Manually add test messages, verify scrolling and colors

**Time Estimate**: 90 minutes

---

### Task 2.4: Integrate Diagnostic Screen into Display Manager

**Objective**: Expose diagnostic screen through PicoDCCDisplay API

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h`

**Add Public Methods**:
```cpp
#include "ui/ui_diagnostic_screen.h"

class PicoDCCDisplay {
public:
    // ... existing methods ...
    
    // UI management
    void showDiagnosticScreen();
    
    // Diagnostic logging
    void logMessage(DiagnosticSeverity severity, const char* message);
    void clearDiagnosticLog();
    
private:
    DiagnosticScreen diag_screen_;
};
```

**File**: `lib/PicoDCCDisplay/pico_dcc_display.cpp`

**Implementation**:
```cpp
bool PicoDCCDisplay::init() {
    // ... existing LVGL init ...
    
    // Create diagnostic screen UI
    diag_screen_.create();
    diag_screen_.show();  // Default screen
    
    // Display boot message
    logMessage(DIAG_INFO, "SYSTEM: Boot complete");
    logMessage(DIAG_INFO, "PicoDCC v1.0 ready");
    
    initialized_ = true;
    return true;
}

void PicoDCCDisplay::showDiagnosticScreen() {
    diag_screen_.show();
}

void PicoDCCDisplay::logMessage(DiagnosticSeverity severity, const char* message) {
    if (!initialized_) return;
    diag_screen_.addMessage(severity, message);
}

void PicoDCCDisplay::clearDiagnosticLog() {
    if (!initialized_) return;
    diag_screen_.clearMessages();
}
```

**Deliverable**: Diagnostic messages visible on LCD

**Testing**: Call `display.logMessage()` from main, verify messages appear

**Time Estimate**: 30 minutes

---

### Task 2.5: Add Test Messages in Main Application

**Objective**: Demonstrate diagnostic screen with sample messages

**File**: `src/pico_dcc.cpp`

**Add Test Messages**:
```cpp
int main() {
    // ... initialization ...
    
    display.init();
    display.showDiagnosticScreen();
    
    // Test messages (will be replaced with real diagnostics in Phase 3)
    display.logMessage(DIAG_INFO, "CONTROLLER: Initialization complete");
    display.logMessage(DIAG_INFO, "TRACK: Main track ready");
    display.logMessage(DIAG_INFO, "TRACK: Prog track ready");
    
    // Simulate some activity
    sleep_ms(1000);
    display.logMessage(DIAG_WARNING, "POWER: Main current 850mA");
    
    sleep_ms(1000);
    display.logMessage(DIAG_INFO, "LOCO: #3 registered");
    
    while (true) {
        controller.loop();
        display.update();
    }
}
```

**Deliverable**: LCD shows scrolling diagnostic messages on boot

**Testing**: Flash firmware, verify messages appear with correct colors

**Time Estimate**: 15 minutes

---

## Phase 2 Summary

**Completion Criteria**:
- ✅ LVGL display driver connected to ST7789T3
- ✅ Framebuffer allocated (76KB RGB565)
- ✅ Display updates integrated into main loop (10Hz)
- ✅ Diagnostic screen UI created with LVGL widgets
- ✅ Scrollable message list with color-coded severity
- ✅ "CLEAR LOG" button functional
- ✅ Test messages display on boot

**Validation**:
1. Flash firmware to Pico
2. Observe LCD showing:
   - "System Messages" title
   - Scrolling list of colored messages
   - Clear log button at bottom
3. Messages use correct colors (green=info, orange=warning, yellow=error, red=critical)
4. Scrolling works smoothly
5. DCC operations not blocked by display updates

**Time Estimate**: 4-5 hours total

**Git Commit**:
```bash
git add lib/PicoDCCDisplay/ src/pico_dcc.cpp
git commit -m "Phase 2: LVGL integration with diagnostic screen

- Connect LVGL to ST7789T3 hardware driver
- Implement 76KB framebuffer (240x320 RGB565)
- Create diagnostic screen UI with scrollable message list
- Add severity-based color coding (info/warning/error/critical)
- Implement 10Hz display refresh in main loop
- Add clear log button functionality
- Display boot messages on initialization"
```

---

## Phase 3: Diagnostic System Integration (Week 2-3)

**Goal**: Connect `pico_diagnostic.h` to LCD, display real system diagnostics

### Task 3.1: Analyze Current Diagnostic System

**Objective**: Understand how diagnostics are currently logged

**File to Review**: `lib/pico_diagnostic.h`

**Current Implementation**:
```cpp
// pico_diagnostic.h currently does nothing (silent by design)
// TODO comments indicate LCD integration was planned

void log_diagnostic(DiagnosticLevel level, const char* component, 
                    const char* message);
```

**Analysis**:
- Diagnostic system exists but outputs nothing (to avoid UART pollution)
- Used throughout codebase: `PicoDccController`, `PicoDccTrack`, etc.
- Needs to route messages to LCD instead of UART

**Deliverable**: Understanding of current diagnostic call sites

**Testing**: Grep for `log_diagnostic` calls to identify all usage

**Time Estimate**: 15 minutes

---

### Task 3.2: Create Display Callback in pico_diagnostic.h

**Objective**: Add callback mechanism for LCD to receive diagnostics

**File**: `lib/pico_diagnostic.h` (modify)

**Changes**:
```cpp
/* lib/pico_diagnostic.h */
#ifndef PICO_DIAGNOSTIC_H
#define PICO_DIAGNOSTIC_H

typedef enum {
    DIAG_INFO = 0,
    DIAG_WARNING = 1,
    DIAG_ERROR = 2,
    DIAG_CRITICAL = 3
} DiagnosticLevel;

// Callback function type for display integration
typedef void (*DiagnosticCallback)(DiagnosticLevel level, const char* component, 
                                    const char* message);

// Register display callback (called once during initialization)
void diagnostic_register_display(DiagnosticCallback callback);

// Log diagnostic message (existing function)
void log_diagnostic(DiagnosticLevel level, const char* component, 
                    const char* message);

// Convenience macros (existing)
#define LOG_CRITICAL(component, message) log_diagnostic(DIAG_CRITICAL, component, message)
#define LOG_ERROR(component, message) log_diagnostic(DIAG_ERROR, component, message)
#define LOG_WARNING(component, message) log_diagnostic(DIAG_WARNING, component, message)
#define LOG_INFO(component, message) log_diagnostic(DIAG_INFO, component, message)

#endif // PICO_DIAGNOSTIC_H
```

**File**: `lib/pico_diagnostic.cpp` (create if doesn't exist, or add to .h as inline)

**Implementation**:
```cpp
/* lib/pico_diagnostic.cpp */
#include "pico_diagnostic.h"

static DiagnosticCallback display_callback = nullptr;

void diagnostic_register_display(DiagnosticCallback callback) {
    display_callback = callback;
}

void log_diagnostic(DiagnosticLevel level, const char* component, 
                    const char* message) {
    // If display callback registered, send to LCD
    if (display_callback) {
        display_callback(level, component, message);
    }
    
    // Otherwise, do nothing (silent mode to avoid UART pollution)
}
```

**Deliverable**: Diagnostic system can route to LCD via callback

**Testing**: Register dummy callback, verify it's called

**Time Estimate**: 20 minutes

---

### Task 3.3: Register Display Callback in Main Application

**Objective**: Connect diagnostic system to PicoDCCDisplay

**File**: `src/pico_dcc.cpp`

**Add Callback Registration**:
```cpp
#include "pico_diagnostic.h"

// Global display instance (needed for callback)
PicoDCCDisplay* g_display = nullptr;

// Callback function to forward diagnostics to display
void diagnostic_display_callback(DiagnosticLevel level, const char* component, 
                                  const char* message) {
    if (!g_display) return;
    
    // Format message: "COMPONENT: message"
    char formatted[128];
    snprintf(formatted, sizeof(formatted), "%s: %s", component, message);
    
    // Convert DiagnosticLevel to DiagnosticSeverity (should be same enum values)
    g_display->logMessage(static_cast<DiagnosticSeverity>(level), formatted);
}

int main() {
    stdio_init_all();
    
    // Initialize display
    PicoDCCDisplay display;
    g_display = &display;
    
    if (!display.init()) {
        printf("ERROR: LCD initialization failed\n");
        g_display = nullptr;  // Disable display callback
    } else {
        // Register diagnostic callback
        diagnostic_register_display(diagnostic_display_callback);
        
        display.showDiagnosticScreen();
    }
    
    // Rest of initialization...
    // Now all LOG_* calls will appear on LCD!
    
    while (true) {
        controller.loop();
        display.update();
    }
}
```

**Deliverable**: All diagnostic calls automatically appear on LCD

**Testing**: Trigger error conditions, verify messages show on screen

**Time Estimate**: 25 minutes

---

### Task 3.4: Test Real Diagnostic Messages

**Objective**: Verify existing diagnostic calls work with LCD

**Test Cases**:
1. **Boot Messages**: `PicoDccController::init()` logs should appear
2. **Power Commands**: Enable/disable track power, check logs
3. **Overcurrent**: Trigger overcurrent, verify critical error shows red
4. **Loco Operations**: Add loco, send throttle commands, check info logs
5. **Invalid Commands**: Send malformed DCC-EX command, check error logs

**Expected Results**:
- Boot sequence shows green info messages
- Power state changes log in white
- Overcurrent trips show red critical messages
- Normal operations show green info
- Errors show yellow/red as appropriate

**File**: Add test scenario in `src/pico_dcc.cpp` (temporary):
```cpp
// TEST SCENARIO (remove after validation)
void test_diagnostics(PicoDCCDisplay& display) {
    LOG_INFO("SYSTEM", "Test diagnostic INFO message");
    sleep_ms(500);
    
    LOG_WARNING("POWER", "Test diagnostic WARNING message");
    sleep_ms(500);
    
    LOG_ERROR("TRACK", "Test diagnostic ERROR message");
    sleep_ms(500);
    
    LOG_CRITICAL("SAFETY", "Test diagnostic CRITICAL message");
    sleep_ms(500);
}

int main() {
    // ... initialization ...
    
    // Run diagnostic test
    test_diagnostics(display);
    
    // ... normal operation ...
}
```

**Deliverable**: All diagnostic levels display correctly with proper colors

**Testing**: Flash firmware, observe test messages, then test real scenarios

**Time Estimate**: 45 minutes

---

### Task 3.5: Add Diagnostic Filtering (Optional)

**Objective**: Allow user to filter which severity levels are shown

**Rationale**: DIAG_INFO can be very verbose during normal operation

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h`

**Add Configuration**:
```cpp
class PicoDCCDisplay {
public:
    // ... existing methods ...
    
    // Configuration
    void setMinimumSeverity(DiagnosticSeverity min_severity);
    DiagnosticSeverity getMinimumSeverity() const;
    
private:
    DiagnosticSeverity min_severity_;
};
```

**File**: `lib/PicoDCCDisplay/pico_dcc_display.cpp`

**Implementation**:
```cpp
PicoDCCDisplay::PicoDCCDisplay() 
    : min_severity_(DIAG_INFO)  // Show all by default
    , initialized_(false) {
}

void PicoDCCDisplay::setMinimumSeverity(DiagnosticSeverity min_severity) {
    min_severity_ = min_severity;
}

DiagnosticSeverity PicoDCCDisplay::getMinimumSeverity() const {
    return min_severity_;
}

void PicoDCCDisplay::logMessage(DiagnosticSeverity severity, const char* message) {
    if (!initialized_) return;
    
    // Filter by severity
    if (severity < min_severity_) {
        return;  // Message below threshold, don't display
    }
    
    diag_screen_.addMessage(severity, message);
}
```

**Usage Example**:
```cpp
// In main(), after display.init():
display.setMinimumSeverity(DIAG_WARNING);  // Only show warnings and above
```

**Deliverable**: Configurable diagnostic filtering

**Testing**: Set filter to WARNING, verify INFO messages don't appear

**Time Estimate**: 20 minutes (optional task)

---

### Task 3.6: Add Message Rate Limiting (Optional)

**Objective**: Prevent display spam from rapid repeated messages

**Problem**: High-frequency diagnostics (e.g., current monitoring) can flood display

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.h`

**Add Deduplication**:
```cpp
class DiagnosticScreen {
private:
    // ... existing members ...
    
    char last_message_[128];
    uint32_t last_message_time_ms_;
    uint16_t repeat_count_;
    
    bool isDuplicateMessage(const char* message);
    void updateRepeatCount();
};
```

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.cpp`

**Implementation**:
```cpp
void DiagnosticScreen::addMessage(DiagnosticSeverity severity, const char* message) {
    if (!msg_list_) return;
    
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    
    // Check if duplicate within 1 second
    if (isDuplicateMessage(message) && (now_ms - last_message_time_ms_ < 1000)) {
        repeat_count_++;
        updateRepeatCount();  // Update existing message with repeat count
        return;
    }
    
    // Not a duplicate, reset tracking
    repeat_count_ = 1;
    strncpy(last_message_, message, sizeof(last_message_) - 1);
    last_message_time_ms_ = now_ms;
    
    // Add message normally
    // ... (existing addMessage code) ...
}

bool DiagnosticScreen::isDuplicateMessage(const char* message) {
    return strncmp(last_message_, message, sizeof(last_message_)) == 0;
}

void DiagnosticScreen::updateRepeatCount() {
    // Update last message to show repeat count
    // E.g., "POWER: Current 850mA (x3)"
    // Implementation depends on LVGL list widget API
}
```

**Deliverable**: Repeated messages show "(x N)" counter instead of flooding list

**Testing**: Log same message 10 times rapidly, verify only one entry with counter

**Time Estimate**: 30 minutes (optional task)

---

## Phase 3 Summary

**Completion Criteria**:
- ✅ Diagnostic callback mechanism added to `pico_diagnostic.h`
- ✅ Display callback registered in `main()`
- ✅ All existing `LOG_*` calls route to LCD automatically
- ✅ Real diagnostic messages display with correct severity colors
- ✅ Boot sequence, power changes, errors all visible on screen
- ✅ (Optional) Severity filtering implemented
- ✅ (Optional) Message deduplication prevents spam

**Validation**:
1. Flash firmware and observe boot sequence on LCD
2. Send DCC-EX power commands (`<1>` / `<0>`), verify logs appear
3. Trigger overcurrent condition, verify red critical message
4. Send throttle commands, verify locomotive info messages
5. Send invalid command, verify yellow error message
6. All messages show correct timestamp and color

**Time Estimate**: 2-3 hours (core tasks), +1 hour if including optional features

**Git Commit**:
```bash
git add lib/pico_diagnostic.h lib/pico_diagnostic.cpp lib/PicoDCCDisplay/ src/pico_dcc.cpp
git commit -m "Phase 3: Diagnostic system integration with LCD

- Add callback mechanism to pico_diagnostic.h
- Register display callback in main application
- Route all LOG_* calls to LCD automatically
- Test with real diagnostic scenarios (boot, power, errors)
- Add optional severity filtering (default: show all)
- Add optional message deduplication (prevent spam)
- All diagnostics now visible on LCD in real-time"
```

---

## Phase 4: Touch Input Integration (Week 3-4)

**Goal**: Implement CST328 capacitive touch driver, add touch buttons for track power control

### Task 4.1: Implement CST328 Touch Driver

**Objective**: Create I2C driver for CST328 capacitive touch controller

**File**: `lib/PicoDCCDisplay/touch_driver.h`

```cpp
/* lib/PicoDCCDisplay/touch_driver.h */
#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifndef TEST_BUILD
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#endif

// GPIO pin definitions (from gpio-pinout-reference.md)
#define TOUCH_PIN_SDA  8   // GP8 - I2C0 Data
#define TOUCH_PIN_SCL  9   // GP9 - I2C0 Clock
#define TOUCH_PIN_INT  10  // GP10 - Touch interrupt (active low)
#define TOUCH_PIN_RST  11  // GP11 - Touch controller reset

// CST328 I2C address
#define CST328_I2C_ADDR 0x1A

// Touch point structure
struct TouchPoint {
    uint16_t x;
    uint16_t y;
    bool pressed;
    uint8_t id;  // Finger ID (0-9 for multi-touch)
};

class TouchDriver {
public:
    TouchDriver();
    ~TouchDriver();
    
    // Initialization
    bool init();
    void reset();
    
    // Touch reading
    bool readTouch(TouchPoint& point);
    bool isTouched();  // Quick check via INT pin
    
    // Configuration
    void setTouchThreshold(uint8_t threshold);
    
private:
    void initGPIO();
    void initI2C();
    bool readRegister(uint8_t reg, uint8_t* data, size_t len);
    
    bool initialized_;
};

#endif // TOUCH_DRIVER_H
```

**File**: `lib/PicoDCCDisplay/touch_driver.cpp`

```cpp
/* lib/PicoDCCDisplay/touch_driver.cpp */
#include "touch_driver.h"

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// CST328 register addresses
#define CST328_REG_TOUCH_NUM 0x02
#define CST328_REG_TOUCH_XY  0x03

TouchDriver::TouchDriver() : initialized_(false) {
}

TouchDriver::~TouchDriver() {
    if (initialized_) {
        // Cleanup I2C if needed
    }
}

bool TouchDriver::init() {
    if (initialized_) {
        return true;
    }
    
    // Initialize GPIO pins
    initGPIO();
    
    // Initialize I2C0 hardware
    initI2C();
    
    // Reset touch controller
    reset();
    
    initialized_ = true;
    return true;
}

void TouchDriver::initGPIO() {
    // INT pin (input with pull-up, active low)
    gpio_init(TOUCH_PIN_INT);
    gpio_set_dir(TOUCH_PIN_INT, GPIO_IN);
    gpio_pull_up(TOUCH_PIN_INT);
    
    // RST pin (output, active low)
    gpio_init(TOUCH_PIN_RST);
    gpio_set_dir(TOUCH_PIN_RST, GPIO_OUT);
    gpio_put(TOUCH_PIN_RST, 1);  // Not in reset initially
}

void TouchDriver::initI2C() {
    // Initialize I2C0 at 400kHz (fast mode)
    i2c_init(i2c0, 400000);
    
    // Configure I2C pins
    gpio_set_function(TOUCH_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TOUCH_PIN_SCL, GPIO_FUNC_I2C);
    
    // Enable pull-ups on I2C pins
    gpio_pull_up(TOUCH_PIN_SDA);
    gpio_pull_up(TOUCH_PIN_SCL);
}

void TouchDriver::reset() {
    gpio_put(TOUCH_PIN_RST, 0);  // Assert reset
    sleep_ms(10);
    gpio_put(TOUCH_PIN_RST, 1);  // Release reset
    sleep_ms(50);                // Wait for controller to initialize
}

bool TouchDriver::isTouched() {
    // INT pin goes low when screen is touched
    return gpio_get(TOUCH_PIN_INT) == 0;
}

bool TouchDriver::readTouch(TouchPoint& point) {
    if (!initialized_) return false;
    
    // Check if touch is present via INT pin
    if (!isTouched()) {
        point.pressed = false;
        return false;
    }
    
    // Read touch data from CST328
    uint8_t touch_data[5];  // Touch count + XY data
    if (!readRegister(CST328_REG_TOUCH_NUM, touch_data, sizeof(touch_data))) {
        point.pressed = false;
        return false;
    }
    
    uint8_t touch_count = touch_data[0];
    if (touch_count == 0) {
        point.pressed = false;
        return false;
    }
    
    // Parse first touch point (multi-touch not needed for buttons)
    point.x = ((touch_data[1] & 0x0F) << 8) | touch_data[2];
    point.y = ((touch_data[3] & 0x0F) << 8) | touch_data[4];
    point.id = (touch_data[1] >> 4) & 0x0F;
    point.pressed = true;
    
    return true;
}

bool TouchDriver::readRegister(uint8_t reg, uint8_t* data, size_t len) {
    // Write register address
    int ret = i2c_write_blocking(i2c0, CST328_I2C_ADDR, &reg, 1, true);
    if (ret < 0) return false;
    
    // Read data
    ret = i2c_read_blocking(i2c0, CST328_I2C_ADDR, data, len, false);
    return ret == (int)len;
}

void TouchDriver::setTouchThreshold(uint8_t threshold) {
    // CST328 threshold configuration (if supported by chip)
    // Implementation depends on CST328 datasheet
}

#endif // !TEST_BUILD
```

**Deliverable**: CST328 I2C driver with touch coordinate reading

**Testing**: Read touch coordinates, print to UART, verify with finger touches

**Time Estimate**: 60 minutes

---

### Task 4.2: Integrate Touch with LVGL

**Objective**: Register touch driver as LVGL input device

**File**: `lib/PicoDCCDisplay/pico_dcc_display.cpp`

**Add LVGL Touch Callback**:
```cpp
#ifndef TEST_BUILD
#include "touch_driver.h"

// Static callback for LVGL to read touch input
static void lvgl_touch_read_cb(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
    TouchDriver* touch = static_cast<TouchDriver*>(indev_drv->user_data);
    
    TouchPoint point;
    if (touch->readTouch(point)) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool PicoDCCDisplay::init() {
    // ... existing LCD and LVGL init ...
    
    // Initialize touch driver
    if (!touch_.init()) {
        LOG_ERROR("DISPLAY", "Touch initialization failed");
        // Continue without touch (display still works)
    }
    
    // Register touch input device with LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    indev_drv.user_data = &touch_;
    lv_indev_drv_register(&indev_drv);
    
    // ... rest of init ...
}
#endif
```

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h`

**Add Touch Member**:
```cpp
#include "touch_driver.h"

class PicoDCCDisplay {
private:
    LcdDriver lcd_;
    TouchDriver touch_;  // <-- ADD THIS
    // ... existing members ...
};
```

**Deliverable**: LVGL receives touch events, buttons are clickable

**Testing**: Touch "CLEAR LOG" button, verify it responds

**Time Estimate**: 30 minutes

---

### Task 4.3: Add Power Control Buttons

**Objective**: Create touch buttons for main/prog track power control

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.h`

**Add Button Members**:
```cpp
class DiagnosticScreen {
public:
    // ... existing methods ...
    
    // Button callbacks (set by PicoDCCDisplay)
    void setMainPowerCallback(void (*callback)(bool enable));
    void setProgPowerCallback(void (*callback)(bool enable));
    void setResetTripsCallback(void (*callback)());
    
private:
#ifndef TEST_BUILD
    lv_obj_t* main_pwr_btn_;
    lv_obj_t* prog_pwr_btn_;
    lv_obj_t* reset_trips_btn_;
#endif
    
    void (*main_power_callback_)(bool);
    void (*prog_power_callback_)(bool);
    void (*reset_trips_callback_)();
    
    bool main_power_state_;
    bool prog_power_state_;
};
```

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.cpp`

**Add Buttons to Screen**:
```cpp
void DiagnosticScreen::create() {
    // ... existing screen creation ...
    
    // Create power control buttons (top row)
    main_pwr_btn_ = lv_btn_create(screen_);
    lv_obj_set_size(main_pwr_btn_, 110, 30);
    lv_obj_align(main_pwr_btn_, LV_ALIGN_TOP_LEFT, 5, 290);
    
    lv_obj_t* main_label = lv_label_create(main_pwr_btn_);
    lv_label_set_text(main_label, "MAIN PWR");
    lv_obj_center(main_label);
    
    lv_obj_add_event_cb(main_pwr_btn_, [](lv_event_t* e) {
        DiagnosticScreen* screen = static_cast<DiagnosticScreen*>(lv_event_get_user_data(e));
        screen->main_power_state_ = !screen->main_power_state_;
        if (screen->main_power_callback_) {
            screen->main_power_callback_(screen->main_power_state_);
        }
    }, LV_EVENT_CLICKED, this);
    
    prog_pwr_btn_ = lv_btn_create(screen_);
    lv_obj_set_size(prog_pwr_btn_, 110, 30);
    lv_obj_align(prog_pwr_btn_, LV_ALIGN_TOP_RIGHT, -5, 290);
    
    lv_obj_t* prog_label = lv_label_create(prog_pwr_btn_);
    lv_label_set_text(prog_label, "PROG PWR");
    lv_obj_center(prog_label);
    
    lv_obj_add_event_cb(prog_pwr_btn_, [](lv_event_t* e) {
        DiagnosticScreen* screen = static_cast<DiagnosticScreen*>(lv_event_get_user_data(e));
        screen->prog_power_state_ = !screen->prog_power_state_;
        if (screen->prog_power_callback_) {
            screen->prog_power_callback_(screen->prog_power_state_);
        }
    }, LV_EVENT_CLICKED, this);
}

void DiagnosticScreen::setMainPowerCallback(void (*callback)(bool)) {
    main_power_callback_ = callback;
}

void DiagnosticScreen::setProgPowerCallback(void (*callback)(bool)) {
    prog_power_callback_ = callback;
}
```

**Deliverable**: Power buttons visible on screen, respond to touch

**Testing**: Touch buttons, verify callbacks are invoked

**Time Estimate**: 45 minutes

---

### Task 4.4: Connect Touch Buttons to DCC Controller

**Objective**: Make touch buttons actually control track power

**File**: `src/pico_dcc.cpp`

**Add Button Callbacks**:
```cpp
void main_power_button_handler(bool enable) {
    if (enable) {
        LOG_INFO("UI", "Main track power ON (via touch)");
        controller.enableMainTrack();
        
        // Echo back to DCC-EX protocol (as designed)
        printf("<p1 MAIN>\n");
    } else {
        LOG_INFO("UI", "Main track power OFF (via touch)");
        controller.disableMainTrack();
        
        printf("<p0 MAIN>\n");
    }
}

void prog_power_button_handler(bool enable) {
    if (enable) {
        LOG_INFO("UI", "Prog track power ON (via touch)");
        controller.enableProgTrack();
        
        printf("<p1 PROG>\n");
    } else {
        LOG_INFO("UI", "Prog track power OFF (via touch)");
        controller.disableProgTrack();
        
        printf("<p0 PROG>\n");
    }
}

int main() {
    // ... initialization ...
    
    // Register touch button callbacks
    display.getDiagnosticScreen().setMainPowerCallback(main_power_button_handler);
    display.getDiagnosticScreen().setProgPowerCallback(prog_power_button_handler);
    
    // ... main loop ...
}
```

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h`

**Add Accessor**:
```cpp
class PicoDCCDisplay {
public:
    // ... existing methods ...
    
    DiagnosticScreen& getDiagnosticScreen() { return diag_screen_; }
};
```

**Deliverable**: Touch buttons control real track power, send DCC-EX responses

**Testing**: Touch MAIN PWR button, verify track power toggles and UART echoes command

**Time Estimate**: 30 minutes

---

### Task 4.5: Add Visual Feedback for Button States

**Objective**: Buttons change color when power is ON/OFF

**File**: `lib/PicoDCCDisplay/ui/ui_diagnostic_screen.cpp`

**Update Button Appearance**:
```cpp
void DiagnosticScreen::updatePowerButtonState(bool is_main, bool enabled) {
    lv_obj_t* btn = is_main ? main_pwr_btn_ : prog_pwr_btn_;
    
    if (enabled) {
        // Green background when ON
        lv_obj_set_style_bg_color(btn, LV_COLOR_DCC_GREEN, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    } else {
        // Gray background when OFF
        lv_obj_set_style_bg_color(btn, lv_color_make(0x40, 0x40, 0x40), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    }
}

// Call in button event handler after toggling state
lv_obj_add_event_cb(main_pwr_btn_, [](lv_event_t* e) {
    DiagnosticScreen* screen = static_cast<DiagnosticScreen*>(lv_event_get_user_data(e));
    screen->main_power_state_ = !screen->main_power_state_;
    screen->updatePowerButtonState(true, screen->main_power_state_);
    if (screen->main_power_callback_) {
        screen->main_power_callback_(screen->main_power_state_);
    }
}, LV_EVENT_CLICKED, this);
```

**Deliverable**: Buttons visually show ON (green) / OFF (gray) state

**Testing**: Toggle power, verify button color changes

**Time Estimate**: 20 minutes

---

## Phase 4 Summary

**Completion Criteria**:
- ✅ CST328 touch driver implemented (I2C0, interrupt-driven)
- ✅ Touch input registered with LVGL
- ✅ Touch buttons respond to finger presses
- ✅ MAIN PWR and PROG PWR buttons control track power
- ✅ Touch commands echo back via DCC-EX protocol (UART)
- ✅ Button states show visual feedback (green=ON, gray=OFF)
- ✅ "CLEAR LOG" button clears diagnostic messages

**Validation**:
1. Touch screen, verify INT pin goes low
2. Touch "MAIN PWR" button:
   - Button turns green
   - Track power enables
   - UART outputs `<p1 MAIN>`
   - Diagnostic log shows "Main track power ON (via touch)"
3. Touch "PROG PWR" button, verify same behavior
4. Touch "CLEAR LOG" button, verify messages disappear
5. Touch events don't block DCC signal generation

**Time Estimate**: 3-4 hours total

**Git Commit**:
```bash
git add lib/PicoDCCDisplay/ src/pico_dcc.cpp
git commit -m "Phase 4: Touch input integration with power controls

- Implement CST328 capacitive touch driver (I2C0, 400kHz)
- Register touch as LVGL input device
- Add MAIN PWR and PROG PWR touch buttons
- Connect buttons to PicoDccController track power control
- Add visual feedback (green=ON, gray=OFF)
- Echo touch commands via DCC-EX protocol (bidirectional)
- Implement interrupt-driven touch reading (GP10 INT pin)
- Verify touch doesn't block DCC signal generation"
```

---

## Phase 5: Advanced UI Features (Week 4+)

**Goal**: Add calibration screen, multi-screen navigation, settings menu

**Note**: This phase is optional and can be implemented iteratively after Phases 1-4 are complete and tested.

### Task 5.1: Create Calibration Screen

**Objective**: Display live ADC values during calibration workflow

**File**: `lib/PicoDCCDisplay/ui/ui_calibration_screen.h`

```cpp
/* lib/PicoDCCDisplay/ui/ui_calibration_screen.h */
#ifndef UI_CALIBRATION_SCREEN_H
#define UI_CALIBRATION_SCREEN_H

#ifndef TEST_BUILD
#include "lvgl/lvgl.h"
#endif

class CalibrationScreen {
public:
    CalibrationScreen();
    ~CalibrationScreen();
    
    void create();
    void show();
    void hide();
    
    // Live data updates
    void updateMainADC(uint16_t raw_adc, float current_ma);
    void updateProgADC(uint16_t raw_adc, float current_ma);
    
    // Calibration workflow
    void setCalibrationStep(const char* instruction);
    void showSuccess(const char* message);
    void showError(const char* message);
    
private:
#ifndef TEST_BUILD
    lv_obj_t* screen_;
    lv_obj_t* title_label_;
    lv_obj_t* main_adc_label_;
    lv_obj_t* prog_adc_label_;
    lv_obj_t* instruction_label_;
    lv_obj_t* status_label_;
    lv_obj_t* back_btn_;
#endif
};

#endif // UI_CALIBRATION_SCREEN_H
```

**Implementation** (similar to diagnostic screen structure):
- Title: "ADC Calibration"
- Live ADC readings: "Main: 1234 (850mA)" / "Prog: 56 (12mA)"
- Instructions: "Step 1: Enable prog track power..."
- Back button to return to diagnostic screen

**Deliverable**: Calibration screen shows live ADC values

**Testing**: Send `<D CAL START>`, verify screen switches and shows real-time ADC

**Time Estimate**: 90 minutes

---

### Task 5.2: Implement Screen Navigation

**Objective**: Allow switching between screens via touch buttons

**File**: `lib/PicoDCCDisplay/pico_dcc_display.h`

**Add Screen Management**:
```cpp
enum class Screen {
    DIAGNOSTIC = 0,
    CALIBRATION = 1,
    SETTINGS = 2,  // Future
    LOCO_LIST = 3  // Future
};

class PicoDCCDisplay {
public:
    // ... existing methods ...
    
    // Screen navigation
    void showScreen(Screen screen);
    Screen getCurrentScreen() const;
    
private:
    Screen current_screen_;
    CalibrationScreen cal_screen_;
    // Future: SettingsScreen, LocoListScreen, etc.
};
```

**Implementation**:
```cpp
void PicoDCCDisplay::showScreen(Screen screen) {
    switch (screen) {
        case Screen::DIAGNOSTIC:
            diag_screen_.show();
            break;
        case Screen::CALIBRATION:
            cal_screen_.show();
            break;
        // Future screens...
    }
    current_screen_ = screen;
}
```

**Add Navigation Buttons** (e.g., "CALIBRATE" button on diagnostic screen):
```cpp
// In DiagnosticScreen::create()
lv_obj_t* calibrate_btn = lv_btn_create(screen_);
lv_obj_set_size(calibrate_btn, 100, 30);
lv_obj_align(calibrate_btn, LV_ALIGN_BOTTOM_LEFT, 5, -5);

lv_obj_t* calibrate_label = lv_label_create(calibrate_btn);
lv_label_set_text(calibrate_label, "CALIBRATE");
lv_obj_center(calibrate_label);

lv_obj_add_event_cb(calibrate_btn, [](lv_event_t* e) {
    // Callback to switch to calibration screen
    PicoDCCDisplay* display = static_cast<PicoDCCDisplay*>(lv_event_get_user_data(e));
    display->showScreen(Screen::CALIBRATION);
}, LV_EVENT_CLICKED, parent_display);
```

**Deliverable**: Users can navigate between screens via touch

**Testing**: Touch "CALIBRATE" button, verify screen switches, touch "BACK" to return

**Time Estimate**: 60 minutes

---

### Task 5.3: Integrate Calibration Workflow

**Objective**: Show calibration screen when `<D CAL>` commands received

**File**: `lib/PicoDCCEX/pico_dcc_ex.cpp` (or wherever calibration commands are handled)

**Add Display Integration**:
```cpp
void PicoDCCEX::handleCalibrationCommand(const char* cmd) {
    if (strstr(cmd, "CAL START")) {
        // Switch to calibration screen
        if (display_) {
            display_->showScreen(Screen::CALIBRATION);
            display_->getCalibrationScreen().setCalibrationStep(
                "Step 1: Enable prog track power, then send <D CAL ADC>"
            );
        }
    } else if (strstr(cmd, "CAL ADC")) {
        // Show live ADC values
        // (Already updating in calibration screen from main loop)
    } else if (strstr(cmd, "CAL SAVE")) {
        // Calibration complete
        if (display_) {
            display_->getCalibrationScreen().showSuccess("Calibration saved!");
            sleep_ms(2000);
            display_->showScreen(Screen::DIAGNOSTIC);  // Return to main screen
        }
    }
}
```

**Update Main Loop** to push ADC values to calibration screen:
```cpp
void PicoDccController::loop() {
    // ... existing loop code ...
    
    // If calibration screen active, update live ADC readings
    if (display_->getCurrentScreen() == Screen::CALIBRATION) {
        uint16_t main_adc = mainTrack_.getCurrentADC();
        float main_ma = mainTrack_.getCurrentMilliamps();
        display_->getCalibrationScreen().updateMainADC(main_adc, main_ma);
        
        uint16_t prog_adc = progTrack_.getCurrentADC();
        float prog_ma = progTrack_.getCurrentMilliamps();
        display_->getCalibrationScreen().updateProgADC(prog_adc, prog_ma);
    }
}
```

**Deliverable**: Calibration workflow integrated with LCD UI

**Testing**: Run full calibration sequence, verify screen guidance and ADC display

**Time Estimate**: 45 minutes

---

### Task 5.4: Add Settings Screen (Future Enhancement)

**Objective**: Allow user to configure display settings via touch

**Potential Settings**:
- Display brightness (if backlight PWM added later)
- Diagnostic severity filter (INFO/WARNING/ERROR/CRITICAL)
- Touch calibration (if needed for resistive touch)
- Screen timeout (auto-dim after inactivity)
- Current display units (mA vs A)

**Implementation Placeholder**:
```cpp
// lib/PicoDCCDisplay/ui/ui_settings_screen.h
class SettingsScreen {
public:
    void create();
    void show();
    void hide();
    
    // Settings widgets: checkboxes, sliders, dropdowns
};
```

**Deliverable**: Settings screen structure ready for future features

**Testing**: Deferred until specific settings are needed

**Time Estimate**: 2-3 hours (when implemented)

---

### Task 5.5: Add Locomotive List Screen (Future Enhancement)

**Objective**: Display active locomotives with speed/direction/functions

**Screen Layout**:
```
┌─────────────────────────┐
│ Active Locomotives      │
├─────────────────────────┤
│ #3                      │
│ [→] Speed: 28 / 28 steps│
│ F0 F1 F2 — — — — —      │
├─────────────────────────┤
│ #42                     │
│ [→] Speed: 56 / 126     │
│ F0 — — — — — — —        │
├─────────────────────────┤
│ #128                    │
│ [←] Speed: 12 / 28 steps│
│ F0 F2 — — — — — —       │
└─────────────────────────┘
```

**Implementation Placeholder**:
```cpp
// lib/PicoDCCDisplay/ui/ui_loco_screen.h
class LocoListScreen {
public:
    void create();
    void show();
    void hide();
    
    // Data updates from PicoDccLocos collection
    void updateLocoList(const std::vector<PicoDccLoco*>& locos);
};
```

**Integration**: Poll `PicoDccLocos` collection in main loop, update screen

**Deliverable**: Locomotive list screen showing real-time loco states

**Testing**: Add/remove locos, verify screen updates

**Time Estimate**: 3-4 hours (when implemented)

---

## Phase 5 Summary

**Completion Criteria**:
- ✅ Calibration screen created with live ADC display
- ✅ Multi-screen navigation implemented (touch buttons to switch)
- ✅ Calibration workflow integrated with LCD guidance
- ✅ (Optional) Settings screen structure ready
- ✅ (Optional) Locomotive list screen structure ready

**Validation**:
1. Touch "CALIBRATE" button on diagnostic screen
2. Observe calibration screen with live ADC values
3. Complete calibration workflow via DCC-EX commands
4. Verify screen auto-returns to diagnostic after save
5. (Future) Access settings screen, adjust filter levels
6. (Future) View locomotive list with real-time updates

**Time Estimate**: 4-6 hours (core calibration), +5-7 hours (optional screens)

**Git Commit**:
```bash
git add lib/PicoDCCDisplay/ lib/PicoDCCEX/ src/pico_dcc.cpp
git commit -m "Phase 5: Advanced UI with calibration screen

- Create calibration screen with live ADC display
- Implement multi-screen navigation system
- Add screen switching via touch buttons
- Integrate calibration workflow with LCD guidance
- Display real-time ADC values during calibration
- Add back button for screen navigation
- Structure ready for future settings/loco screens"
```

---

## Testing Strategy 🧪

### Unit Testing (TEST_BUILD Mode)

**Mock Implementation** (`lib/PicoDCCDisplay/mocks/`):
```cpp
/* lcd_driver_mock.cpp */
// Mock SPI operations (no-ops in test mode)
bool LcdDriver::init() { return true; }
void LcdDriver::fillScreen(uint16_t color) { /* no-op */ }

/* touch_driver_mock.cpp */
// Mock I2C operations (simulate touch events)
bool TouchDriver::readTouch(TouchPoint& point) {
    // Return simulated touch data for testing
    static bool touched = false;
    point.pressed = touched;
    point.x = 120;
    point.y = 160;
    touched = !touched;  // Toggle for testing
    return point.pressed;
}
```

**Test Cases** (`test/pico_dcc_display_tests.cpp`):
```cpp
void test_display_init(void** state) {
    PicoDCCDisplay display;
    assert_true(display.init());
}

void test_log_message(void** state) {
    PicoDCCDisplay display;
    display.init();
    display.logMessage(DIAG_INFO, "Test message");
    // Verify message added to internal buffer (if exposed)
}

void test_screen_switching(void** state) {
    PicoDCCDisplay display;
    display.init();
    display.showScreen(Screen::CALIBRATION);
    assert_int_equal(display.getCurrentScreen(), Screen::CALIBRATION);
}
```

**Run Tests**:
```bash
cmake -DTEST_BUILD=ON ..
cmake --build .
./test/pico_dcc_display_tests.exe
```

**Expected**: All display logic tests pass without hardware

---

### Integration Testing (Hardware Mode)

**Test Scenarios**:

**Scenario 1: Boot Sequence**
1. Flash firmware to Pico
2. Observe LCD during boot:
   - Color bars appear for 2 seconds (Phase 1)
   - Diagnostic screen appears (Phase 2)
   - Boot messages display ("SYSTEM: Boot complete")
3. Verify no DCC signal interruption during display init

**Scenario 2: Diagnostic Logging**
1. Send DCC-EX power commands: `<1>` (main on), `<0>` (main off)
2. Verify messages appear on LCD:
   - "POWER: Main track enabled" (green)
   - "POWER: Main track disabled" (white)
3. Trigger overcurrent (short circuit)
4. Verify red critical message: "TRACK: Overcurrent detected!"

**Scenario 3: Touch Input**
1. Touch "MAIN PWR" button
2. Verify:
   - Button turns green
   - Track power enables
   - UART outputs `<p1 MAIN>`
   - Diagnostic shows "UI: Main track power ON (via touch)"
3. Touch again to toggle off
4. Verify button turns gray and power disables

**Scenario 4: Calibration Workflow**
1. Send `<D CAL START>` via UART
2. Verify screen switches to calibration
3. Enable prog track power
4. Observe live ADC values updating (10Hz)
5. Send `<D CAL SAVE>`
6. Verify screen returns to diagnostic

**Scenario 5: Message Scrolling**
1. Log 60+ messages rapidly
2. Verify:
   - List scrolls automatically
   - Oldest messages removed (max 50)
   - No flicker or lag
   - DCC signals continue uninterrupted

**Scenario 6: Multi-Touch Stress Test**
1. Rapidly touch multiple buttons
2. Verify:
   - No crashes or freezes
   - Button states update correctly
   - No race conditions with DCC controller

---

### Performance Validation

**Metrics to Measure**:
```
Display Update Rate: Target 10Hz (100ms period)
  - Measure time between lv_task_handler() calls
  - Should be consistent (100ms ± 10ms)

Touch Response Time: Target <50ms
  - Time from finger touch to button action
  - CST328 interrupt latency + LVGL processing

DCC Signal Quality: Must not degrade
  - Measure DCC signal timing with oscilloscope
  - Verify no jitter introduced by display updates
  - Ensure Core 1 (PIO) remains unaffected by Core 0 (display)

Memory Usage:
  - Framebuffer: 76KB (expected)
  - LVGL heap: ~30KB (expected)
  - Stack usage: Monitor for overflows
  - Total RAM: <150KB (out of 264KB available)

CPU Usage:
  - Display updates should use <10% CPU time
  - Main DCC operations remain at <30% (existing)
  - Total <40% CPU usage, leaving headroom
```

**Profiling Commands**:
```cpp
// In main loop (temporary debug code)
uint32_t start_us = time_us_32();
display.update();
uint32_t elapsed_us = time_us_32() - start_us;
if (elapsed_us > 10000) {  // >10ms warning
    LOG_WARNING("PERF", "Display update took %u us", elapsed_us);
}
```

---

## Completion Checklist ✅

### Phase 1: Hardware Bring-Up
- [ ] LVGL added as git submodule
- [ ] Component structure created
- [ ] CMakeLists configured
- [ ] ST7789T3 driver implemented
- [ ] Test pattern displays
- [ ] Compiles in both modes

### Phase 2: Basic UI
- [ ] LVGL display driver connected
- [ ] Framebuffer allocated
- [ ] Diagnostic screen created
- [ ] Message scrolling works
- [ ] Clear log button functional
- [ ] 10Hz updates in main loop

### Phase 3: Diagnostic Integration
- [ ] Callback mechanism added
- [ ] Display registered
- [ ] Real diagnostics appear on LCD
- [ ] Severity colors correct
- [ ] Message filtering works (optional)
- [ ] No UART pollution

### Phase 4: Touch Input
- [ ] CST328 driver implemented
- [ ] Touch registered with LVGL
- [ ] Power buttons respond
- [ ] Track power controls work
- [ ] Visual feedback correct
- [ ] DCC-EX echo works

### Phase 5: Advanced UI (Optional)
- [ ] Calibration screen created
- [ ] Screen navigation works
- [ ] Calibration workflow integrated
- [ ] Live ADC display functional
- [ ] Settings screen structured
- [ ] Loco list screen structured

### Documentation
- [ ] Hardware connection guide
- [ ] API reference
- [ ] User guide
- [ ] Integration notes

### Testing
- [ ] All unit tests pass (TEST_BUILD)
- [ ] Integration tests pass (hardware)
- [ ] Performance metrics acceptable
- [ ] No DCC signal degradation

---

## Known Limitations & Future Work

### Current Limitations:
1. **Backlight Control**: Tied to 3.3V (always on, no dimming)
   - Future: Add PWM control on spare GPIO
2. **Single Screen**: Only diagnostic screen initially
   - Future: Add more screens as needed
3. **No Multi-Touch**: Only first touch point used
   - Fine for button UI, could add gesture support later
4. **Framebuffer RAM**: Uses 76KB of 264KB
   - Acceptable, but limits future complex graphics
5. **Display Refresh**: Fixed 10Hz
   - Could make adaptive based on activity

### Future Enhancements:
- **Current Graphs**: LVGL chart widget for real-time current monitoring
- **Loco Throttle Control**: Touch slider to control speed
- **DCC Packet Visualizer**: Show raw packets on screen
- **Network Status**: If WiFi added, show connection status
- **SD Card Logging**: Save diagnostic history to SD card
- **Themes**: Allow user to select color schemes
- **Animations**: Add smooth transitions between screens
- **Status Bar**: Show uptime, track status at a glance

---

## Troubleshooting Guide

### Display Not Working
**Symptom**: LCD stays blank/white/random colors

**Checks**:
1. Verify SPI0 pin connections (GP4-7)
2. Check DC and RST pins (GP2-3)
3. Measure SPI clock with oscilloscope (should be 62.5MHz)
4. Try slower SPI speed: `spi_init(spi0, 10000000);` // 10MHz
5. Check ST7789T3 initialization sequence matches datasheet
6. Verify power supply (3.3V stable, sufficient current)

**Debug**:
```cpp
// In lcd_driver.cpp init()
LOG_INFO("LCD", "Resetting display...");
reset();
LOG_INFO("LCD", "Sending init sequence...");
sendInitSequence();
LOG_INFO("LCD", "Filling screen red...");
fillScreen(COLOR_RED);  // Should see red screen
```

---

### Touch Not Responding
**Symptom**: Touch events not detected

**Checks**:
1. Verify I2C0 pin connections (GP8-9)
2. Check INT pin (GP10) - should be HIGH when not touched, LOW when touched
3. Measure I2C signals with logic analyzer (400kHz)
4. Verify CST328 I2C address (0x1A) with i2c_scan
5. Check pull-ups on SDA/SCL (should have 4.7kΩ)

**Debug**:
```cpp
// In main loop (temporary)
if (gpio_get(TOUCH_PIN_INT) == 0) {
    LOG_INFO("TOUCH", "INT pin LOW - screen touched!");
}

TouchPoint point;
if (touch.readTouch(point)) {
    LOG_INFO("TOUCH", "X=%u, Y=%u", point.x, point.y);
}
```

---

### Display Updates Slow/Laggy
**Symptom**: Screen doesn't update smoothly

**Checks**:
1. Verify `display.update()` called in main loop
2. Check CPU usage (add profiling)
3. Reduce LVGL animation complexity
4. Increase SPI speed if below 62.5MHz
5. Check for blocking operations in main loop

**Optimization**:
```cpp
// Reduce LVGL task frequency if needed
#define LV_DISP_DEF_REFR_PERIOD 200  // 5Hz instead of 10Hz
```

---

### DCC Signal Degradation
**Symptom**: Locomotives misbehave after LCD added

**Checks**:
1. Verify Core 1 (PIO) timing unaffected
2. Check that display updates are on Core 0 only
3. Measure DCC waveform with oscilloscope
4. Confirm no SPI DMA conflicts with PIO DMA
5. Verify no long blocking calls in display code

**Fix**:
```cpp
// Ensure display updates don't block
void PicoDCCDisplay::update() {
    // Already throttled to 10Hz internally
    // lv_task_handler() should take <10ms
}
```

---

## Resources & References

### Hardware Documentation:
- **Waveshare WAV-27579**: https://www.waveshare.com/wiki/
- **ST7789T3 Datasheet**: Search "ST7789T3 datasheet PDF"
- **CST328 Datasheet**: Search "CST328 capacitive touch controller"
- **Pico SDK GPIO**: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html

### Software Libraries:
- **LVGL Documentation**: https://docs.lvgl.io/
- **LVGL Examples**: https://github.com/lvgl/lvgl/tree/master/examples
- **Pico SPI**: https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_spi/
- **Pico I2C**: https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_i2c/

### Community Forums:
- **LVGL Forum**: https://forum.lvgl.io/
- **Raspberry Pi Pico Forum**: https://forums.raspberrypi.com/viewforum.php?f=145
- **DCC-EX Discord**: (for DCC protocol questions)

---

## Project Sign-Off

**When all phases complete, verify**:
1. ✅ All completion checklist items checked
2. ✅ All tests passing (unit + integration)
3. ✅ Documentation complete and reviewed
4. ✅ Code committed to `feature/lcd-display` branch
5. ✅ No regressions in existing DCC functionality
6. ✅ Performance metrics meet targets
7. ✅ User acceptance testing passed

**Final Git Workflow**:
```bash
# Ensure all changes committed
git status

# Merge feature branch to main
git checkout main
git merge feature/lcd-display

# Tag release
git tag -a v1.1.0-lcd -m "LCD integration complete"

# Push to remote
git push origin main
git push origin v1.1.0-lcd
```

**Celebrate!** 🎉 You now have a fully functional LCD display integrated into PicoDCC!

---

**End of Implementation Plan**

This plan provides a complete roadmap from hardware initialization through advanced UI features. Follow the phases sequentially, test thoroughly at each stage, and commit working code frequently. Good luck with your LCD integration! 🚀
