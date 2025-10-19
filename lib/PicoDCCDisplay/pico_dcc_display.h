/* lib/PicoDCCDisplay/pico_dcc_display.h */
#ifndef PICO_DCC_DISPLAY_H
#define PICO_DCC_DISPLAY_H

#include <stdint.h>
#include <cstdint>
#include "lcd_driver.h"

#ifndef TEST_BUILD
#include "lvgl.h"
#endif

// RGB565 color definitions
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

// Track status data structure
struct TrackStatus {
    bool main_power_on;
    float main_current_ma;
    bool prog_power_on;
    float prog_current_ma;
    uint32_t packets_sent;
    uint32_t idle_packets_sent;
    uint8_t loco_count;
};

class PicoDCCDisplay {
public:
    PicoDCCDisplay();
    ~PicoDCCDisplay();
    
    // Initialization
    bool init();
    
    // Test methods (Phase 1 only)
    void displayTestPattern();
    void displayBootMessage();
    
    // Phase 2: LVGL UI methods
    void showDiagnosticScreen();
    void updateTrackStatus(const TrackStatus& status);
    void update();  // Call periodically to refresh LVGL (10Hz recommended)
    
private:
    LcdDriver lcd_;
    bool initialized_;
    bool lvgl_initialized_;
    
#ifndef TEST_BUILD
    // LVGL objects for diagnostic screen
    lv_obj_t* screen_;
    lv_obj_t* title_label_;
    lv_obj_t* main_power_label_;
    lv_obj_t* main_current_label_;
    lv_obj_t* prog_power_label_;
    lv_obj_t* prog_current_label_;
    lv_obj_t* packets_label_;
    lv_obj_t* locos_label_;
    
    // LVGL display driver buffer
    static lv_disp_draw_buf_t disp_buf_;
    static lv_color_t buf1_[LV_HOR_RES_MAX * 20];  // 20 lines buffer
    static lv_disp_drv_t disp_drv_;
    
    // LVGL callback functions
    static void flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p);
    static PicoDCCDisplay* instance_;  // For callback access
    
    bool initLVGL();
    void createDiagnosticScreen();
#endif

};

#endif // PICO_DCC_DISPLAY_H
