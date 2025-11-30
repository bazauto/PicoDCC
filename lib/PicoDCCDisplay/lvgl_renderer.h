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
    
    // Log viewer screen methods
    void showLogScreen() override;
    void updateLogScreen() override;
    
    // Settings and maintenance mode methods
    void showSettingsScreen() override;
    bool showMaintenanceModeEntryModal() override;
    void showMaintenanceModeScreen() override;
    void updateMaintenanceModeScreen(bool has_unsaved) override;
    bool showUnsavedChangesModal() override;
    
private:
    LcdDriver& lcd_;
    TouchDriver& touch_;
    class PicoDccController* controller_ref_;
    
    // LVGL objects for diagnostic screen
    lv_obj_t* screen_;
    lv_obj_t* title_label_;
    lv_obj_t* packets_label_;
    lv_obj_t* locos_label_;
    
    // Phase 4: Touch button objects
    lv_obj_t* btn_main_power_;      // Label updated to show power state + current
    lv_obj_t* btn_prog_power_;      // Label updated to show power state + current
    lv_obj_t* btn_calibrate_;       // Moved to settings screen
    
    // Log viewer screen objects
    lv_obj_t* log_screen_;
    lv_obj_t* log_title_label_;
    lv_obj_t* log_table_;
    lv_obj_t* btn_clear_logs_;
    lv_obj_t* btn_back_to_main_;
    lv_obj_t* btn_view_logs_;       // Button on main screen to open log viewer
    lv_obj_t* log_count_label_;     // Indicator showing log count on main screen
    
    // Settings screen objects
    lv_obj_t* settings_screen_;
    lv_obj_t* settings_title_label_;
    lv_obj_t* btn_maintenance_mode_;
    lv_obj_t* btn_back_to_main_from_settings_;
    lv_obj_t* btn_settings_;        // Button on main screen to open settings
    
    // Maintenance mode screen objects
    lv_obj_t* maintenance_screen_;
    lv_obj_t* maintenance_title_label_;
    lv_obj_t* maintenance_status_label_;
    lv_obj_t* unsaved_indicator_label_;
    lv_obj_t* btn_save_config_;
    lv_obj_t* btn_exit_maintenance_;
    lv_obj_t* maintenance_mode_indicator_;  // Indicator on main screen
    
    // Modal dialog objects (reusable)
    lv_obj_t* modal_box_;
    lv_obj_t* modal_btn_yes_;
    lv_obj_t* modal_btn_no_;
    bool modal_result_;             // Result from modal dialog
    
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
    
    // Log viewer event handlers
    static void onViewLogsClicked(lv_event_t* e);
    static void onClearLogsClicked(lv_event_t* e);
    static void onBackToMainClicked(lv_event_t* e);
    
    // Settings screen event handlers
    static void onSettingsClicked(lv_event_t* e);
    static void onMaintenanceModeClicked(lv_event_t* e);
    static void onBackToMainFromSettingsClicked(lv_event_t* e);
    
    // Maintenance mode event handlers
    static void onSaveConfigClicked(lv_event_t* e);
    static void onExitMaintenanceClicked(lv_event_t* e);
    
    // Modal dialog event handlers
    static void onModalYesClicked(lv_event_t* e);
    static void onModalNoClicked(lv_event_t* e);
    
    // Helper methods
    void initLVGL();
    void createDiagnosticScreen();
    void createTouchButtons();
    void createLogScreen();
    void createSettingsScreen();
    void createMaintenanceModeScreen();
    bool showModal(const char* title, const char* message);
    const char* severityToString(int level);
    lv_color_t severityToColor(int level);
};

#endif // LVGL_RENDERER_H
