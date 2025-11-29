#ifndef PICO_DCCEXPACKET_H
#define PICO_DCCEXPACKET_H

#include <stdio.h>
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

    raw_dcc_cmd_t raw_dcc_cmd;
    char dccex_cab_update[16] = {'\0'};
    char dccex_power_update[10] = {'\0'};

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
    bool isConfigCommand() { return packet.opcode == 'D' || packet.opcode == 'E'; }
    bool isSaveCommand() { return packet.opcode == 'E'; }
    bool isReadAddressCommand() { return packet.opcode == 'R'; }
    bool isVerifyCommand() { return packet.opcode == 'V'; }
    bool isWriteCommand() { return packet.opcode == 'W'; }
    bool isUnsupportedCommand() { return packet.opcode == 'T' || packet.opcode == 'S' || packet.opcode == 'Z'; }

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
    
    // Config command accessors (D commands)
    // Valid ranges enforced in validatePacket():
    //   LIMIT (param1=1): 30-100mA (ACK threshold, NMRA S-9.2.3 specifies 60mA)
    //   MIN (param1=2): 3000-8000µs (minimum ACK pulse duration)
    //   MAX (param1=3): 6000-10000µs (maximum ACK pulse duration)
    int getConfigSubcommand() { return packet.addr; }      // ACK = 1
    int getConfigParamType() { return packet.param1; }     // LIMIT=1, MIN=2, MAX=3
    int getConfigValue() { return packet.param2; }         // Numeric value
    
    // CV command accessors (R/V/W commands)
    int getCVNumber() { return packet.addr; }              // CV number (1-1024)
    int getCVValue() { return packet.param1; }             // CV value for verify/write
    int getWriteForm() { return packet.param2; }           // W command: 1=<W addr>, 2=<W cv value>

    raw_dcc_cmd_t *getRawDccAccessoryCmd();
    char *getDccExCabUpdate();
    char *getDccExPowerUpdate();
};

#endif