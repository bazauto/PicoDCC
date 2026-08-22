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


public:
    PicoDccLocos();

    size_t getLocoCount();

    // Returns false, and leaves cmd zeroed, when the candidate loco is invalid
    // (bad opcode, bad address, bad speed) or the collection is already at
    // MAX_LOCO. Nothing is pushed, nothing is queued, on a false return.
    bool addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd);
    
    void forgetLoco(uint16_t address);
    void forgetAllLocos();

    PicoDccLoco *findLoco(uint16_t address);
    
    // Update an existing loco's throttle settings while maintaining thread safety
    // Returns true if loco was found and updated, false if not found
    bool updateLocoThrottle(uint16_t address, PicoDccExPacket *packet, raw_dcc_cmd_t &cmd);
    
    bool getNextReminder(raw_dcc_cmd_t &cmd);
    
    void sendEmergencyStopResponses();
};

#endif