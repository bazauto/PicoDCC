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

#define CMD_QUEUE_LENGTH 5

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



public:
    PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s, uint8_t timing_led_pin);

    void dccLoop();
    void dccexLoop();
    
    // Safety functions
    void emergencyPowerCutoff();

#ifdef TEST_BUILD
    // Test accessors
    bool isTrackPowerOn(bool isProg) { return isProg ? prog_track->getPower() : main_track->getPower(); }
    PicoDccEx* getDccEx() { return pico_dccex; }
    PicoDccTrack* getTrack(bool isProg) { return isProg ? prog_track : main_track; }
    size_t getLocoCount() { return pico_locos->getLocoCount(); }
    PicoDccLocos* getLocos() { return pico_locos; }
#endif
};

#endif