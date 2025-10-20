#ifndef PICO_DCCEX_H
#define PICO_DCCEX_H

#include <stdio.h>
#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/util/queue.h>
#endif

#include "pico_dccexpacket.h"

#ifndef COMMAND_BUFFER_SIZE
 #define COMMAND_BUFFER_SIZE 100
#endif

enum pico_dccex_state
{
    DCCEX_IDLE = 0,
    DCCEX_RECIVING = 1,
    DCCEX_PACKET = 2
};

class PicoDccEx
{
private:
    int maxSupportedCabs = 0;

    enum pico_dccex_state processState;
    int bufferLength = 0;
    char buffer[COMMAND_BUFFER_SIZE];
    PicoDccExPacket *currentPacket = nullptr;


public:
    PicoDccEx(int maxCab);

    bool processCommand(pico_dccex_packet* packet);
    void reset();

    enum pico_dccex_state getProcessState() { return processState; }
    int getMaxSupportedCabs() { return maxSupportedCabs; }
    const char* getBuffer() const { return buffer; }
};

#endif