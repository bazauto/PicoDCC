#include "pico_dccexpacket.h"

PicoDccExPacket::PicoDccExPacket(char *buffer)
{
    initPacket();

    // The packet to decode will be stored in the buffer and the output should go in currentMessage
    packet.opcode = buffer[0];

    // Decode the rest of the pack if there are params
    switch (packet.opcode)
    {
    // Track Power
    case ('0'):
    case ('1'):
        if (packet.opcode == '1')
            packet.power_on = true;
        else
            packet.power_on = false;

        if (strlen(buffer) == 1)
            break;

        char track[5];
        if (sscanf(buffer, "%*c %4s", track) == 1)
        {
            if (strcmp(track, "MAIN") == 0)
                packet.power_track = DCCEX_TRACK_MAIN;

            if (strcmp(track, "PROG") == 0)
                packet.power_track = DCCEX_TRACK_PROG;
        }
        break;

    // Throttle Control and Functions have the same parameter layout
    // <t cab speed direction> and <F cab funct state>
    case ('t'):
    case ('F'):
        if (sscanf(buffer, "%*c %d %d %d", &packet.cab, &packet.speed_funct, &packet.direction_state) != 3)
        {
            packet.cab = -1; // Invalid cab
        }
        break;
    }

    validatePacket();
}

PicoDccExPacket::PicoDccExPacket(pico_dccex_packet packetData)
{
    initPacket();
    packet = packetData;
    validatePacket();
}

PicoDccExPacket::~PicoDccExPacket()
{
    if (dccex_cab_update)
    {
        free(dccex_cab_update);
        dccex_cab_update = nullptr;
    }

    if (dccex_power_update)
    {
        free(dccex_power_update);
        dccex_power_update = nullptr;
    }

    if (raw_dcc_cmd)
    {
        free(raw_dcc_cmd);
        raw_dcc_cmd = nullptr;
    }
}

void PicoDccExPacket::initPacket()
{
    // Allocate memory for the generated outputs
    dccex_cab_update = (dccex_cab_update_t *)malloc(sizeof(dccex_cab_update_t));
    memset(dccex_cab_update, 0, sizeof(dccex_cab_update_t));
    dccex_power_update = (dccex_power_update_t *)malloc(sizeof(dccex_power_update_t));
    memset(dccex_power_update, 0, sizeof(dccex_power_update_t));
    raw_dcc_cmd = (raw_dcc_cmd_t *)malloc(sizeof(raw_dcc_cmd_t));
    memset(raw_dcc_cmd, 0, sizeof(raw_dcc_cmd));
}

void PicoDccExPacket::validatePacket()
{
    valid_packet = false;

    // Here we validate that the packet is supported by this controller
    // And if so it has the required data to perform the operation
    switch (packet.opcode)
    {
    // Track Power
    case ('0'):
    case ('1'):
        valid_packet = true;
        break;

    // Version and Num supported cabs
    case ('s'):
    case ('#'):
        valid_packet = true;
        break;

    // Throttle Control and Functions have the same parameter layout
    // <t cab speed direction> and <F cab funct state>
    case ('t'):
    case ('F'):
        if (packet.cab != -1)
        {
            valid_packet = true;
        }
        break;

    // Emergency Stop
    case ('!'):
        valid_packet = true;
        break;
    }
}

raw_dcc_cmd_t *PicoDccExPacket::getRawDccThrottleCmd()
{
    // Some validation which will cause a null response if it fails.
    if (packet.opcode != 't' && packet.opcode != 'F')
    {
        return NULL;
    }
    if (!valid_packet)
    {
        return NULL;
    }

    // Build the raw DCC packet from the contents of the packet
    int addr = getCab();
    if (addr > HIGHEST_SHORT_ADDR)
    {
        raw_dcc_cmd->data[raw_dcc_cmd->length++] = (addr >> 8) | 0xc0;
    }
    raw_dcc_cmd->data[raw_dcc_cmd->length++] = addr & 0xff;

    uint8_t speed128 = (getSpeed() & 0x7f);
    uint8_t speed28 = (speed128 * 10 + 36) / 46;
    uint8_t code28 = ((speed28 + 3) / 2) | ((speed28 & 1) ? 0 : 16);
    raw_dcc_cmd->data[raw_dcc_cmd->length++] = 64 | code28 | (getDirection() * 32);

    return raw_dcc_cmd;
}

raw_dcc_cmd_t *PicoDccExPacket::getRawDccFunctionCmd()
{
    return nullptr;
}

dccex_cab_update_t *PicoDccExPacket::getDccExCabUpdate()
{
    uint8_t speed128 = (getSpeed() & 0x7f);
    int8_t responseSpeed = 0;
    if (speed128 == 1)
        responseSpeed = -1;

    if (speed128 > 1)
        speed128 = speed128 - 1;

    speed128 = speed128 | (getDirection() * 128);

    snprintf(dccex_cab_update->text, sizeof(dccex_cab_update->text), "<l %d 0 %d 0>", getCab(), speed128);
    return dccex_cab_update;
}

dccex_power_update_t *PicoDccExPacket::getDccExPowerUpdate()
{
    if (getTrack() == DCCEX_TRACK_ALL)
        snprintf(dccex_power_update->text, sizeof(dccex_power_update->text), "<p%d>", (packet.power_on ? 1 : 0));

    if (getTrack() == DCCEX_TRACK_MAIN)
        snprintf(dccex_power_update->text, sizeof(dccex_power_update->text), "<p%d MAIN>", (packet.power_on ? 1 : 0));

    if (getTrack() == DCCEX_TRACK_PROG)
        snprintf(dccex_power_update->text, sizeof(dccex_power_update->text), "<p%d PROG>", (packet.power_on ? 1 : 0));

    return dccex_power_update;
}
