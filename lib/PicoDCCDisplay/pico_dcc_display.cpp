/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#include "pico_dcc_display.h"

// Forward declarations for controller types
class PicoDccController;
class PicoDccTrack;

// Platform-specific includes
#ifndef TEST_BUILD
    #include "pico/stdlib.h"
    #include "../PicoDCCController/pico_dcccontroller.h"
    #include "../PicoDCCTrack/pico_dcctrack.h"
#endif

// Platform abstraction for timing
#ifdef TEST_BUILD
    // Test mode: External timing control provided by test file
    extern uint32_t mock_time_ms;
    
    namespace {
        uint32_t get_time_ms() { return mock_time_ms; }
    }
#else
    // Hardware mode: Use Pico SDK timer
    namespace {
        uint32_t get_time_ms() { return time_us_32() / 1000; }
    }
#endif

PicoDCCDisplay::PicoDCCDisplay(LcdDriver& lcd, IDisplayRenderer& renderer)
    : lcd_(lcd)
    , renderer_(renderer)
    , initialized_(false)
    , last_update_time_(0)
{
}

PicoDCCDisplay::~PicoDCCDisplay() {
}

bool PicoDCCDisplay::init() {
    if (initialized_) {
        return true;
    }
    
    // Initialize LCD hardware
    if (!lcd_.init()) {
        return false;
    }
    
    // Initialize renderer (LVGL setup or mock)
    if (!renderer_.init()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void PicoDCCDisplay::runBootSequence(uint32_t delay_ms) {
    if (!initialized_) return;
    
    // Phase 1: Show color test pattern
    renderer_.showTestPattern();
    
    // Platform-specific delay between phases
#ifndef TEST_BUILD
    sleep_ms(delay_ms);
#endif
    
    // Phase 2: Show diagnostic screen
    renderer_.showDiagnosticScreen();
}

void PicoDCCDisplay::loop(PicoDccController* controller) {
    if (!initialized_ || !controller) return;
    
    // Save controller reference for button callbacks
    renderer_.setController(controller);
    
    // Update display at 10Hz
    uint32_t now = get_time_ms();
    uint32_t elapsed = now - last_update_time_;
    
    if (elapsed >= UPDATE_INTERVAL_MS) {
        // Gather track status from controller
        TrackStatus status = gatherTrackStatus(controller);
        
        // Update display via renderer
        renderer_.updateDiagnosticScreen(status);
        renderer_.tick();
        
        last_update_time_ = now;
    }
}

TrackStatus PicoDCCDisplay::gatherTrackStatus(PicoDccController* controller) {
    TrackStatus status = {};
    
    // Early return with default values if no controller (test mode or error)
    if (!controller) {
        return status;
    }
    
#ifndef TEST_BUILD
    // Hardware mode: Gather real data from controller
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
#endif
    
    return status;
}
