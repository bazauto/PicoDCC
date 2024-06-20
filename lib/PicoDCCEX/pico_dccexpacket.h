#ifndef PICO_DCCEXPACKET_H
#define PICO_DCCEXPACKET_H

#include <stdio.h>
#include <pico/stdlib.h>
#include <malloc.h>
#include "../PicoDCCTrack/pico_dcctrack.h"

class PicoDccExPacket
{
private:
    char opcode = '\0';
    int cab = 0;
    int speed_funct = 0;
    int direction_state = 0;

    bool valid_packet = false;

    raw_dcc_cmd_t *raw_dcc_cmd;
    char *dcc_update;

public:
    PicoDccExPacket(char *buffer);
    ~PicoDccExPacket();

    bool isValid() { return valid_packet; }

    char getOpcode() { return opcode; }
    int getCab() { return cab; }
    int getSpeed() { return speed_funct; }
    int getFunct() { return speed_funct; }
    int getDirection() { return direction_state; }
    int getState() { return direction_state; }

    raw_dcc_cmd_t *getRawDccSpeedCmd();
    char *getDccExUpdate();
};

#endif