#ifndef PICO_DCCEXPACKET_H
#define PICO_DCCEXPACKET_H

#include <stdio.h>
#include <pico/stdlib.h>
#include <malloc.h>
#include <string.h>
#include "../PicoDCCTrack/pico_dcctrack.h"

enum pico_dccex_track_select
{
    DCCEX_TRACK_ALL = 0,
    DCCEX_TRACK_MAIN = 1,
    DCCEX_TRACK_PROG = 2
};

class PicoDccExPacket
{
private:
    char opcode = '\0';
    int cab = 0;
    int speed_funct = 0;
    int direction_state = 0;

    bool power_on = false;
    pico_dccex_track_select power_track = DCCEX_TRACK_ALL;
    bool valid_packet = false;

    raw_dcc_cmd_t *raw_dcc_cmd;
    char *dcc_update;

public:
    PicoDccExPacket(char *buffer);
    ~PicoDccExPacket();

    bool isValid() { return valid_packet; }

    bool isPowerCommand() { return opcode == '0' || opcode == '1'; }
    bool isVersionCommand() { return opcode == 's'; }
    bool isNumCabsCommand() { return opcode == '#'; }
    bool isThrottleCommand() { return opcode == 't'; }
    bool isFunctionCommand() { return opcode == 'F'; }
    bool isEmergencyStopCommand() { return opcode == '!'; }

    bool getPowerOn() { return power_on; }
    pico_dccex_track_select getTrack() { return power_track; }

    char getOpcode() { return opcode; }
    int getCab() { return cab; }
    int getSpeed() { return speed_funct; }
    int getFunct() { return speed_funct; }
    int getDirection() { return direction_state; }
    int getState() { return direction_state; }

    raw_dcc_cmd_t *getRawDccThrottleCmd();
    raw_dcc_cmd_t *getRawDccFunctionCmd();
    char *getDccExUpdate();
};

#endif