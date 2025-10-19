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
