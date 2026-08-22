/* lib/PicoDCCDisplay/lvgl_renderer.cpp */
#include "lvgl_renderer.h"
#include "pico_dcc_display.h"
#include "pico/stdlib.h"
#include "../PicoDCCController/pico_dcccontroller.h"
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "../pico_diagnostic.h"
#include "../dcc_time.h"
#include "../dccex_communication.h"
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

// Theme color definitions - Use lv_color_make() with 8-bit RGB values (0-255)
// With LV_COLOR_16_SWAP=1, colors now work correctly!
// Background colors
#define THEME_BG_SCREEN              lv_color_black()

// Track button state colors  
#define THEME_TRACK_OFF              lv_color_make(80, 80, 100)    // Dark grey
#define THEME_TRACK_ON               lv_color_make(255, 165, 0)    // Orange
#define THEME_TRACK_TRIPPED          lv_color_make(200, 40, 40)    // Red

// UI button colors
#define THEME_BTN_SECONDARY          lv_color_make(80, 80, 100)    // Dark grey (Logs/Settings)
#define THEME_BTN_SECONDARY_PRESSED  lv_color_make(100, 100, 130)  // Lighter grey when pressed

// Text colors
#define THEME_TEXT_NORMAL            lv_color_white()              // Normal text
#define THEME_TEXT_STATS             lv_color_make(180, 180, 180)  // Stats text (light grey)
#define THEME_TEXT_STATS_ALERT       lv_color_make(255, 255, 100)  // Stats alert (yellow)
#define THEME_TEXT_WARNING           lv_color_make(255, 255, 0)    // Warning text (yellow)

// Modal colors
#define THEME_MODAL_BG               lv_color_make(32, 32, 32)     // Dark grey background
#define THEME_MODAL_BTN_YES          lv_color_make(0, 170, 0)      // Green
#define THEME_MODAL_BTN_NO           lv_color_make(170, 0, 0)      // Red

// Diagnostic log severity colors
#define THEME_DIAG_INFO              lv_color_white()              // White
#define THEME_DIAG_WARNING           lv_color_make(255, 255, 0)    // Yellow
#define THEME_DIAG_ERROR             lv_color_make(255, 128, 0)    // Orange
#define THEME_DIAG_CRITICAL          lv_color_make(255, 0, 0)      // Red
#define THEME_DIAG_UNKNOWN           lv_color_make(128, 128, 128)  // Grey

// Status indicator colors
#define THEME_STATUS_SUCCESS         lv_color_make(0, 200, 0)      // Green
#define THEME_STATUS_WARNING         lv_color_make(255, 165, 0)    // Orange

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
    , packets_label_(nullptr)
    , locos_label_(nullptr)
    , btn_main_power_(nullptr)
    , btn_prog_power_(nullptr)
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
    lv_obj_set_style_bg_color(screen_, THEME_BG_SCREEN, 0);
    
    // BOTTOM ROW: Stats (fixed positions, won't overlap)
    packets_label_ = lv_label_create(screen_);
    lv_label_set_text(packets_label_, "Pkt/s:0.0");
    lv_obj_set_style_text_color(packets_label_, THEME_TEXT_STATS, 0);
    lv_obj_set_style_text_font(packets_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(packets_label_, 10, 215);
    
    log_count_label_ = lv_label_create(screen_);
    lv_label_set_text(log_count_label_, "Log:0");
    lv_obj_set_style_text_color(log_count_label_, THEME_TEXT_STATS, 0);
    lv_obj_set_style_text_font(log_count_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(log_count_label_, 120, 215);
    
    locos_label_ = lv_label_create(screen_);
    lv_label_set_text(locos_label_, "Loco:0");
    lv_obj_set_style_text_color(locos_label_, THEME_TEXT_STATS, 0);
    lv_obj_set_style_text_font(locos_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(locos_label_, 230, 215);
    
    // Create interactive touch buttons
    createTouchButtons();
}

void LvglRenderer::createTouchButtons() {
    if (!screen_) return;
    
    // New layout: 2 large track buttons at top, VIEW LOGS and SETTINGS buttons at bottom
    const int track_btn_width = 145;   // Larger buttons for track control
    const int track_btn_height = 90;
    const int small_btn_width = 145;
    const int small_btn_height = 50;
    const int spacing = 10;
    const int top_margin = 10;
    const int start_x = (320 - (2 * track_btn_width + spacing)) / 2;
    
    // Button 1: MAIN TRACK - Grey by default, Orange when ON, Red when TRIPPED
    btn_main_power_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_main_power_, track_btn_width, track_btn_height);
    lv_obj_set_pos(btn_main_power_, start_x, top_margin);
    lv_obj_set_style_bg_color(btn_main_power_, THEME_TRACK_OFF, LV_STATE_DEFAULT);  // Grey (OFF)
    lv_obj_set_style_border_width(btn_main_power_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_main_power_, 12, LV_STATE_DEFAULT);
    lv_obj_t* main_label = lv_label_create(btn_main_power_);
    lv_label_set_text(main_label, "MAIN\nOFF\n0.0 mA");
    lv_obj_set_style_text_font(main_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(main_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(main_label);
    lv_obj_add_event_cb(btn_main_power_, onMainPowerClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 2: PROG TRACK - Grey by default, Orange when ON, Red when TRIPPED
    btn_prog_power_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_prog_power_, track_btn_width, track_btn_height);
    lv_obj_set_pos(btn_prog_power_, start_x + track_btn_width + spacing, top_margin);
    lv_obj_set_style_bg_color(btn_prog_power_, THEME_TRACK_OFF, LV_STATE_DEFAULT);  // Grey (OFF)
    lv_obj_set_style_border_width(btn_prog_power_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_prog_power_, 12, LV_STATE_DEFAULT);
    lv_obj_t* prog_label = lv_label_create(btn_prog_power_);
    lv_label_set_text(prog_label, "PROG\nOFF\n0.0 mA");
    lv_obj_set_style_text_font(prog_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(prog_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(prog_label);
    lv_obj_add_event_cb(btn_prog_power_, onProgPowerClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 3: VIEW LOGS - Purple/grey color
    btn_view_logs_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_view_logs_, small_btn_width, small_btn_height);
    lv_obj_set_pos(btn_view_logs_, start_x, top_margin + track_btn_height + spacing);
    lv_obj_set_style_bg_color(btn_view_logs_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_view_logs_, THEME_BTN_SECONDARY_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_view_logs_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_view_logs_, 12, LV_STATE_DEFAULT);
    lv_obj_t* logs_label = lv_label_create(btn_view_logs_);
    lv_label_set_text(logs_label, "VIEW LOGS");
    lv_obj_set_style_text_font(logs_label, &lv_font_montserrat_14, 0);
    lv_obj_center(logs_label);
    lv_obj_add_event_cb(btn_view_logs_, onViewLogsClicked, LV_EVENT_CLICKED, nullptr);
    
    // Button 4: SETTINGS - Grey color
    btn_settings_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_settings_, small_btn_width, small_btn_height);
    lv_obj_set_pos(btn_settings_, start_x + small_btn_width + spacing, top_margin + track_btn_height + spacing);
    lv_obj_set_style_bg_color(btn_settings_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_settings_, THEME_BTN_SECONDARY_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_settings_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_settings_, 12, LV_STATE_DEFAULT);
    lv_obj_t* settings_label = lv_label_create(btn_settings_);
    lv_label_set_text(settings_label, "SETTINGS");
    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_14, 0);
    lv_obj_center(settings_label);
    lv_obj_add_event_cb(btn_settings_, onSettingsClicked, LV_EVENT_CLICKED, nullptr);
}

void LvglRenderer::updateDiagnosticScreen(const TrackStatus& status) {
    if (!screen_) return;
    
    char buf[64];
    
    // Update MAIN track button - color and text based on state
    lv_obj_t* main_label = lv_obj_get_child(btn_main_power_, 0);
    if (status.main_tripped) {
        // RED - Tripped state
        lv_obj_set_style_bg_color(btn_main_power_, THEME_TRACK_TRIPPED, LV_STATE_DEFAULT);
        snprintf(buf, sizeof(buf), "MAIN\nTRIPPED\n%.1f mA", status.main_current_ma);
    } else if (status.main_power_on) {
        // ORANGE - Powered ON
        lv_obj_set_style_bg_color(btn_main_power_, THEME_TRACK_ON, LV_STATE_DEFAULT);
        snprintf(buf, sizeof(buf), "MAIN\nON\n%.1f mA", status.main_current_ma);
    } else {
        // GREY - Powered OFF
        lv_obj_set_style_bg_color(btn_main_power_, THEME_TRACK_OFF, LV_STATE_DEFAULT);
        snprintf(buf, sizeof(buf), "MAIN\nOFF\n%.1f mA", status.main_current_ma);
    }
    lv_label_set_text(main_label, buf);
    
    // Update PROG track button - color and text based on state
    lv_obj_t* prog_label = lv_obj_get_child(btn_prog_power_, 0);
    if (status.prog_tripped) {
        // RED - Tripped state
        lv_obj_set_style_bg_color(btn_prog_power_, THEME_TRACK_TRIPPED, LV_STATE_DEFAULT);
        snprintf(buf, sizeof(buf), "PROG\nTRIPPED\n%.1f mA", status.prog_current_ma);
    } else if (status.prog_power_on) {
        // ORANGE - Powered ON
        lv_obj_set_style_bg_color(btn_prog_power_, THEME_TRACK_ON, LV_STATE_DEFAULT);
        snprintf(buf, sizeof(buf), "PROG\nON\n%.1f mA", status.prog_current_ma);
    } else {
        // GREY - Powered OFF
        lv_obj_set_style_bg_color(btn_prog_power_, THEME_TRACK_OFF, LV_STATE_DEFAULT);
        snprintf(buf, sizeof(buf), "PROG\nOFF\n%.1f mA", status.prog_current_ma);
    }
    lv_label_set_text(prog_label, buf);
    
    // Update packets-per-second indicator using smoothed delta of non-idle commands
    uint32_t now_ms = dcc_millis();
    if (last_packet_sample_time_ms_ == 0) {
        last_packet_sample_time_ms_ = now_ms;
        last_packet_count_ = status.packets_sent;
        packet_rate_pps_ = 0.0f;
    } else {
        uint32_t delta_time = now_ms - last_packet_sample_time_ms_;
        if (delta_time >= 50) {  // 50ms minimum window to reduce noise
            uint32_t delta_packets;
            if (status.packets_sent >= last_packet_count_) {
                delta_packets = status.packets_sent - last_packet_count_;
            } else {
                // Counter wrapped or reset; treat as fresh sample
                delta_packets = status.packets_sent;
            }
            float instant_rate = 0.0f;
            if (delta_time > 0) {
                instant_rate = (static_cast<float>(delta_packets) * 1000.0f) / static_cast<float>(delta_time);
            }
            // Simple smoothing to avoid jitter (60% previous, 40% new sample)
            packet_rate_pps_ = (packet_rate_pps_ * 0.6f) + (instant_rate * 0.4f);
            last_packet_sample_time_ms_ = now_ms;
            last_packet_count_ = status.packets_sent;
        }
    }
    // Format rate with fractional precision for low values, clamp to 3 digits to fit label
    float clamped_rate = packet_rate_pps_;
    if (clamped_rate > 999.0f) {
        clamped_rate = 999.0f;
    }
    if (clamped_rate < 9.95f) {
        snprintf(buf, sizeof(buf), "Pkt/s:%0.1f", clamped_rate);
    } else {
        snprintf(buf, sizeof(buf), "Pkt/s:%3.0f", clamped_rate);
    }
    lv_label_set_text(packets_label_, buf);
    
    // Update log count (limit to 2 digits)
    uint32_t log_count = diag_log_get_count();
    uint32_t log_display = log_count > 99 ? 99 : log_count;
    snprintf(buf, sizeof(buf), "Log:%lu", (unsigned long)log_display);
    lv_label_set_text(log_count_label_, buf);
    
    // Change log count color based on severity
    if (log_count > 0) {
        lv_obj_set_style_text_color(log_count_label_, THEME_TEXT_STATS_ALERT, 0); // Yellow
    } else {
        lv_obj_set_style_text_color(log_count_label_, lv_color_white(), 0);
    }
    
    // Update locomotive count (limit to 2 digits)
    uint32_t loco_display = status.loco_count > 99 ? 99 : status.loco_count;
    snprintf(buf, sizeof(buf), "Loco:%u", loco_display);
    lv_label_set_text(locos_label_, buf);
    
    // Force screen invalidation to trigger redraw
    lv_obj_invalidate(screen_);
}

void LvglRenderer::tick() {
    // Tell LVGL how much time has passed
    static uint32_t last_tick_time = 0;
    uint32_t now = dcc_millis();
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
        uint32_t now = dcc_millis();
        
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
        if (main_track->isTripped()) {
            // Reset trip by turning power back on
            main_track->powerOn();
            #ifndef TEST_BUILD
            DCCEX_RESPONSE("<p1 MAIN>");
            #endif
        } else {
            // Normal power toggle
            bool current_state = main_track->getPower();
            bool new_state = !current_state;
            main_track->setPower(new_state);
            
            // Send DCC-EX power status update
            #ifndef TEST_BUILD
            if (new_state) {
                DCCEX_RESPONSE("<p1 MAIN>");
            } else {
                DCCEX_RESPONSE("<p0 MAIN>");
            }
            #endif
        }
    }
}

void LvglRenderer::onProgPowerClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    PicoDccTrack* prog_track = instance_->controller_ref_->getTrack(true);
    if (prog_track) {
        if (prog_track->isTripped()) {
            // Reset trip by turning power back on
            prog_track->powerOn();
            #ifndef TEST_BUILD
            DCCEX_RESPONSE("<p1 PROG>");
            #endif
        } else {
            // Normal power toggle
            bool current_state = prog_track->getPower();
            bool new_state = !current_state;
            prog_track->setPower(new_state);
            
            // Send DCC-EX power status update
            #ifndef TEST_BUILD
            if (new_state) {
                DCCEX_RESPONSE("<p1 PROG>");
            } else {
                DCCEX_RESPONSE("<p0 PROG>");
            }
            #endif
        }
    }
}

void LvglRenderer::createLogScreen() {
    // Create the log screen
    log_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(log_screen_, THEME_BG_SCREEN, 0);
    
    // Create title label
    log_title_label_ = lv_label_create(log_screen_);
    lv_label_set_text(log_title_label_, "Diagnostic Logs");
    lv_obj_set_style_text_color(log_title_label_, THEME_TEXT_NORMAL, 0);
    lv_obj_set_style_text_font(log_title_label_, &lv_font_montserrat_16, 0);
    lv_obj_align(log_title_label_, LV_ALIGN_TOP_MID, 0, 5);
    
    // Create scrollable textarea for log entries
    log_table_ = lv_textarea_create(log_screen_);
    lv_obj_set_size(log_table_, 310, 160);
    lv_obj_align(log_table_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(log_table_, THEME_BG_SCREEN, 0);
    lv_obj_set_style_border_color(log_table_, THEME_BTN_SECONDARY, 0);
    lv_obj_set_style_text_color(log_table_, THEME_TEXT_NORMAL, 0);
    lv_textarea_set_text(log_table_, "");
    
    // Create Back button
    btn_back_to_main_ = lv_btn_create(log_screen_);
    lv_obj_set_size(btn_back_to_main_, 80, 40);
    lv_obj_align(btn_back_to_main_, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_set_style_bg_color(btn_back_to_main_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back_to_main_, onBackToMainClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(btn_back_to_main_);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    
    // Create Clear button
    btn_clear_logs_ = lv_btn_create(log_screen_);
    lv_obj_set_size(btn_clear_logs_, 80, 40);
    lv_obj_align(btn_clear_logs_, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    lv_obj_set_style_bg_color(btn_clear_logs_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
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
        case DIAG_INFO: return THEME_DIAG_INFO;
        case DIAG_WARNING: return THEME_DIAG_WARNING;
        case DIAG_ERROR: return THEME_DIAG_ERROR;
        case DIAG_CRITICAL: return THEME_DIAG_CRITICAL;
        default: return THEME_DIAG_UNKNOWN;
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
    lv_obj_set_style_bg_color(settings_screen_, THEME_BG_SCREEN, 0);
    
    // Title
    lv_obj_t* title = lv_label_create(settings_screen_);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, THEME_TEXT_NORMAL, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Maintenance Mode button
    btn_maintenance_mode_ = lv_btn_create(settings_screen_);
    lv_obj_set_size(btn_maintenance_mode_, 220, 50);
    lv_obj_align(btn_maintenance_mode_, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(btn_maintenance_mode_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_maintenance_mode_, onMaintenanceModeClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* mm_label = lv_label_create(btn_maintenance_mode_);
    lv_label_set_text(mm_label, "Layout Maintenance\nMode");
    lv_obj_set_style_text_font(mm_label, &lv_font_montserrat_14, 0);
    lv_obj_center(mm_label);
    
    // Back button
    lv_obj_t* btn_back = lv_btn_create(settings_screen_);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_back, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, onBackToMainClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(btn_back);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
}

bool LvglRenderer::showMaintenanceModeEntryModal() {
    // Show safety checklist modal (NON-BLOCKING)
    showModal(
        "Layout Maintenance Mode",
        "SAFETY CHECKLIST:\n\n"
        "1. All locomotives stopped\n"
        "2. Track power will be OFF\n"
        "3. Remote commands disabled\n\n"
        "Enter maintenance mode?"
    );
    return true;  // Modal is displayed, result will come via button callback
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
    lv_obj_set_style_bg_color(maintenance_screen_, THEME_BG_SCREEN, 0);
    
    // Title
    lv_obj_t* title = lv_label_create(maintenance_screen_);
    lv_label_set_text(title, "Layout Maintenance");
    lv_obj_set_style_text_color(title, THEME_TEXT_WARNING, 0);  // Yellow warning
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
    lv_obj_set_style_text_color(status_label, THEME_TEXT_NORMAL, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 10, 50);
    
    // Unsaved changes indicator
    unsaved_indicator_label_ = lv_label_create(maintenance_screen_);
    lv_label_set_text(unsaved_indicator_label_, "");
    lv_obj_set_style_text_color(unsaved_indicator_label_, THEME_TEXT_WARNING, 0);
    lv_obj_set_style_text_font(unsaved_indicator_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(unsaved_indicator_label_, LV_ALIGN_CENTER, 0, 10);
    
    // Save Config button
    btn_save_config_ = lv_btn_create(maintenance_screen_);
    lv_obj_set_size(btn_save_config_, 150, 40);
    lv_obj_align(btn_save_config_, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(btn_save_config_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_save_config_, onSaveConfigClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* save_label = lv_label_create(btn_save_config_);
    lv_label_set_text(save_label, "Save to Flash");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_14, 0);
    lv_obj_center(save_label);
    
    // Exit Maintenance button
    btn_exit_maintenance_ = lv_btn_create(maintenance_screen_);
    lv_obj_set_size(btn_exit_maintenance_, 150, 40);
    lv_obj_align(btn_exit_maintenance_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_exit_maintenance_, THEME_BTN_SECONDARY, LV_STATE_DEFAULT);
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
        lv_obj_set_style_text_color(unsaved_indicator_label_, THEME_STATUS_WARNING, 0);
    } else {
        lv_label_set_text(unsaved_indicator_label_, "No unsaved changes");
        lv_obj_set_style_text_color(unsaved_indicator_label_, THEME_STATUS_SUCCESS, 0);
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
    LOG_INFO("Display", "Creating maintenance mode entry modal");
    
    // Create modal container (dark overlay) - taller to fit content
    modal_box_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal_box_, 300, 260);  // Increased height and width
    lv_obj_center(modal_box_);
    lv_obj_set_style_bg_color(modal_box_, THEME_MODAL_BG, 0);
    lv_obj_set_style_border_color(modal_box_, THEME_TEXT_NORMAL, 0);
    lv_obj_set_style_border_width(modal_box_, 2, 0);
    lv_obj_set_style_pad_all(modal_box_, 15, 0);  // Add padding
    
    // Make modal clickable and ensure it's on top
    lv_obj_clear_flag(modal_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(modal_box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(modal_box_);  // Ensure it's on top
    
    // Title
    lv_obj_t* title_label = lv_label_create(modal_box_);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, THEME_TEXT_WARNING, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);
    
    // Message - positioned lower to avoid title overlap
    lv_obj_t* msg_label = lv_label_create(modal_box_);
    lv_label_set_text(msg_label, message);
    lv_obj_set_style_text_color(msg_label, THEME_TEXT_NORMAL, 0);
    lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(msg_label, 270);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 30);  // More space from title
    
    // Yes button - positioned with more clearance from message
    modal_btn_yes_ = lv_btn_create(modal_box_);
    lv_obj_set_size(modal_btn_yes_, 110, 45);  // Slightly larger for easier touch
    lv_obj_align(modal_btn_yes_, LV_ALIGN_BOTTOM_LEFT, 15, -15);
    lv_obj_clear_flag(modal_btn_yes_, LV_OBJ_FLAG_SCROLLABLE);  // Ensure clickable
    lv_obj_add_flag(modal_btn_yes_, LV_OBJ_FLAG_CLICKABLE);  // Explicitly clickable
    lv_obj_set_style_bg_color(modal_btn_yes_, THEME_MODAL_BTN_YES, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(modal_btn_yes_, onModalYesClicked, LV_EVENT_CLICKED, NULL);
    // Also try PRESSED event as fallback
    lv_obj_add_event_cb(modal_btn_yes_, onModalYesClicked, LV_EVENT_PRESSED, NULL);
    lv_obj_t* yes_label = lv_label_create(modal_btn_yes_);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_14, 0);
    lv_obj_center(yes_label);
    
    // No button
    modal_btn_no_ = lv_btn_create(modal_box_);
    lv_obj_set_size(modal_btn_no_, 110, 45);  // Match Yes button size
    lv_obj_align(modal_btn_no_, LV_ALIGN_BOTTOM_RIGHT, -15, -15);
    lv_obj_clear_flag(modal_btn_no_, LV_OBJ_FLAG_SCROLLABLE);  // Ensure clickable
    lv_obj_add_flag(modal_btn_no_, LV_OBJ_FLAG_CLICKABLE);  // Explicitly clickable
    lv_obj_set_style_bg_color(modal_btn_no_, THEME_MODAL_BTN_NO, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(modal_btn_no_, onModalNoClicked, LV_EVENT_CLICKED, NULL);
    // Also try PRESSED event as fallback
    lv_obj_add_event_cb(modal_btn_no_, onModalNoClicked, LV_EVENT_PRESSED, NULL);
    lv_obj_t* no_label = lv_label_create(modal_btn_no_);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_font(no_label, &lv_font_montserrat_14, 0);
    lv_obj_center(no_label);
    
    // Reset result flag
    modal_result_ = false;
    
    LOG_INFO("Display", "Modal created (non-blocking)");
    
    // NON-BLOCKING: Return immediately, buttons will handle the response
    return false;  // Return value not used in non-blocking mode
}

void LvglRenderer::onSettingsClicked(lv_event_t* e) {
    if (!instance_) return;
    instance_->showSettingsScreen();
}

void LvglRenderer::onMaintenanceModeClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Check if can enter maintenance mode
    if (!instance_->controller_ref_->canEnterMaintenanceMode()) {
        LOG_ERROR("Display", "Cannot enter maintenance mode - track power must be OFF");
        return;
    }
    
    // Show non-blocking confirmation modal
    // The modal buttons will handle the actual mode entry
    instance_->showMaintenanceModeEntryModal();
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
    
    lv_event_code_t code = lv_event_get_code(e);
    
    // Log any event reaching the button
    if (code == LV_EVENT_CLICKED) {
        LOG_INFO("Display", "Modal YES clicked");
    } else if (code == LV_EVENT_PRESSED) {
        LOG_INFO("Display", "Modal YES pressed");
    }
    
    // Set result
    instance_->modal_result_ = true;
    
    // Close modal
    if (instance_->modal_box_) {
        lv_obj_del(instance_->modal_box_);
        instance_->modal_box_ = nullptr;
    }
    
    // Perform the action: Enter maintenance mode
    if (instance_->controller_ref_) {
        instance_->controller_ref_->enterMaintenanceMode();
        instance_->showMaintenanceModeScreen();
    }
}

void LvglRenderer::onModalNoClicked(lv_event_t* e) {
    if (!instance_) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    
    // Log any event reaching the button
    if (code == LV_EVENT_CLICKED) {
        LOG_INFO("Display", "Modal NO clicked");
    } else if (code == LV_EVENT_PRESSED) {
        LOG_INFO("Display", "Modal NO pressed");
    }
    
    // Set result
    instance_->modal_result_ = false;
    
    // Close modal
    if (instance_->modal_box_) {
        lv_obj_del(instance_->modal_box_);
        instance_->modal_box_ = nullptr;
    }
    
    // Stay on settings screen (no action needed)
}

