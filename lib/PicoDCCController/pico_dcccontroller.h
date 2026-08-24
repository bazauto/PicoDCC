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

#define CMD_QUEUE_LENGTH 5

// How long Core 1 is allowed to take to produce its first heartbeat, and to send
// its first command, before either counts as a real failure rather than startup.
// Long enough to cover launch and PIO bring-up; short enough that a Core 1 which
// never starts is still caught well before anyone could put a train on the track.
#define CORE1_STARTUP_GRACE_MS 500

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

    // Startup state for the monitors above. The constructor runs long before the
    // first loop pass -- LCD init and the boot sequence sit in between, and Core 1
    // is not launched until after them -- so baselines taken at construction are
    // already stale by the time they are first compared.
    bool monitors_armed;          // Baseline taken on the first Core 0 pass
    uint32_t monitors_armed_ms;   // When that happened, for the startup deadline
    bool core1_seen_alive;        // Core 1 has ticked at least once
    bool core1_loop_started;      // Core 1's loop has run; separate flag because
                                  // t=0 is a legal timestamp and cannot be a sentinel
    uint32_t core1_start_ms;      // When Core 1's loop first ran

    // Power-fault reporting (#4) and indication (#42).
    //
    // A fault cutoff has to survive the pass that caused it. Cutting power stops
    // commands being sent, so on the very next pass the command gap is small
    // again and isPIOHealthy() recovers -- which used to run the `else` branch
    // and turn the error LED back off, leaving a dead layout looking healthy
    // (#42). The LED, and the report, follow this LATCH rather than the
    // instantaneous condition.
    //
    // Announcing is split from latching because of which core is which.
    // dccLoop() is Core 1, the DCC hot path; a blocking uart_puts() there is
    // exactly the stall the timing monitor exists to catch. So Core 1 sets
    // `power_fault_unannounced` and Core 0 drains it in dccexLoop() and emits
    // the <p0 ...> frames. Both are volatile bools written by one core and read
    // by the other -- single-word, no struct assignment across cores (rule 4).
    volatile bool power_fault_latched;      // A fault cut power; LED follows this
    volatile bool power_fault_unannounced;  // Core 0 owes the host a <p0 ...>

    // Operation mode and configuration
    OperationMode operation_mode;
    PicoConfigStorage config_storage;

    void raisePowerFault();

public:
    PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s, uint8_t timing_led_pin);

    void dccLoop();
    void dccexLoop();
    
    // Safety functions
    void emergencyPowerCutoff();

    /** Whether a fault (timing, PIO, Core 1 heartbeat, overcurrent) is currently cutting power. Cleared only by a deliberate power restore. */
    bool isPowerFaultLatched() const { return power_fault_latched; }
    
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