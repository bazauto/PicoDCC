/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#include "pico_dcc_display.h"
#include <cstdio>

#ifndef TEST_BUILD
#include "pico/stdlib.h"
#include "lvgl.h"
#include <hardware/uart.h>
#include "../PicoDCCController/pico_dcccontroller.h"
#include "../PicoDCCTrack/pico_dcctrack.h"
#endif

// Static member initialization
#ifndef TEST_BUILD
lv_disp_draw_buf_t PicoDCCDisplay::disp_buf_;
lv_color_t PicoDCCDisplay::buf1_[LV_HOR_RES_MAX * 20];
lv_disp_drv_t PicoDCCDisplay::disp_drv_;
lv_indev_drv_t PicoDCCDisplay::indev_drv_;  // Phase 4: Touch input
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
    controller_ref_ = nullptr;
    
    // Phase 4: Initialize touch button objects
    btn_main_power_ = nullptr;
    btn_prog_power_ = nullptr;
    btn_reset_trips_ = nullptr;
    btn_calibrate_ = nullptr;
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
    // Save controller reference for button callbacks (Phase 4)
    controller_ref_ = controller;
    
    // Update display at 10Hz
    uint32_t now = time_us_32() / 1000;
    uint32_t elapsed = now - last_update_time_;
    if (elapsed >= UPDATE_INTERVAL_MS) {
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
    
    // Phase 4: Initialize touch driver
    if (!touch_.init()) {
        // Touch init failed - continue without touch
        uart_puts(uart0, "WARNING: Touch initialization failed\n");
    } else {
        // Register touch input device with LVGL
        lv_indev_drv_init(&indev_drv_);
        indev_drv_.type = LV_INDEV_TYPE_POINTER;
        indev_drv_.read_cb = touchCallback;
        lv_indev_t* indev = lv_indev_drv_register(&indev_drv_);
        
        if (indev) {
            uart_puts(uart0, "Touch input device registered with LVGL\n");
            
            // Check if the read timer was created
            if (indev->driver && indev->driver->read_timer) {
                uart_puts(uart0, "Touch read timer created successfully\n");
                
                // Check timer configuration (access structure members directly)
                lv_timer_t* timer = indev->driver->read_timer;
                char buf[64];
                snprintf(buf, sizeof(buf), "Timer period: %lu ms, paused: %d\n", 
                         (unsigned long)timer->period, (int)timer->paused);
                uart_puts(uart0, buf);
            } else {
                uart_puts(uart0, "WARNING: Touch read timer NOT created!\n");
            }
        } else {
            uart_puts(uart0, "ERROR: Touch registration failed!\n");
        }
    }

    uart_puts(uart0, "LVGL initialized successfully\n");
    
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
    
    // Phase 4: Create interactive touch buttons
    createTouchButtons();
}

void PicoDCCDisplay::createTouchButtons() {
    if (!screen_) return;
    
    // Button layout: 4 buttons in a row at the middle of screen
    // Each button: 70x40 pixels, 10px spacing
    const int btn_width = 70;
    const int btn_height = 40;
    const int btn_spacing = 10;
    const int start_y = 100;  // Y position (centered vertically)
    
    // Calculate starting X to center 4 buttons (320 - (4*70 + 3*10)) / 2 = 25
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
    
    // Debug: Verify this is being called
    static uint32_t update_count = 0;
    if (++update_count % 100 == 0) {  // Print every 100 calls (~1 second)
        uart_puts(uart0, "[UPDATE] lv_timer_handler() being called\n");
    }
    
    // Process LVGL timers and trigger any pending redraws
    lv_timer_handler();
    
    // Force immediate refresh if there are invalidated areas
    lv_refr_now(nullptr);
}

// Phase 4: Touch input callback for LVGL
void PicoDCCDisplay::touchCallback(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    // Debug: Print EVERY callback invocation to see if LVGL is calling us
    static uint32_t callback_count = 0;
    if (++callback_count % 100 == 0) {  // Print every 100 calls
        uart_puts(uart0, "[CALLBACK] touchCallback() called by LVGL\n");
    }
    
    if (!instance_) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    // Check if interrupt flag is set (catches short INT pulses)
    // If no interrupt, skip I2C read to save time
    if (!instance_->touch_.hasPendingTouch()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;  // No interrupt pending - most common case
    }
    
    uart_puts(uart0, "[CALLBACK] Flag is set! Reading touch data...\n");
    
    // Read fresh touch data from CST328
    TouchPoint points[1];
    uint8_t num_touches = instance_->touch_.readTouchPoints(points, 1);
    
    // Update LVGL with current touch state
    if (num_touches > 0 && points[0].valid) {
        data->state = LV_INDEV_STATE_PRESSED;
        
        // CST328 provides 12-bit coordinates (0-4095)
        // Scale to screen resolution (320x240 landscape)
        // Assuming touch coordinates match display orientation
        data->point.x = (points[0].x * 320) / 4096;
        data->point.y = (points[0].y * 240) / 4096;
        
        // Debug: Print touch events
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
            "Touch: Raw (%u,%u) -> Scaled (%d,%d) Event=%u\n",
            (unsigned)points[0].x, (unsigned)points[0].y,
            (int)data->point.x, (int)data->point.y,
            (unsigned)points[0].event);
            uart_puts(uart0, buf);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Phase 4: Button event handlers
void PicoDCCDisplay::onMainPowerClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Toggle main track power
    PicoDccTrack* main_track = instance_->controller_ref_->getTrack(false);
    if (main_track) {
        bool current_state = main_track->getPower();
        main_track->setPower(!current_state);
    }
}

void PicoDCCDisplay::onProgPowerClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Toggle programming track power
    PicoDccTrack* prog_track = instance_->controller_ref_->getTrack(true);
    if (prog_track) {
        bool current_state = prog_track->getPower();
        prog_track->setPower(!current_state);
    }
}

void PicoDCCDisplay::onResetTripsClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // Reset trips by powering off then on both tracks
    PicoDccTrack* main_track = instance_->controller_ref_->getTrack(false);
    PicoDccTrack* prog_track = instance_->controller_ref_->getTrack(true);
    
    // Power cycle to clear any trip conditions
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

void PicoDCCDisplay::onCalibrateClicked(lv_event_t* e) {
    if (!instance_ || !instance_->controller_ref_) return;
    
    // TODO Phase 5: Show calibration screen
    // For now, just print a message
    uart_puts(uart0, "Calibration button pressed (not yet implemented)\n");
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
