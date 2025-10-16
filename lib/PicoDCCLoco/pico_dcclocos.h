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
#define INVALID_LOCO_ADDR 65535

class PicoDccLocos
{
private:
    std::vector<PicoDccLoco> locos;
    uint16_t last_loco_reminder;
    semaphore_t locos_lock;


public:
    PicoDccLocos();

    size_t getLocoCount();

    void addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd);
    
    void forgetLoco(uint16_t address);
    void forgetAllLocos();

    PicoDccLoco *findLoco(uint16_t address);
    
    bool getNextReminder(raw_dcc_cmd_t &cmd);
    
    void sendEmergencyStopResponses();
};

#endif