/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#include "pico_dcc_display.h"
#include <cstdio>

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#include "lvgl.h"
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

#ifndef TEST_BUILD
bool PicoDCCDisplay::initLVGL() {
    lv_init();
    
    // Initialize display buffer
    lv_disp_draw_buf_init(&disp_buf_, buf1_, nullptr, LV_HOR_RES_MAX * 20);
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = 240;
    disp_drv_.ver_res = 320;
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
    
    // Title label
    title_label_ = lv_label_create(screen_);
    lv_label_set_text(title_label_, "PicoDCC Status");
    lv_obj_set_style_text_color(title_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 10);
    
    // Main track power label
    main_power_label_ = lv_label_create(screen_);
    lv_label_set_text(main_power_label_, "Main: OFF");
    lv_obj_set_style_text_color(main_power_label_, lv_color_make(255, 100, 100), 0);
    lv_obj_align(main_power_label_, LV_ALIGN_TOP_LEFT, 10, 50);
    
    // Main track current label
    main_current_label_ = lv_label_create(screen_);
    lv_label_set_text(main_current_label_, "0.0 mA");
    lv_obj_set_style_text_color(main_current_label_, lv_color_white(), 0);
    lv_obj_align(main_current_label_, LV_ALIGN_TOP_LEFT, 10, 75);
    
    // Prog track power label
    prog_power_label_ = lv_label_create(screen_);
    lv_label_set_text(prog_power_label_, "Prog: OFF");
    lv_obj_set_style_text_color(prog_power_label_, lv_color_make(255, 100, 100), 0);
    lv_obj_align(prog_power_label_, LV_ALIGN_TOP_LEFT, 10, 110);
    
    // Prog track current label
    prog_current_label_ = lv_label_create(screen_);
    lv_label_set_text(prog_current_label_, "0.0 mA");
    lv_obj_set_style_text_color(prog_current_label_, lv_color_white(), 0);
    lv_obj_align(prog_current_label_, LV_ALIGN_TOP_LEFT, 10, 135);
    
    // Packets sent label
    packets_label_ = lv_label_create(screen_);
    lv_label_set_text(packets_label_, "Packets: 0");
    lv_obj_set_style_text_color(packets_label_, lv_color_white(), 0);
    lv_obj_align(packets_label_, LV_ALIGN_TOP_LEFT, 10, 170);
    
    // Locomotive count label
    locos_label_ = lv_label_create(screen_);
    lv_label_set_text(locos_label_, "Locos: 0");
    lv_obj_set_style_text_color(locos_label_, lv_color_white(), 0);
    lv_obj_align(locos_label_, LV_ALIGN_TOP_LEFT, 10, 195);
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
}

void PicoDCCDisplay::update() {
    if (!lvgl_initialized_) return;
    lv_timer_handler();
}
#endif // !TEST_BUILD

// Test pattern methods (Phase 1)
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
