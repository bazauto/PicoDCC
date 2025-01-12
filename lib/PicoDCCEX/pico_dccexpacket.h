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

typedef struct {
    char text[16];
} dccex_cab_update_t;

typedef struct {
    char text[10];
} dccex_power_update_t;

// This is the raw packet data that is transfered through the queues
struct pico_dccex_packet
{
    char opcode;
    int cab;
    int speed_funct;
    int direction_state;
    bool power_on;
    pico_dccex_track_select power_track;
};

class PicoDccExPacket
{
private:
    pico_dccex_packet packet = {'\0', 0, 0, 0, false, DCCEX_TRACK_ALL};

    bool valid_packet = false;

    raw_dcc_cmd_t *raw_dcc_cmd;
    dccex_cab_update_t *dccex_cab_update;
    dccex_power_update_t *dccex_power_update;

    void initPacket();
    void validatePacket();

public:
    PicoDccExPacket(char *buffer);
    PicoDccExPacket(pico_dccex_packet packetData);
    ~PicoDccExPacket();

    pico_dccex_packet *getPacketData() { return &packet; }

    bool isValid() { return valid_packet; }

    bool isPowerCommand() { return packet.opcode == '0' || packet.opcode == '1'; }
    bool isVersionCommand() { return packet.opcode == 's'; }
    bool isNumCabsCommand() { return packet.opcode == '#'; }
    bool isThrottleCommand() { return packet.opcode == 't'; }
    bool isFunctionCommand() { return packet.opcode == 'F'; }
    bool isEmergencyStopCommand() { return packet.opcode == '!'; }

    bool getPowerOn() { return packet.power_on; }
    pico_dccex_track_select getTrack() { return packet.power_track; }

    char getOpcode() { return packet.opcode; }
    int getCab() { return packet.cab; }
    int getSpeed() { return packet.speed_funct; }
    int getFunct() { return packet.speed_funct; }
    int getDirection() { return packet.direction_state; }
    int getState() { return packet.direction_state; }

    raw_dcc_cmd_t *getRawDccThrottleCmd();
    raw_dcc_cmd_t *getRawDccFunctionCmd();
    dccex_cab_update_t *getDccExCabUpdate();
    dccex_power_update_t *getDccExPowerUpdate();
};

#endif