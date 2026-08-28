#ifndef PICO_DCCLOCOS_H
#define PICO_DCCLOCOS_H

#include <stdio.h>
#include <vector>
#include <list>
#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/sem.h>
#include <pico/util/queue.h>
#endif

#include "../PicoDCCLoco/pico_dccloco.h"
#include "../PicoDCCEX/pico_dccexpacket.h"
#include "../PicoDCCTrack/pico_dcctrack.h"

#define MAX_LOCO 50
// INVALID_LOCO_ADDR lives in dcc_types.h now.

class PicoDccLocos
{
private:
    std::vector<PicoDccLoco> locos;
    uint16_t last_loco_reminder;
    semaphore_t locos_lock;

    // Speed step mode for locos that have not been named individually (#8).
    // RAM only, never persisted -- see PicoDccLoco::speed_steps.
    uint8_t station_speed_steps;


public:
    PicoDccLocos();

    size_t getLocoCount();

    // Returns false, and leaves cmd zeroed, when the candidate loco is invalid
    // (bad opcode, bad address, bad speed) or the collection is already at
    // MAX_LOCO. Nothing is pushed, nothing is queued, on a false return.
    bool addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd);
    
    void forgetLoco(uint16_t address);
    void forgetAllLocos();

    // Sets every known loco to speed 0, preserving direction (#3). Used by the
    // <!> broadcast emergency stop, so that the reminder stream keeps asserting
    // "stopped" for locos that may have missed the broadcast. Returns the
    // number of locos that were actually moving.
    size_t stopAllLocos();

    // Copies the named loco into `out` and returns true, or returns false and
    // leaves `out` untouched if the address is unknown. The copy is taken with
    // the lock held.
    //
    // This replaces findLoco(), which handed back a raw pointer into the vector
    // after releasing the lock (#37) -- something the caller could not safely
    // hold and had no way to protect. Returning a copy matches the shape
    // updateLocoThrottle() and getNextReminder() already use.
    //
    // To *change* a loco, use updateLocoThrottle() or setLocoSpeedSteps().
    // Mutation belongs inside this class, under the lock; writing through a
    // snapshot changes nothing in the collection.
    bool getLoco(uint16_t address, PicoDccLoco &out);

    // Update an existing loco's throttle settings while maintaining thread safety
    // Returns true if loco was found and updated, false if not found
    bool updateLocoThrottle(uint16_t address, PicoDccExPacket *packet, raw_dcc_cmd_t &cmd);
    
    bool getNextReminder(raw_dcc_cmd_t &cmd);

    // <D SPEED28|SPEED128> with no cab: the default every loco follows until
    // one is named individually. Returns false if steps is neither 28 nor 128.
    bool setStationSpeedSteps(uint8_t steps);
    uint8_t getStationSpeedSteps();

    // <D SPEED28|SPEED128 cab>: names one loco. An unknown cab is *created*,
    // stopped and facing forward, so that the mode is already right when the
    // first throttle command for it arrives -- the orchestrator re-asserts the
    // roster's modes on seeing the boot banner, which is before it drives
    // anything. Returns false only for an address outside 1..10239, a step
    // count that is neither 28 nor 128, or a collection already at MAX_LOCO.
    bool setLocoSpeedSteps(uint16_t address, uint8_t steps);

    // The mode a given loco is currently using, or the station default when
    // the loco is not in the collection.
    uint8_t getLocoSpeedSteps(uint16_t address);
    
    void sendEmergencyStopResponses();
};

#endif