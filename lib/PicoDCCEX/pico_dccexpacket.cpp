#include "pico_dccexpacket.h"

PicoDccExPacket::PicoDccExPacket()
{
    valid_packet = false;
}

PicoDccExPacket::PicoDccExPacket(char *buffer)
{
    // Allocate memory for the generated outputs
    this->dccex_cab_update = (char *)malloc(sizeof(char) * 15);
    this->dccex_power_update = (char *)malloc(sizeof(char) * 9);
    this->raw_dcc_cmd = (raw_dcc_cmd_t *)malloc(sizeof(raw_dcc_cmd_t));

    // The packet to decode will be stored in the buffer and the output should go in currentMessage
    opcode = buffer[0];

    // Decode the rest of the pack if there are params
    switch (opcode)
    {
    // Track Power
    case ('0'):
    case ('1'):
        valid_packet = true;

        if (opcode == '1')
            power_on = true;
        else
            power_on = false;

        if (strlen(buffer) == 1)
            break;

        char track[5];
        if (sscanf(buffer, "%*c %4s", track) == 1)
        {
            if (strcmp(track, "MAIN") == 0)
                power_track = DCCEX_TRACK_MAIN;

            if (strcmp(track, "PROG") == 0)
                power_track = DCCEX_TRACK_PROG;
        }
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
        if (sscanf(buffer, "%*c %d %d %d", &cab, &speed_funct, &direction_state) == 3)
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

PicoDccExPacket::~PicoDccExPacket()
{
    if (this->dccex_cab_update)
    {
        free(this->dccex_cab_update);
    }

    if (this->raw_dcc_cmd)
    {
        free(this->raw_dcc_cmd);
    }
}

raw_dcc_cmd_t *PicoDccExPacket::getRawDccThrottleCmd()
{
    // Some validation which will cause a null response if it fails.
    if (opcode != 't' && opcode != 'F')
    {
        return NULL;
    }
    if (!valid_packet)
    {
        return NULL;
    }

    // If we have already constructed the string, simply return it.
    // We don't need to regenerate it as there is no means to alter the packet in this object.
    if (this->raw_dcc_cmd)
    {
        return this->raw_dcc_cmd;
    }

    // Build the raw DCC packet from the contents of the packet
    int addr = this->getCab();
    if (addr > HIGHEST_SHORT_ADDR)
    {
        this->raw_dcc_cmd->data[this->raw_dcc_cmd->length++] = (addr >> 8) | 0xc0;
    }
    this->raw_dcc_cmd->data[this->raw_dcc_cmd->length++] = addr & 0xff;

    uint8_t speed128 = (this->getSpeed() & 0x7f);
    uint8_t speed28 = (speed128 * 10 + 36) / 46;
    uint8_t code28 = ((speed28 + 3) / 2) | ((speed28 & 1) ? 0 : 16);
    this->raw_dcc_cmd->data[this->raw_dcc_cmd->length++] = 64 | code28 | (this->getDirection() * 32);

    return this->raw_dcc_cmd;
}

raw_dcc_cmd_t *PicoDccExPacket::getRawDccFunctionCmd()
{
    return nullptr;
}

char *PicoDccExPacket::getDccExCabUpdate()
{
    // If we have already constructed the string, simply return it.
    // We don't need to regenerate it as there is no means to alter the packet in this object.
    if (this->dccex_cab_update)
    {
        return this->dccex_cab_update;
    }

    uint8_t speed128 = (this->getSpeed() & 0x7f);
    int8_t responseSpeed = 0;
    if (speed128 == 1)
        responseSpeed = -1;

    if (speed128 > 1)
        speed128 = speed128 - 1;

    speed128 = speed128 | (this->getDirection() * 128);

    snprintf(this->dccex_cab_update, sizeof(this->dccex_cab_update), "<l %d 0 %d 0>", this->getCab(), speed128);
    return this->dccex_cab_update;
}

char *PicoDccExPacket::getDccExPowerUpdate()
{
    if (this->dccex_power_update)
    {
        return this->dccex_power_update;
    }

    if (this->getTrack() == DCCEX_TRACK_ALL)
        snprintf(this->dccex_power_update, sizeof(this->dccex_power_update), "<p%d>", (this->power_on ? 1 : 0));

    if (this->getTrack() == DCCEX_TRACK_MAIN)
        snprintf(this->dccex_power_update, sizeof(this->dccex_power_update), "<p%d MAIN>", (this->power_on ? 1 : 0));

    if (this->getTrack() == DCCEX_TRACK_PROG)
        snprintf(this->dccex_power_update, sizeof(this->dccex_power_update), "<p%d PROG>", (this->power_on ? 1 : 0));

    return this->dccex_power_update;
}
