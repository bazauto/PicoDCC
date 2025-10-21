#ifndef PICO_DCCCONTROLLER_H
#define PICO_DCCCONTROLLER_H

#include <stdio.h>
#include <vector>
#include <queue>

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/sem.h>
#include <pico/util/queue.h>
#include <pico/time.h>
#include <hardware/timer.h>
#endif
#include "../PicoDCCEX/pico_dccex.h"
#include "../PicoDCCLoco/pico_dccloco.h"
#include "../PicoDCCLoco/pico_dcclocos.h"
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "../PicoConfigStorage/pico_config_storage.h"
#include "../PicoDCCProgrammer/pico_dcc_programmer.h"

#define CMD_QUEUE_LENGTH 5

// Operation modes for the controller
enum class OperationMode {
    NORMAL,              // Standard DCC operation
    LAYOUT_MAINTENANCE   // Safe mode for flash writes (main track power OFF)
};

class PicoDccController
{
private:
    // We support 1 main and 1 prog track in the controller
    PicoDccTrack *main_track;
    PicoDccTrack *prog_track;

    PicoDccEx *pico_dccex;
    uint8_t timing_error_led_pin;

    PicoDccLocos *pico_locos;


    queue_t track_cmd_queue;  // Queue for commands from Core 0 to Core 1

    // Main command queue for repeat/interleaving logic (Core 0)
    std::queue<raw_dcc_cmd_t> main_cmd_queue;

    // Core health monitoring for safety
    volatile uint32_t core1_heartbeat;
    uint32_t last_core1_check;
    uint32_t last_core1_heartbeat_value;

    // Operation mode and configuration
    OperationMode operation_mode;
    PicoConfigStorage config_storage;
    
    // CV programming support
    PicoDccProgrammer programmer;

public:
    PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s, uint8_t timing_led_pin);

    void dccLoop();
    void dccexLoop();
    
    // Safety functions
    void emergencyPowerCutoff();
    
    // Layout Maintenance Mode management
    bool canEnterMaintenanceMode() const;
    void enterMaintenanceMode();
    void exitMaintenanceMode();
    bool isMaintenanceModeActive() const { return operation_mode == OperationMode::LAYOUT_MAINTENANCE; }
    
    // Configuration management
    PicoConfigStorage* getConfigStorage() { return &config_storage; }
    
    // Configuration command handlers
    bool handleConfigCommand(class PicoDccExPacket* packet);
    void handleACKLimitCommand(float value);
    void handleACKMinCommand(float value);
    void handleACKMaxCommand(float value);
    void handleSaveCommand();
    void handleStatusCommand();
    void handleReadAddressCommand();
    
    // Display/status accessors (used by PicoDCCDisplay and tests)
    bool isTrackPowerOn(bool isProg) { return isProg ? prog_track->getPower() : main_track->getPower(); }
    size_t getLocoCount() { return pico_locos->getLocoCount(); }
    PicoDccTrack* getTrack(bool isProg) { return isProg ? prog_track : main_track; }

#ifdef TEST_BUILD
    // Additional test accessors
    PicoDccEx* getDccEx() { return pico_dccex; }
    PicoDccLocos* getLocos() { return pico_locos; }
#endif
};

#endif