/* lib/PicoDCCDisplay/pico_dcc_display.cpp */
#include "pico_dcc_display.h"
#include "../dcc_time.h"

// Forward declarations for controller types
class PicoDccController;
class PicoDccTrack;

// Platform-specific includes
#ifndef TEST_BUILD
    #include "pico/stdlib.h"
    #include "../PicoDCCController/pico_dcccontroller.h"
    #include "../PicoDCCTrack/pico_dcctrack.h"
#endif

// Platform abstraction for timing.
//
// dcc_millis() already carries the test/hardware split, so this no longer needs
// an #ifdef of its own -- and it no longer differences a counter that wraps
// every 71.6 minutes (#32).
namespace {
    uint32_t get_time_ms() { return dcc_millis(); }
}

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
    
    // Show diagnostic screen directly (test pattern removed)
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
    PicoConfigStorage* config = controller->getConfigStorage();
    
    // Get ADC-to-mA conversion factor from calibrated config
    float adc_to_ma = 0.0488f;  // Default
    if (config != nullptr) {
        adc_to_ma = config->getADCToMAConversion();
    }
    
    // Power status
    status.main_power_on = main_track->getPower();
    status.main_tripped = main_track->isTripped();
    status.prog_power_on = prog_track->getPower();
    status.prog_tripped = prog_track->isTripped();
    
    // Current readings (convert ADC counts to milliamps using calibration factor)
    status.main_current_ma = main_track->getAverageCurrent() * adc_to_ma;
    status.prog_current_ma = prog_track->getAverageCurrent() * adc_to_ma;
    
    // Packet statistics (use main track stats)
    status.packets_sent = main_track->getCommandsSent();
    status.idle_packets_sent = main_track->getIdlePacketsSent();
    
    // Locomotive count
    status.loco_count = controller->getLocoCount();
    
    // Mode and configuration status
    status.maintenance_mode_active = controller->isMaintenanceModeActive();
    status.has_unsaved_changes = controller->getConfigStorage()->hasUnsavedChanges();
#endif
    
    return status;
}
