/* lib/PicoDCCDisplay/lvgl_renderer.h */
#ifndef LVGL_RENDERER_H
#define LVGL_RENDERER_H

#include "i_display_renderer.h"
#include "lcd_driver.h"
#include "touch_driver.h"
#include "lvgl.h"

/**
 * @brief LVGL-based implementation of the display renderer
 * 
 * This class wraps all LVGL API calls and provides the concrete implementation
 * for hardware rendering. Only compiled in hardware mode (not TEST_BUILD).
 */
class LvglRenderer : public IDisplayRenderer {
public:
    LvglRenderer(LcdDriver& lcd, TouchDriver& touch);
    ~LvglRenderer() override;
    
    bool init() override;
    void showTestPattern() override;
    void showDiagnosticScreen() override;
    void updateDiagnosticScreen(const TrackStatus& status) override;
    void tick() override;
    void setController(class PicoDccController* controller) override;
    
private:
    LcdDriver& lcd_;
    TouchDriver& touch_;
    class PicoDccController* controller_ref_;
    
    // LVGL objects for diagnostic screen
    lv_obj_t* screen_;
    lv_obj_t* title_label_;
    lv_obj_t* main_power_label_;
    lv_obj_t* main_current_label_;
    lv_obj_t* prog_power_label_;
    lv_obj_t* prog_current_label_;
    lv_obj_t* packets_label_;
    lv_obj_t* locos_label_;
    
    // Phase 4: Touch button objects
    lv_obj_t* btn_main_power_;
    lv_obj_t* btn_prog_power_;
    lv_obj_t* btn_reset_trips_;
    lv_obj_t* btn_calibrate_;
    
    // LVGL display driver buffer
    static lv_disp_draw_buf_t disp_buf_;
    static lv_color_t buf1_[LV_HOR_RES_MAX * 20];  // 20 lines buffer
    static lv_disp_drv_t disp_drv_;
    
    // LVGL input device (Phase 4: Touch)
    static lv_indev_drv_t indev_drv_;
    
    // LVGL callback functions
    static void flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p);
    static void touchCallback(lv_indev_drv_t* drv, lv_indev_data_t* data);  // Phase 4
    static LvglRenderer* instance_;  // For callback access
    
    // Phase 4: Button event handlers
    static void onMainPowerClicked(lv_event_t* e);
    static void onProgPowerClicked(lv_event_t* e);
    static void onResetTripsClicked(lv_event_t* e);
    static void onCalibrateClicked(lv_event_t* e);
    
    // Helper methods
    void initLVGL();
    void createDiagnosticScreen();
    void createTouchButtons();
};

#endif // LVGL_RENDERER_H
