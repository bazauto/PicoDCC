/* lib/PicoDCCDisplay/pico_dcc_display.h */
#ifndef PICO_DCC_DISPLAY_H
#define PICO_DCC_DISPLAY_H

#include <stdint.h>
#include <cstdint>
#include "lcd_driver.h"
#include "i_display_renderer.h"

// Track status data structure
struct TrackStatus {
    bool main_power_on;
    float main_current_ma;
    bool main_tripped;         // Main track tripped due to overcurrent
    bool prog_power_on;
    float prog_current_ma;
    bool prog_tripped;         // Prog track tripped due to overcurrent
    uint32_t packets_sent;
    uint32_t idle_packets_sent;
    uint8_t loco_count;
    bool maintenance_mode_active;  // Layout Maintenance Mode indicator
    bool has_unsaved_changes;      // Configuration unsaved changes
};

/**
 * @brief Main display controller for PicoDCC
 * 
 * Clean business logic layer with NO conditional compilation.
 * Uses dependency injection to abstract rendering implementation.
 * 
 * Architecture:
 * - Core business logic (timing, state, data gathering)
 * - Delegates rendering to IDisplayRenderer interface
 * - Hardware builds use LvglRenderer (real LVGL)
 * - Test builds use MockDisplayRenderer (stubs)
 */
class PicoDCCDisplay {
public:
    /**
     * @brief Constructor with dependency injection
     * @param lcd LCD driver instance
     * @param renderer Display renderer implementation (LVGL or mock)
     */
    PicoDCCDisplay(LcdDriver& lcd, IDisplayRenderer& renderer);
    ~PicoDCCDisplay();
    
    /**
     * @brief Initialize the display system
     * @return true if initialization succeeded
     */
    bool init();
    
    /**
     * @brief Run boot sequence (test pattern + diagnostic screen)
     * @param delay_ms Delay in milliseconds between boot phases
     */
    void runBootSequence(uint32_t delay_ms = 2000);
    
    /**
     * @brief Main loop integration - call from Core 0 main loop
     * @param controller Pointer to DCC controller for data gathering
     * 
     * Handles:
     * - 10Hz update timing
     * - Track status data gathering
     * - Display refresh via renderer
     */
    void loop(class PicoDccController* controller);
    
    // Test inspection methods (for unit tests)
    bool isInitialized() const { return initialized_; }
    uint32_t getLastUpdateTime() const { return last_update_time_; }
    
private:
    LcdDriver& lcd_;
    IDisplayRenderer& renderer_;
    bool initialized_;
    uint32_t last_update_time_;
    
    static const uint32_t UPDATE_INTERVAL_MS = 100;  // 10Hz refresh rate
    
    /**
     * @brief Gather current track status from controller
     * @param controller DCC controller to query
     * @return TrackStatus struct with current values
     */
    TrackStatus gatherTrackStatus(class PicoDccController* controller);
};

#endif // PICO_DCC_DISPLAY_H
