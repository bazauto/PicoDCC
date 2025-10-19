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
        
        // Fill this band with color
        uint8_t color_bytes[2] = {
            static_cast<uint8_t>(colors[i] >> 8),
            static_cast<uint8_t>(colors[i] & 0xFF)
        };
        
#ifndef TEST_BUILD
        gpio_put(LCD_PIN_DC, 1);  // Data mode
        gpio_put(LCD_PIN_CS, 0);
        for (uint32_t j = 0; j < 240 * 40; j++) {
            spi_write_blocking(spi0, color_bytes, 2);
        }
        gpio_put(LCD_PIN_CS, 1);
#endif
    }
}

void PicoDCCDisplay::displayBootMessage() {
    if (!initialized_) return;
    
    // Black screen for now (text rendering comes in Phase 2 with LVGL)
    lcd_.fillScreen(COLOR_BLACK);
    
    // Note: "PicoDCC v1.0" text will be added once LVGL is integrated
}
