/* lib/PicoDCCDisplay/lvgl_renderer.cpp */
#include "lvgl_renderer.h"
#include "pico_dcc_display.h"
#include "pico/stdlib.h"
#include "../PicoDCCController/pico_dcccontroller.h"
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "../pico_diagnostic.h"
#include <cstdio>
#include <cstdlib>  // For abs()

// RGB565 color definitions for direct LCD access
#define COLOR_BLACK   0x0000
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_WHITE   0xFFFF

// Static member initialization
lv_disp_draw_buf_t LvglRenderer::disp_buf_;
lv_color_t LvglRenderer::buf1_[LV_HOR_RES_MAX * 20];
lv_disp_drv_t LvglRenderer::disp_drv_;
lv_indev_drv_t LvglRenderer::indev_drv_;
LvglRenderer* LvglRenderer::instance_ = nullptr;

LvglRenderer::LvglRenderer(LcdDriver& lcd, TouchDriver& touch)
    : lcd_(lcd)
    , touch_(touch)
    , controller_ref_(nullptr)
    , screen_(nullptr)
    , title_label_(nullptr)
    , main_power_label_(nullptr)
    , main_current_label_(nullptr)
    , prog_power_label_(nullptr)
    , prog_current_label_(nullptr)
    , packets_label_(nullptr)
    , locos_label_(nullptr)
    , btn_main_power_(nullptr)
    , btn_prog_power_(nullptr)
    , btn_reset_trips_(nullptr)
    , btn_calibrate_(nullptr)
    , log_screen_(nullptr)
    , log_title_label_(nullptr)
    , log_table_(nullptr)
    , btn_clear_logs_(nullptr)
    , btn_back_to_main_(nullptr)
    , btn_view_logs_(nullptr)
    , log_count_label_(nullptr)
    , settings_screen_(nullptr)
    , btn_settings_(nullptr)
    , btn_maintenance_mode_(nullptr)
    , maintenance_screen_(nullptr)
    , unsaved_indicator_label_(nullptr)
    , btn_save_config_(nullptr)
    , btn_exit_maintenance_(nullptr)
    , modal_box_(nullptr)
    , modal_btn_yes_(nullptr)
    , modal_btn_no_(nullptr)
    , modal_result_(false)
{
    instance_ = this;
}

LvglRenderer::~LvglRenderer() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

bool LvglRenderer::init() {
    initLVGL();
    return true;
}

void LvglRenderer::initLVGL() {
    lv_init();
    
    // Initialize display buffer
    lv_disp_draw_buf_init(&disp_buf_, buf1_, nullptr, LV_HOR_RES_MAX * 20);
    
    // Initialize display driver (landscape: 320x240)
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = 320;
    disp_drv_.ver_res = 240;
    disp_drv_.flush_cb = flushCallback;
    disp_drv_.draw_buf = &disp_buf_;
    lv_disp_drv_register(&disp_drv_);
    
    // Initialize touch driver
    if (touch_.init()) {
        // Register touch input device with LVGL
        lv_indev_drv_init(&indev_drv_);
        indev_drv_.type = LV_INDEV_TYPE_POINTER;
        indev_drv_.read_cb = touchCallback;
        lv_indev_drv_register(&indev_drv_);
    }
}

void LvglRenderer::showTestPattern() {
    // Display vertical color bars (8 colors, each 40 pixels wide for landscape 320x240)
    const uint16_t colors[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
        COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE, COLOR_BLACK
    };
    
    for (int i = 0; i < 8; i++) {
        lcd_.setWindow(i * 40, 0, (i + 1) * 40 - 1, 239);
        
        uint8_t color_bytes[2] = {
            static_cast<uint8_t>(colors[i] >> 8),
            static_cast<uint8_t>(colors[i] & 0xFF)
        };
        
        gpio_put(LCD_PIN_DC, 1);  // Data mode
        gpio_put(LCD_PIN_CS, 0);
        for (uint32_t j = 0; j < 40 * 240; j++) {
            spi_write_blocking(spi0, color_bytes, 2);
        }
        gpio_put(LCD_PIN_CS, 1);
    }
}

void LvglRenderer::showDiagnosticScreen() {
    createDiagnosticScreen();
    lv_scr_load(screen_);
}

void LvglRenderer::createDiagnosticScreen() {
    // Create a new screen
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    
    // Title label (centered at top)
    title_label_ = lv_label_create(screen_);
    lv_label_set_text(title_label_, "PicoDCC Status");
    lv_obj_set_style_text_color(title_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 5);
    
    // LEFT COLUMN: Main Track
    main_power_label_ = lv_label_create(screen_);
    lv_label_set_text(main_power_label_, "Main: OFF");
    lv_obj_set_style_text_color(main_power_label_, lv_color_make(255, 100, 100), 0);
    lv_obj_align(main_power_label_, LV_ALIGN_TOP_LEFT, 10, 35);
    
    main_current_label_ = lv_label_create(screen_);
    lv_label_set_text(main_current_label_, "0.0 mA");
    lv_obj_set_style_text_color(main_current_label_, lv_color_white(), 0);
    lv_obj_align(main_current_label_, LV_ALIGN_TOP_LEFT, 10, 60);
    
    // MIDDLE COLUMN: Prog Track
    prog_power_label_ = lv_label_create(screen_);
    lv_label_set_text(prog_power_label_, "Prog: OFF");
    lv_obj_set_style_text_color(prog_power_label_, lv_color_make(255, 100, 100), 0);
    lv_obj_align(prog_power_label_, LV_ALIGN_TOP_MID, 0, 35);
    
    prog_current_label_ = lv_label_create(screen_);
    lv_label_set_text(prog_current_label_, "0.0 mA");
    lv_obj_set_style_text_color(prog_current_label_, lv_color_white(), 0);
    lv_obj_align(prog_current_label_, LV_ALIGN_TOP_MID, 0, 60);
    
    // BOTTOM LEFT: Packet stats
    packets_label_ = lv_label_create(screen_);
    lv_label_set_text(packets_label_, "Packets: 0");
    lv_obj_set_style_text_color(packets_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(packets_label_, &lv_font_montserrat_12, 0);
    lv_obj_align(packets_label_, LV_ALIGN_BOTTOM_LEFT, 10, -35);
    
    // BOTTOM CENTER: Log count
    log_count_label_ = lv_label_create(screen_);
    lv_label_set_text(log_count_label_, "Logs: 0");
    lv_obj_set_style_text_color(log_count_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(log_count_label_, &lv_font_montserrat_12, 0);
    lv_obj_align(log_count_label_, LV_ALIGN_BOTTOM_MID, 0, -35);
    
    // BOTTOM RIGHT: Locomotive count
    locos_label_ = lv_label_create(screen_);
    lv_label_set_text(locos_label_, "Locos: 0");
    lv_obj_set_style_text_color(locos_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(locos_label_, &lv_font_montserrat_12, 0);
    lv_obj_align(locos_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -35);
    
    // Create interactive touch buttons
    createTouchButtons();
}

void LvglRenderer::createTouchButtons() {
    if (!screen_) return;
    
    const int btn_width = 70;
    const int btn_height = 40;
    const int btn_spacing = 10;
    const int start_y = 100;
    int start_x = (320 - (4 * btn_width + 3 * btn_spacing)) / 2;
    
    // Button 1: MAIN PWR
    btn_main_power_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_main_power_, btn_width, btn_height);
    lv_obj_set_pos(btn_main_power_, start_x, start_y);
    lv_obj_t* label1 = lv_label_create(btn_main_power_);
    lv_label_set_text(label1, "MAIN\nPWR");
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_12, 0);
    lv_obj_center(label1);
    lv_obj_add_event_cb(btn_main_power_, onMainPowerClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 2: PROG PWR
    btn_prog_power_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_prog_power_, btn_width, btn_height);
    lv_obj_set_pos(btn_prog_power_, start_x + btn_width + btn_spacing, start_y);
    lv_obj_t* label2 = lv_label_create(btn_prog_power_);
    lv_label_set_text(label2, "PROG\nPWR");
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_12, 0);
    lv_obj_center(label2);
    lv_obj_add_event_cb(btn_prog_power_, onProgPowerClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 3: RESET TRIPS
    btn_reset_trips_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_reset_trips_, btn_width, btn_height);
    lv_obj_set_pos(btn_reset_trips_, start_x + 2 * (btn_width + btn_spacing), start_y);
    lv_obj_t* label3 = lv_label_create(btn_reset_trips_);
    lv_label_set_text(label3, "RESET\nTRIPS");
    lv_obj_set_style_text_font(label3, &lv_font_montserrat_12, 0);
    lv_obj_center(label3);
    lv_obj_add_event_cb(btn_reset_trips_, onResetTripsClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 4: CALIBRATE
    btn_calibrate_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_calibrate_, btn_width, btn_height);
    lv_obj_set_pos(btn_calibrate_, start_x + 3 * (btn_width + btn_spacing), start_y);
    lv_obj_t* label4 = lv_label_create(btn_calibrate_);
    lv_label_set_text(label4, "CALI-\nBRATE");
    lv_obj_set_style_text_font(label4, &lv_font_montserrat_12, 0);
    lv_obj_center(label4);
    lv_obj_add_event_cb(btn_calibrate_, onCalibrateClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 5: VIEW LOGS (bottom left)
    btn_view_logs_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_view_logs_, 80, 30);
    lv_obj_align(btn_view_logs_, LV_ALIGN_BOTTOM_LEFT, 80, -5);
    lv_obj_t* label5 = lv_label_create(btn_view_logs_);
    lv_label_set_text(label5, "Logs");
    lv_obj_set_style_text_font(label5, &lv_font_montserrat_12, 0);
    lv_obj_center(label5);
    lv_obj_add_event_cb(btn_view_logs_, onViewLogsClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 6: SETTINGS (bottom right)
    btn_settings_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_settings_, 80, 30);
    lv_obj_align(btn_settings_, LV_ALIGN_BOTTOM_RIGHT, -80, -5);
    lv_obj_t* label6 = lv_label_create(btn_settings_);
    lv_label_set_text(label6, "Settings");
    lv_obj_set_style_text_font(label6, &lv_font_montserrat_12, 0);
    lv_obj_center(label6);
    lv_obj_add_event_cb(btn_settings_, onSettingsClicked, LV_EVENT_CLICKED, nullptr);
}

void LvglRenderer::updateDiagnosticScreen(const TrackStatus& status) {
    if (!screen_) return;
    
    // Update main track power
    if (status.main_power_on) {
        lv_label_set_text(main_power_label_, "Main: ON");
        lv_obj_set_style_text_color(main_power_label_, lv_color_make(100, 255, 100), 0);
    } else {
        lv_label_set_text(main_power_label_, "Main: OFF");
        lv_obj_set_style_text_color(main_power_label_, lv_color_make(255, 100, 100), 0);
    }
    
    // Update main track current
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f mA", status.main_current_ma);
    lv_label_set_text(main_current_label_, buf);
    
    // Update prog track power
    if (status.prog_power_on) {
        lv_label_set_text(prog_power_label_, "Prog: ON");
        lv_obj_set_style_text_color(prog_power_label_, lv_color_make(100, 255, 100), 0);
    } else {
        lv_label_set_text(prog_power_label_, "Prog: OFF");
        lv_obj_set_style_text_color(prog_power_label_, lv_color_make(255, 100, 100), 0);
    }
    
    // Update prog track current
    snprintf(buf, sizeof(buf), "%.1f mA", status.prog_current_ma);
    lv_label_set_text(prog_current_label_, buf);
    
    // Update packet count
    snprintf(buf, sizeof(buf), "Packets: %lu (%lu idle)", 
             (unsigned long)status.packets_sent, 
             (unsigned long)status.idle_packets_sent);
    lv_label_set_text(packets_label_, buf);
    
    // Update log count
    uint32_t log_count = diag_log_get_count();
    snprintf(buf, sizeof(buf), "Logs: %lu", (unsigned long)log_count);
    lv_label_set_text(log_count_label_, buf);
    
    // Change log count color based on severity (optional: check for critical logs)
    if (log_count > 0) {
        lv_obj_set_style_text_color(log_count_label_, lv_color_make(255, 255, 100), 0); // Yellow
    } else {
        lv_obj_set_style_text_color(log_count_label_, lv_color_white(), 0);
    }
    
    // Update locomotive count
    snprintf(buf, sizeof(buf), "Locos: %u", status.loco_count);
    lv_label_set_text(locos_label_, buf);
    
    // Force screen invalidation to trigger redraw
    lv_obj_invalidate(screen_);
}

void LvglRenderer::tick() {
    // Tell LVGL how much time has passed
    static uint32_t last_tick_time = 0;
    uint32_t now = time_us_32() / 1000;
    if (last_tick_time == 0) {
        last_tick_time = now;
    }
    uint32_t elapsed_ms = now - last_tick_time;
    if (elapsed_ms > 0) {
        lv_tick_inc(elapsed_ms);
        last_tick_time = now;
    }
    
    // Process LVGL timers and trigger any pending redraws
    lv_timer_handler();
    lv_refr_now(nullptr);
}

void LvglRenderer::setController(PicoDccController* controller) {
    controller_ref_ = controller;
}

void LvglRenderer::flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    if (!instance_) {
        lv_disp_flush_ready(disp);
        return;
    }
    
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    
    instance_->lcd_.setWindow(area->x1, area->y1, area->x2, area->y2);
    instance_->lcd_.pushPixels(reinterpret_cast<uint16_t*>(color_p), w * h);
    
    lv_disp_flush_ready(disp);
}

void LvglRenderer::touchCallback(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (!instance_) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    if (!instance_->touch_.hasPendingTouch()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    TouchPoint points[1];
    uint8_t num_touches = instance_->touch_.readTouchPoints(points, 1);
    
    if (num_touches > 0 && points[0].valid) {
        static int last_raw_x = 0;
        static int last_raw_y = 0;
        static uint32_t last_touch_time = 0;
        uint32_t now = time_us_32() / 1000;
        
        int delta_x = abs((int)points[0].x - last_raw_x);
        int delta_y = abs((int)points[0].y - last_raw_y);
        uint32_t delta_time = now - last_touch_time;
        
        if (delta_time < 50 && delta_x < 100 && delta_y < 100) {
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
        
        data->state = LV_INDEV_STATE_PRESSED;
        
        // CST328 coordinate transformation for 270° landscape display
        int scaled_x = 320 - ((points[0].y * 320) / 300);
        int scaled_y = (points[0].x * 240) / 220;
        
        if (scaled_x < 0) scaled_x = 0;
        if (scaled_x > 319) scaled_x = 319;
        if (scaled_y < 0) scaled_y = 0;
        if (scaled_y > 239) scaled_y = 239;
        
        data->point.x = scaled_x;
        data->point.y = scaled_y;
        
        last_raw_x = points[0].x;
        last_raw_y = points[0].y;
        last_touch_time = now;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void LvglRenderer::onMainPowerClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    PicoDccTrack* main_track = instance_->controller_ref_->getTrack(false);
    if (main_track) {
        bool current_state = main_track->getPower();
        main_track->setPower(!current_state);
    }
}

void LvglRenderer::onProgPowerClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    PicoDccTrack* prog_track = instance_->controller_ref_->getTrack(true);
    if (prog_track) {
        bool current_state = prog_track->getPower();
        prog_track->setPower(!current_state);
    }
}

void LvglRenderer::onResetTripsClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    PicoDccTrack* main_track = instance_->controller_ref_->getTrack(false);
    PicoDccTrack* prog_track = instance_->controller_ref_->getTrack(true);
    
    if (main_track) {
        main_track->powerOff();
        sleep_ms(100);
        main_track->powerOn();
    }
    if (prog_track) {
        prog_track->powerOff();
        sleep_ms(100);
        prog_track->powerOn();
    }
}

void LvglRenderer::onCalibrateClicked(lv_event_t* e) {
    // TODO: Implement programming track calibration
}

void LvglRenderer::createLogScreen() {
    // Create the log screen
    log_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(log_screen_, lv_color_hex(0x000000), 0);
    
    // Create title label
    log_title_label_ = lv_label_create(log_screen_);
    lv_label_set_text(log_title_label_, "Diagnostic Logs");
    lv_obj_set_style_text_color(log_title_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(log_title_label_, &lv_font_montserrat_16, 0);
    lv_obj_align(log_title_label_, LV_ALIGN_TOP_MID, 0, 5);
    
    // Create scrollable textarea for log entries
    log_table_ = lv_textarea_create(log_screen_);
    lv_obj_set_size(log_table_, 310, 180);
    lv_obj_align(log_table_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(log_table_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(log_table_, lv_color_hex(0x404040), 0);
    lv_obj_set_style_text_color(log_table_, lv_color_hex(0xFFFFFF), 0);
    lv_textarea_set_text(log_table_, "");
    
    // Create Back button
    btn_back_to_main_ = lv_btn_create(log_screen_);
    lv_obj_set_size(btn_back_to_main_, 80, 30);
    lv_obj_align(btn_back_to_main_, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_add_event_cb(btn_back_to_main_, onBackToMainClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(btn_back_to_main_);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    
    // Create Clear button
    btn_clear_logs_ = lv_btn_create(log_screen_);
    lv_obj_set_size(btn_clear_logs_, 80, 30);
    lv_obj_align(btn_clear_logs_, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    lv_obj_add_event_cb(btn_clear_logs_, onClearLogsClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* clear_label = lv_label_create(btn_clear_logs_);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);
}

void LvglRenderer::showLogScreen() {
    if (!log_screen_) {
        createLogScreen();
    }
    
    // Populate the log table with current entries
    updateLogScreen();
    
    // Switch to log screen
    lv_scr_load(log_screen_);
}

void LvglRenderer::updateLogScreen() {
    if (!log_table_) return;
    
    // Get current log count
    uint32_t log_count = diag_log_get_count();
    
    // Safety limit to prevent buffer issues
    if (log_count > 20) {
        log_count = 20;  // Cap at 20 entries max
    }
    
    // Build formatted log text with very conservative buffer
    static char log_text[2048];  // Static to avoid stack issues
    log_text[0] = '\0';
    
    for (uint32_t i = 0; i < log_count && i < 20; i++) {
        diagnostic_msg_t entry;
        
        // Try to get entry - skip if fails
        if (!diag_log_get_entry(i, &entry)) {
            continue;
        }
        
        // CRITICAL: Force null termination immediately after copy
        entry.component[DIAG_COMPONENT_MAX_LEN - 1] = '\0';
        entry.message[DIAG_MESSAGE_MAX_LEN - 1] = '\0';
        
        // Map level to string
        const char* level_str = "INFO";
        if (entry.level == DIAG_WARNING) level_str = "WARN";
        else if (entry.level == DIAG_ERROR) level_str = "ERROR";
        else if (entry.level == DIAG_CRITICAL) level_str = "CRIT";
        
        // Calculate current length
        size_t current_len = 0;
        while (log_text[current_len] != '\0' && current_len < sizeof(log_text)) {
            current_len++;
        }
        
        // Check if we have space (need at least 128 bytes for one entry)
        if (current_len + 128 >= sizeof(log_text)) {
            break;  // Buffer almost full, stop adding entries
        }
        
        // Format directly into the output buffer at current position
        int written = snprintf(log_text + current_len, sizeof(log_text) - current_len,
                              "[%lu.%03lu] %s %s: %s\n",
                              entry.timestamp / 1000, entry.timestamp % 1000,
                              level_str, entry.component, entry.message);
        
        // Safety check for snprintf failure
        if (written < 0 || written >= (int)(sizeof(log_text) - current_len)) {
            break;  // Stop if formatting failed or would overflow
        }
    }
    
    // Ensure final null termination
    log_text[sizeof(log_text) - 1] = '\0';
    
    // Update the textarea
    lv_textarea_set_text(log_table_, log_text);
    lv_textarea_set_cursor_pos(log_table_, LV_TEXTAREA_CURSOR_LAST);
}

void LvglRenderer::onViewLogsClicked(lv_event_t* e) {
    if (!instance_) return;
    instance_->showLogScreen();
}

void LvglRenderer::onClearLogsClicked(lv_event_t* e) {
    if (!instance_) return;
    
    // Clear the diagnostic log buffer
    diag_log_clear();
    
    // Refresh the display
    instance_->updateLogScreen();
}

void LvglRenderer::onBackToMainClicked(lv_event_t* e) {
    if (!instance_ || !instance_->screen_) return;
    
    // Return to diagnostic screen
    lv_scr_load(instance_->screen_);
}

const char* LvglRenderer::severityToString(int level) {
    switch (level) {
        case DIAG_INFO: return "INFO";
        case DIAG_WARNING: return "WARN";
        case DIAG_ERROR: return "ERROR";
        case DIAG_CRITICAL: return "CRIT";
        default: return "???";
    }
}

lv_color_t LvglRenderer::severityToColor(int level) {
    switch (level) {
        case DIAG_INFO: return lv_color_hex(0xFFFFFF);    // White
        case DIAG_WARNING: return lv_color_hex(0xFFFF00); // Yellow
        case DIAG_ERROR: return lv_color_hex(0xFF8000);   // Orange
        case DIAG_CRITICAL: return lv_color_hex(0xFF0000); // Red
        default: return lv_color_hex(0x808080);            // Gray
    }
}

// ============================================================================
// SETTINGS AND MAINTENANCE MODE SCREENS
// ============================================================================

void LvglRenderer::showSettingsScreen() {
    if (!settings_screen_) {
        createSettingsScreen();
    }
    lv_scr_load(settings_screen_);
}

void LvglRenderer::createSettingsScreen() {
    // Create settings screen
    settings_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settings_screen_, lv_color_hex(0x000000), 0);
    
    // Title
    lv_obj_t* title = lv_label_create(settings_screen_);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Maintenance Mode button
    btn_maintenance_mode_ = lv_btn_create(settings_screen_);
    lv_obj_set_size(btn_maintenance_mode_, 220, 50);
    lv_obj_align(btn_maintenance_mode_, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_event_cb(btn_maintenance_mode_, onMaintenanceModeClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* mm_label = lv_label_create(btn_maintenance_mode_);
    lv_label_set_text(mm_label, "Layout Maintenance\nMode");
    lv_obj_set_style_text_font(mm_label, &lv_font_montserrat_14, 0);
    lv_obj_center(mm_label);
    
    // Back button
    lv_obj_t* btn_back = lv_btn_create(settings_screen_);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(btn_back, onBackToMainClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(btn_back);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
}

bool LvglRenderer::showMaintenanceModeEntryModal() {
    // Show safety checklist modal
    bool result = showModal(
        "Layout Maintenance Mode",
        "SAFETY CHECKLIST:\n\n"
        "1. All locomotives stopped\n"
        "2. Track power will be OFF\n"
        "3. Remote commands disabled\n\n"
        "Enter maintenance mode?"
    );
    return result;
}

void LvglRenderer::showMaintenanceModeScreen() {
    if (!maintenance_screen_) {
        createMaintenanceModeScreen();
    }
    lv_scr_load(maintenance_screen_);
}

void LvglRenderer::createMaintenanceModeScreen() {
    // Create maintenance mode screen
    maintenance_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(maintenance_screen_, lv_color_hex(0x000000), 0);
    
    // Title
    lv_obj_t* title = lv_label_create(maintenance_screen_);
    lv_label_set_text(title, "Layout Maintenance");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFF00), 0);  // Yellow warning
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Status label
    lv_obj_t* status_label = lv_label_create(maintenance_screen_);
    lv_label_set_text(status_label, 
        "Main track: OFF (locked)\n"
        "Config changes: Runtime only\n"
        "Use <D ACK> commands to adjust\n"
        "Save to flash with <E> or button"
    );
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 10, 50);
    
    // Unsaved changes indicator
    unsaved_indicator_label_ = lv_label_create(maintenance_screen_);
    lv_label_set_text(unsaved_indicator_label_, "");
    lv_obj_set_style_text_color(unsaved_indicator_label_, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(unsaved_indicator_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(unsaved_indicator_label_, LV_ALIGN_CENTER, 0, 10);
    
    // Save Config button
    btn_save_config_ = lv_btn_create(maintenance_screen_);
    lv_obj_set_size(btn_save_config_, 150, 40);
    lv_obj_align(btn_save_config_, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btn_save_config_, onSaveConfigClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* save_label = lv_label_create(btn_save_config_);
    lv_label_set_text(save_label, "Save to Flash");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_14, 0);
    lv_obj_center(save_label);
    
    // Exit Maintenance button
    btn_exit_maintenance_ = lv_btn_create(maintenance_screen_);
    lv_obj_set_size(btn_exit_maintenance_, 150, 40);
    lv_obj_align(btn_exit_maintenance_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_exit_maintenance_, onExitMaintenanceClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* exit_label = lv_label_create(btn_exit_maintenance_);
    lv_label_set_text(exit_label, "Exit Maintenance");
    lv_obj_set_style_text_font(exit_label, &lv_font_montserrat_14, 0);
    lv_obj_center(exit_label);
}

void LvglRenderer::updateMaintenanceModeScreen(bool has_unsaved_changes) {
    if (!unsaved_indicator_label_) return;
    
    if (has_unsaved_changes) {
        lv_label_set_text(unsaved_indicator_label_, "* UNSAVED CHANGES *");
        lv_obj_set_style_text_color(unsaved_indicator_label_, lv_color_hex(0xFF8000), 0);  // Orange
    } else {
        lv_label_set_text(unsaved_indicator_label_, "No unsaved changes");
        lv_obj_set_style_text_color(unsaved_indicator_label_, lv_color_hex(0x00FF00), 0);  // Green
    }
}

bool LvglRenderer::showUnsavedChangesModal() {
    bool result = showModal(
        "Unsaved Changes",
        "You have unsaved configuration\n"
        "changes in RAM.\n\n"
        "Exit without saving?"
    );
    return result;
}

bool LvglRenderer::showModal(const char* title, const char* message) {
    // Create modal container (dark overlay)
    modal_box_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal_box_, 280, 180);
    lv_obj_center(modal_box_);
    lv_obj_set_style_bg_color(modal_box_, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_color(modal_box_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(modal_box_, 2, 0);
    
    // Title
    lv_obj_t* title_label = lv_label_create(modal_box_);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);
    
    // Message
    lv_obj_t* msg_label = lv_label_create(modal_box_);
    lv_label_set_text(msg_label, message);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(msg_label, 260);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 40);
    
    // Yes button
    modal_btn_yes_ = lv_btn_create(modal_box_);
    lv_obj_set_size(modal_btn_yes_, 100, 40);
    lv_obj_align(modal_btn_yes_, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_add_event_cb(modal_btn_yes_, onModalYesClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* yes_label = lv_label_create(modal_btn_yes_);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_center(yes_label);
    
    // No button
    modal_btn_no_ = lv_btn_create(modal_box_);
    lv_obj_set_size(modal_btn_no_, 100, 40);
    lv_obj_align(modal_btn_no_, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_add_event_cb(modal_btn_no_, onModalNoClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* no_label = lv_label_create(modal_btn_no_);
    lv_label_set_text(no_label, "No");
    lv_obj_center(no_label);
    
    // Reset result flag
    modal_result_ = false;
    
    // Block until user responds (process LVGL events)
    bool waiting = true;
    while (waiting) {
        lv_timer_handler();
        lv_refr_now(nullptr);
        sleep_ms(10);
        
        // Check if modal was closed (object deleted by event handler)
        if (!modal_box_) {
            waiting = false;
        }
    }
    
    return modal_result_;
}

void LvglRenderer::onSettingsClicked(lv_event_t* e) {
    if (!instance_) return;
    instance_->showSettingsScreen();
}

void LvglRenderer::onMaintenanceModeClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Check if can enter maintenance mode
    if (!instance_->controller_ref_->canEnterMaintenanceMode()) {
        // Show error modal (reuse showModal with single button)
        // For now, just return - would need a showErrorModal() helper
        LOG_ERROR("Display", "Cannot enter maintenance mode");
        return;
    }
    
    // Show entry confirmation modal
    bool confirmed = instance_->showMaintenanceModeEntryModal();
    if (confirmed) {
        // Enter maintenance mode
        instance_->controller_ref_->enterMaintenanceMode();
        instance_->showMaintenanceModeScreen();
    } else {
        // User cancelled, return to settings
        // Already on settings screen, no action needed
    }
}

void LvglRenderer::onSaveConfigClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Save config to flash (410ms blocking operation)
    instance_->controller_ref_->getConfigStorage()->save();
    
    // Update screen to show no unsaved changes
    instance_->updateMaintenanceModeScreen(false);
    
    LOG_INFO("Display", "Configuration saved to flash");
}

void LvglRenderer::onExitMaintenanceClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Check for unsaved changes
    bool has_unsaved = instance_->controller_ref_->getConfigStorage()->hasUnsavedChanges();
    
    if (has_unsaved) {
        // Show warning modal
        bool confirmed = instance_->showUnsavedChangesModal();
        if (!confirmed) {
            // User cancelled exit
            return;
        }
        // User confirmed exit, discard changes
        instance_->controller_ref_->getConfigStorage()->discardChanges();
    }
    
    // Exit maintenance mode
    instance_->controller_ref_->exitMaintenanceMode();
    
    // Return to main diagnostic screen
    if (instance_->screen_) {
        lv_scr_load(instance_->screen_);
    }
    
    LOG_INFO("Display", "Exited maintenance mode");
}

void LvglRenderer::onModalYesClicked(lv_event_t* e) {
    if (!instance_) return;
    
    // Set result and close modal
    instance_->modal_result_ = true;
    
    if (instance_->modal_box_) {
        lv_obj_del(instance_->modal_box_);
        instance_->modal_box_ = nullptr;
    }
}

void LvglRenderer::onModalNoClicked(lv_event_t* e) {
    if (!instance_) return;
    
    // Set result and close modal
    instance_->modal_result_ = false;
    
    if (instance_->modal_box_) {
        lv_obj_del(instance_->modal_box_);
        instance_->modal_box_ = nullptr;
    }
}

