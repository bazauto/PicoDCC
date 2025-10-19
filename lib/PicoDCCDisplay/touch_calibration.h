#ifndef TOUCH_CALIBRATION_H
#define TOUCH_CALIBRATION_H

#include <stdint.h>

/**
 * Touch screen calibration utility
 * 
 * Guides user through touching calibration points on the display,
 * collects raw touch coordinates, and computes calibration parameters.
 */
class TouchCalibration {
public:
    TouchCalibration();
    
    /**
     * Start the calibration process
     * Shows instructions on LCD and guides through touch points
     */
    void start();
    
    /**
     * Process a touch point during calibration
     * Call with valid=true when touch detected, valid=false when no touch
     * Returns true if calibration is complete
     */
    bool processTouchPoint(uint16_t raw_x, uint16_t raw_y, bool valid);
    
    /**
     * Check if calibration is currently running
     */
    bool isRunning() const { return is_running_; }
    
    /**
     * Get the current calibration instruction text
     */
    const char* getCurrentInstruction() const;
    
private:
    // Calibration grid: 3×3 points across the screen
    static constexpr int GRID_COLS = 3;
    static constexpr int GRID_ROWS = 3;
    static constexpr int TOTAL_POINTS = GRID_COLS * GRID_ROWS;
    static constexpr int SAMPLES_PER_POINT = 3;
    
    // Display coordinates for calibration points (320×240 screen)
    static constexpr int DISPLAY_POINTS_X[GRID_COLS] = {40, 160, 280};
    static constexpr int DISPLAY_POINTS_Y[GRID_ROWS] = {30, 120, 210};
    
    struct CalibrationPoint {
        int display_x;
        int display_y;
        uint32_t raw_x_sum;
        uint32_t raw_y_sum;
        int sample_count;
    };
    
    CalibrationPoint points_[TOTAL_POINTS];
    int current_point_;
    bool is_running_;
    uint32_t last_touch_time_;
    uint16_t last_raw_x_;  // Track last touch coordinates
    uint16_t last_raw_y_;
    bool last_was_valid_touch_;  // True if last call had a valid touch
    
    void initializePoints();
    void computeAndOutputCalibration();
    void outputCalibrationMatrix();
};

#endif // TOUCH_CALIBRATION_H
