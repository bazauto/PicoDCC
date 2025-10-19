#include "touch_calibration.h"

#ifndef TEST_BUILD
#include "hardware/uart.h"
#include "pico/time.h"
#include <cstdio>
#else
#include "../../test/mocks.h"
#endif

#include <cstring>
#include <cstdlib>  // For abs()

TouchCalibration::TouchCalibration() 
    : current_point_(0)
    , is_running_(false)
    , last_touch_time_(0)
    , last_raw_x_(0)
    , last_raw_y_(0)
    , last_was_valid_touch_(false)
{
    initializePoints();
}

void TouchCalibration::initializePoints() {
    int idx = 0;
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            points_[idx].display_x = DISPLAY_POINTS_X[col];
            points_[idx].display_y = DISPLAY_POINTS_Y[row];
            points_[idx].raw_x_sum = 0;
            points_[idx].raw_y_sum = 0;
            points_[idx].sample_count = 0;
            idx++;
        }
    }
}

void TouchCalibration::start() {
    is_running_ = true;
    current_point_ = 0;
    initializePoints();
    last_touch_time_ = 0;
    last_raw_x_ = 0;
    last_raw_y_ = 0;
    last_was_valid_touch_ = false;
    
#ifndef TEST_BUILD
    uart_puts(uart0, "\n");
    uart_puts(uart0, "========================================\n");
    uart_puts(uart0, "TOUCH CALIBRATION STARTED\n");
    uart_puts(uart0, "========================================\n");
    uart_puts(uart0, "Instructions:\n");
    uart_puts(uart0, "- You will see a crosshair (+) on the display\n");
    uart_puts(uart0, "- Touch the CENTER of each crosshair\n");
    uart_puts(uart0, "- Each point will be sampled 3 times\n");
    uart_puts(uart0, "- Total: 9 points × 3 samples = 27 touches\n");
    uart_puts(uart0, "- Wait for 'Next point...' before moving\n");
    uart_puts(uart0, "========================================\n\n");
    
    char msg[100];
    snprintf(msg, sizeof(msg), "Point 1/%d: Touch crosshair at (%d, %d)\n", 
             TOTAL_POINTS, points_[0].display_x, points_[0].display_y);
    uart_puts(uart0, msg);
#endif
}

bool TouchCalibration::processTouchPoint(uint16_t raw_x, uint16_t raw_y, bool valid) {
    if (!is_running_ || current_point_ >= TOTAL_POINTS) {
        return false;
    }
    
#ifndef TEST_BUILD
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
#else
    uint32_t now_ms = 0;
#endif
    
    // Simplified approach: Detect rising edge (no touch → touch)
    // Don't try to detect falling edge - just wait for next rising edge
    bool is_new_touch = valid && !last_was_valid_touch_;
    
#ifndef TEST_BUILD
    // Debug: Print occasionally to see state
    static int debug_count = 0;
    debug_count++;
    if (debug_count % 50 == 0) {
        char msg[100];
        snprintf(msg, sizeof(msg), "[CAL DEBUG %d] valid=%d, last_valid=%d, is_new=%d\n", 
                 debug_count, valid, last_was_valid_touch_, is_new_touch);
        uart_puts(uart0, msg);
    }
    
    if (is_new_touch) {
        uart_puts(uart0, "[CAL] *** NEW TOUCH DETECTED ***\n");
    }
#endif
    
    // Update state for next call (AFTER checking for new touch)
    last_was_valid_touch_ = valid;
    
    if (!valid) {
        // No touch - reset timing and return
        last_touch_time_ = 0;
        return false;
    }
    
    // Only process on new touch (not continuous hold)
    if (!is_new_touch) {
        return false;
    }
    
    // Minimal time-based debounce
#ifndef TEST_BUILD
    // Just require 100ms between samples to avoid double-taps
    if (now_ms - last_touch_time_ < 100) {
        return false;
    }
    
    CalibrationPoint& pt = points_[current_point_];
    
    // Coordinate stability check: reject touches that are too different from expected position
    if (pt.sample_count > 0) {
        // For subsequent samples, check they're close to the average so far
        int avg_x = pt.raw_x_sum / pt.sample_count;
        int avg_y = pt.raw_y_sum / pt.sample_count;
        int delta_x = abs((int)raw_x - avg_x);
        int delta_y = abs((int)raw_y - avg_y);
        
        // Reject if touch is more than 200 units away from previous average
        if (delta_x > 200 || delta_y > 200) {
            char msg[100];
            snprintf(msg, sizeof(msg), "  Rejected: Too far from previous samples (dx=%d, dy=%d)\n", 
                     delta_x, delta_y);
            uart_puts(uart0, msg);
            return false;
        }
    }
    
    last_touch_time_ = now_ms;
#else
    CalibrationPoint& pt = points_[current_point_];
#endif
    
    // Accumulate this sample
    pt.raw_x_sum += raw_x;
    pt.raw_y_sum += raw_y;
    pt.sample_count++;
    
    // Store coordinates for next comparison
    last_raw_x_ = raw_x;
    last_raw_y_ = raw_y;
    
#ifndef TEST_BUILD
    char sample_msg[100];
    snprintf(sample_msg, sizeof(sample_msg), "  Sample %d/%d: Raw(%u, %u)\n", 
             pt.sample_count, SAMPLES_PER_POINT, raw_x, raw_y);
    uart_puts(uart0, sample_msg);
#endif
    
    // Check if this point is complete
    if (pt.sample_count >= SAMPLES_PER_POINT) {
        current_point_++;
        
        if (current_point_ >= TOTAL_POINTS) {
            // Calibration complete!
            computeAndOutputCalibration();
            is_running_ = false;
            return true;
        }
        
        // Move to next point
#ifndef TEST_BUILD
        char next_msg[100];
        snprintf(next_msg, sizeof(next_msg), "\nPoint %d/%d: Touch crosshair at (%d, %d)\n", 
                 current_point_ + 1, TOTAL_POINTS, 
                 points_[current_point_].display_x, 
                 points_[current_point_].display_y);
        uart_puts(uart0, next_msg);
#endif
    }
    
    return false;
}

const char* TouchCalibration::getCurrentInstruction() const {
    if (!is_running_ || current_point_ >= TOTAL_POINTS) {
        return "Calibration not active";
    }
    
    static char instruction[100];
    const CalibrationPoint& pt = points_[current_point_];
    snprintf(instruction, sizeof(instruction), 
             "Touch + at (%d,%d)\nSample %d/%d", 
             pt.display_x, pt.display_y, 
             pt.sample_count + 1, SAMPLES_PER_POINT);
    return instruction;
}

void TouchCalibration::computeAndOutputCalibration() {
#ifndef TEST_BUILD
    uart_puts(uart0, "\n========================================\n");
    uart_puts(uart0, "CALIBRATION DATA COLLECTED\n");
    uart_puts(uart0, "========================================\n\n");
    
    // Output raw data for each point
    uart_puts(uart0, "Calibration Point Data:\n");
    uart_puts(uart0, "Format: Display(X,Y) -> Raw(X_avg, Y_avg)\n\n");
    
    for (int i = 0; i < TOTAL_POINTS; i++) {
        CalibrationPoint& pt = points_[i];
        uint16_t avg_x = pt.raw_x_sum / pt.sample_count;
        uint16_t avg_y = pt.raw_y_sum / pt.sample_count;
        
        char msg[100];
        snprintf(msg, sizeof(msg), 
                 "Point %d: Display(%3d,%3d) -> Raw(%4u, %4u)\n",
                 i + 1, pt.display_x, pt.display_y, avg_x, avg_y);
        uart_puts(uart0, msg);
    }
    
    uart_puts(uart0, "\n");
    outputCalibrationMatrix();
    
    uart_puts(uart0, "\n========================================\n");
    uart_puts(uart0, "CALIBRATION COMPLETE\n");
    uart_puts(uart0, "Copy the calibration code above into\n");
    uart_puts(uart0, "touchCallback() in pico_dcc_display.cpp\n");
    uart_puts(uart0, "========================================\n\n");
#endif
}

void TouchCalibration::outputCalibrationMatrix() {
#ifndef TEST_BUILD
    // Compute average raw coordinates for each display position
    // This gives us a mapping we can use for piecewise linear interpolation
    
    uart_puts(uart0, "Suggested Calibration Code:\n");
    uart_puts(uart0, "----------------------------------------\n");
    
    // For X-axis mapping (using middle row Y=120)
    uart_puts(uart0, "// X-axis calibration (from middle row Y=120):\n");
    CalibrationPoint& left = points_[3];   // Row 1, Col 0
    CalibrationPoint& center = points_[4]; // Row 1, Col 1  
    CalibrationPoint& right = points_[5];  // Row 1, Col 2
    
    uint16_t raw_y_left = left.raw_y_sum / left.sample_count;
    uint16_t raw_y_center = center.raw_y_sum / center.sample_count;
    uint16_t raw_y_right = right.raw_y_sum / right.sample_count;
    
    char msg[200];
    snprintf(msg, sizeof(msg),
             "// Left   (X=%3d): Raw Y = %4u\n"
             "// Center (X=%3d): Raw Y = %4u\n"
             "// Right  (X=%3d): Raw Y = %4u\n",
             left.display_x, raw_y_left,
             center.display_x, raw_y_center,
             right.display_x, raw_y_right);
    uart_puts(uart0, msg);
    
    // For Y-axis mapping (using middle column X=160)
    uart_puts(uart0, "\n// Y-axis calibration (from middle column X=160):\n");
    CalibrationPoint& top = points_[1];    // Row 0, Col 1
    CalibrationPoint& middle = points_[4]; // Row 1, Col 1
    CalibrationPoint& bottom = points_[7]; // Row 2, Col 1
    
    uint16_t raw_x_top = top.raw_x_sum / top.sample_count;
    uint16_t raw_x_middle = middle.raw_x_sum / middle.sample_count;
    uint16_t raw_x_bottom = bottom.raw_x_sum / bottom.sample_count;
    
    snprintf(msg, sizeof(msg),
             "// Top    (Y=%3d): Raw X = %4u\n"
             "// Middle (Y=%3d): Raw X = %4u\n"
             "// Bottom (Y=%3d): Raw X = %4u\n",
             top.display_y, raw_x_top,
             middle.display_y, raw_x_middle,
             bottom.display_y, raw_x_bottom);
    uart_puts(uart0, msg);
    
    // Generate piecewise linear code
    uart_puts(uart0, "\n// Piecewise linear interpolation code:\n");
    uart_puts(uart0, "int raw_y = points[0].y;\n");
    uart_puts(uart0, "int raw_x = points[0].x;\n");
    uart_puts(uart0, "int scaled_x, scaled_y;\n\n");
    
    // X-axis (uses raw Y - NOTE: raw Y DECREASES as display X increases)
    uart_puts(uart0, "// Map raw Y to display X (inverted):\n");
    snprintf(msg, sizeof(msg),
             "if (raw_y >= %u) {\n"
             "    scaled_x = %d + ((%u - raw_y) * (%d - %d)) / (%u - %u);\n"
             "} else if (raw_y >= %u) {\n"
             "    scaled_x = %d + ((%u - raw_y) * (%d - %d)) / (%u - %u);\n"
             "} else {\n"
             "    scaled_x = %d + ((%u - raw_y) * (%d - %d)) / (%u - %u);\n"
             "}\n",
             raw_y_center,
             left.display_x, raw_y_left, center.display_x, left.display_x, raw_y_left, raw_y_center,
             raw_y_right,
             center.display_x, raw_y_center, right.display_x, center.display_x, raw_y_center, raw_y_right,
             right.display_x, raw_y_center, 320, right.display_x, raw_y_center, raw_y_right);
    uart_puts(uart0, msg);
    
    // Y-axis (uses raw X)
    uart_puts(uart0, "\n// Map raw X to display Y:\n");
    snprintf(msg, sizeof(msg),
             "if (raw_x <= %u) {\n"
             "    scaled_y = %d + ((raw_x - %u) * (%d - %d)) / (%u - %u);\n"
             "} else if (raw_x <= %u) {\n"
             "    scaled_y = %d + ((raw_x - %u) * (%d - %d)) / (%u - %u);\n"
             "} else {\n"
             "    scaled_y = %d + ((raw_x - %u) * (%d - %d)) / (%u - %u);\n"
             "}\n",
             raw_x_middle,
             top.display_y, raw_x_top, middle.display_y, top.display_y, raw_x_middle, raw_x_top,
             raw_x_bottom,
             middle.display_y, raw_x_middle, bottom.display_y, middle.display_y, raw_x_bottom, raw_x_middle,
             bottom.display_y, raw_x_bottom, 240, bottom.display_y, (uint16_t)4095, raw_x_bottom);
    uart_puts(uart0, msg);
    
    uart_puts(uart0, "\n// Clamp to screen bounds:\n");
    uart_puts(uart0, "if (scaled_x < 0) scaled_x = 0;\n");
    uart_puts(uart0, "if (scaled_x > 320) scaled_x = 320;\n");
    uart_puts(uart0, "if (scaled_y < 0) scaled_y = 0;\n");
    uart_puts(uart0, "if (scaled_y > 240) scaled_y = 240;\n");
    uart_puts(uart0, "\ndata->point.x = scaled_x;\n");
    uart_puts(uart0, "data->point.y = scaled_y;\n");
    
    uart_puts(uart0, "----------------------------------------\n");
#endif
}
