/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#include "pico_dcc_display.h"
#include <cstdio>

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#include "lvgl.h"
#include "../PicoDCCController/pico_dcccontroller.h"
#include "../PicoDCCTrack/pico_dcctrack.h"
#endif

// Static member initialization
#ifndef TEST_BUILD
lv_disp_draw_buf_t PicoDCCDisplay::disp_buf_;
lv_color_t PicoDCCDisplay::buf1_[LV_HOR_RES_MAX * 20];
lv_disp_drv_t PicoDCCDisplay::disp_drv_;
PicoDCCDisplay* PicoDCCDisplay::instance_ = nullptr;
#endif

PicoDCCDisplay::PicoDCCDisplay() : initialized_(false), lvgl_initialized_(false) {
#ifndef TEST_BUILD
    instance_ = this;
    screen_ = nullptr;
    title_label_ = nullptr;
    main_power_label_ = nullptr;
    main_current_label_ = nullptr;
    prog_power_label_ = nullptr;
    prog_current_label_ = nullptr;
    packets_label_ = nullptr;
    locos_label_ = nullptr;
    last_update_time_ = 0;
#endif
}

PicoDCCDisplay::~PicoDCCDisplay() {
#ifndef TEST_BUILD
    if (instance_ == this) {
        instance_ = nullptr;
    }
#endif
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
    
#ifndef TEST_BUILD
    // Initialize LVGL
    if (!initLVGL()) {
        return false;
    }
    lvgl_initialized_ = true;
#endif
    
    return true;
}

void PicoDCCDisplay::runBootSequence() {
    if (!initialized_) return;
    
#ifndef TEST_BUILD
    // Phase 1 test: Show color test pattern for 2 seconds
    displayTestPattern();
    sleep_ms(2000);
    
    // Phase 2: Show diagnostic screen
    showDiagnosticScreen();
#endif
}

void PicoDCCDisplay::loop(PicoDccController* controller) {
    if (!initialized_ || !controller) return;
    
#ifndef TEST_BUILD
    // Update display at 10Hz
    uint32_t now = time_us_32() / 1000;
    if ((now - last_update_time_) >= UPDATE_INTERVAL_MS) {
        // Gather track status from controller
        TrackStatus status;
        PicoDccTrack* main_track = controller->getTrack(false);
        PicoDccTrack* prog_track = controller->getTrack(true);
        
        // Power status
        status.main_power_on = main_track->getPower();
        status.prog_power_on = prog_track->getPower();
        
        // Current readings (convert to milliamps)
        status.main_current_ma = main_track->getAverageCurrent() * 1000.0f;
        status.prog_current_ma = prog_track->getAverageCurrent() * 1000.0f;
        
        // Packet statistics (use main track stats)
        status.packets_sent = main_track->getCommandsSent();
        status.idle_packets_sent = main_track->getIdlePacketsSent();
        
        // Locomotive count
        status.loco_count = controller->getLocoCount();
        
        updateTrackStatus(status);
        update();
        
        last_update_time_ = now;
    }
#endif
}

#ifndef TEST_BUILD
bool PicoDCCDisplay::initLVGL() {
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
    
    return true;
}

void PicoDCCDisplay::flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    if (!instance_) {
        lv_disp_flush_ready(disp);
        return;
    }
    
    // Calculate area dimensions
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    
    // Set LCD window
    instance_->lcd_.setWindow(area->x1, area->y1, area->x2, area->y2);
    
    // Push pixels (LVGL uses native RGB565 format)
    instance_->lcd_.pushPixels(reinterpret_cast<uint16_t*>(color_p), w * h);
    
    // Inform LVGL we're done
    lv_disp_flush_ready(disp);
}

void PicoDCCDisplay::createDiagnosticScreen() {
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
    lv_obj_align(packets_label_, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    
    // BOTTOM RIGHT: Locomotive count
    locos_label_ = lv_label_create(screen_);
    lv_label_set_text(locos_label_, "Locos: 0");
    lv_obj_set_style_text_color(locos_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(locos_label_, &lv_font_montserrat_12, 0);
    lv_obj_align(locos_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

void PicoDCCDisplay::showDiagnosticScreen() {
    if (!lvgl_initialized_) return;
    
    createDiagnosticScreen();
    lv_scr_load(screen_);
}

void PicoDCCDisplay::updateTrackStatus(const TrackStatus& status) {
    if (!lvgl_initialized_ || !screen_) return;
    
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
    
    // Update locomotive count
    snprintf(buf, sizeof(buf), "Locos: %u", status.loco_count);
    lv_label_set_text(locos_label_, buf);
    
    // Force screen invalidation to trigger redraw
    lv_obj_invalidate(screen_);
}

void PicoDCCDisplay::update() {
    if (!lvgl_initialized_) return;
    
    // Process LVGL timers and trigger any pending redraws
    lv_timer_handler();
    
    // Force immediate refresh if there are invalidated areas
    lv_refr_now(nullptr);
}
#endif // !TEST_BUILD

// Test pattern methods (Phase 1)
void PicoDCCDisplay::displayTestPattern() {
    if (!initialized_) return;
    
    // Display vertical color bars (8 colors, each 40 pixels wide for landscape 320x240)
    const uint16_t colors[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
        COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE, COLOR_BLACK
    };
    
    for (int i = 0; i < 8; i++) {
        // Landscape: 320x240, so 8 bars @ 40 pixels wide = 320 pixels
        lcd_.setWindow(i * 40, 0, (i + 1) * 40 - 1, 239);
        
        // Fill this band with color
        uint8_t color_bytes[2] = {
            static_cast<uint8_t>(colors[i] >> 8),
            static_cast<uint8_t>(colors[i] & 0xFF)
        };
        
#ifndef TEST_BUILD
        gpio_put(LCD_PIN_DC, 1);  // Data mode
        gpio_put(LCD_PIN_CS, 0);
        for (uint32_t j = 0; j < 40 * 240; j++) {  // 40 wide x 240 tall
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
