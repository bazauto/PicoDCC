#ifndef PICO_DCCEX_H
#define PICO_DCCEX_H

#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <string>
#include <functional>

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
    char buffer[COMMAND_BUFFER_SIZE] = "";
    PicoDccExPacket *currentPacket;


public:
    PicoDccEx(int maxCab);

    void loop(queue_t *cmd_queue);

    void reset();

    enum pico_dccex_state getProcessState() { return processState; }
};

#endif