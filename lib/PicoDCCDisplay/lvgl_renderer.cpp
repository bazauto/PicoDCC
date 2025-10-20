/* lib/PicoDCCDisplay/lvgl_renderer.cpp */
#include "lvgl_renderer.h"
#include "pico_dcc_display.h"
#include "pico/stdlib.h"
#include "../PicoDCCController/pico_dcccontroller.h"
#include "../PicoDCCTrack/pico_dcctrack.h"
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
    lv_obj_align(packets_label_, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    
    // BOTTOM RIGHT: Locomotive count
    locos_label_ = lv_label_create(screen_);
    lv_label_set_text(locos_label_, "Locos: 0");
    lv_obj_set_style_text_color(locos_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(locos_label_, &lv_font_montserrat_12, 0);
    lv_obj_align(locos_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    
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
