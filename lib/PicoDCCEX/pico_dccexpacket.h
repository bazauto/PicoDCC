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

// This is the raw packet data that is transfered through the queues
struct pico_dccex_packet
{
    char opcode;
    int addr;               // Cab or accessory address
    int param1;             // Speed, function or accessory subaddress
    int param2;             // Direction, function state or accessory activate
    bool power_on;
    pico_dccex_track_select power_track;
};

class PicoDccExPacket
{
private:
    pico_dccex_packet packet = {'\0', 0, 0, 0, false, DCCEX_TRACK_ALL};

    bool valid_packet = false;

    raw_dcc_cmd_t raw_dcc_cmd = {false, 0, {0}, 0, 0};
    char dccex_cab_update[16] = "";
    char dccex_power_update[10] = "";

    void decodePacket(char *buffer);
    void validatePacket();

public:
    PicoDccExPacket(char *buffer);
    PicoDccExPacket(pico_dccex_packet packetData);

    pico_dccex_packet *getPacketData() { return &packet; }

    bool isValid() { return valid_packet; }

    bool isPowerCommand() { return packet.opcode == '0' || packet.opcode == '1'; }
    bool isVersionCommand() { return packet.opcode == 's'; }
    bool isNumCabsCommand() { return packet.opcode == '#'; }
    bool isThrottleCommand() { return packet.opcode == 't'; }
    bool isFunctionCommand() { return packet.opcode == 'F'; }
    bool isEmergencyStopCommand() { return packet.opcode == '!'; }
    bool isAccesoryCommand() { return packet.opcode == 'a'; }

    bool getPowerOn() { return packet.power_on; }
    pico_dccex_track_select getTrack() { return packet.power_track; }

    char getOpcode() { return packet.opcode; }
    int getCab() { return packet.addr; }
    int getSpeed() { return packet.param1; }
    int getFunct() { return packet.param1; }
    int getDirection() { return packet.param2; }
    int getState() { return packet.param2; }
    int getAccessoryAddr() { return packet.addr; }
    int getAccessorySubAddr() { return packet.param1; }
    int getAccessoryActivate() { return packet.param2; }

    raw_dcc_cmd_t *getRawDccAccessoryCmd();
    char *getDccExCabUpdate();
    char *getDccExPowerUpdate();
};

#endif