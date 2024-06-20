#include "pico_dccexpacket.h"

PicoDccExPacket::PicoDccExPacket(char *buffer)
{
    // Allocate memory for the generated outputs
    this->dcc_update = (char*)malloc(sizeof(char) * 15);
    this->raw_dcc_cmd = (raw_dcc_cmd_t*)malloc(sizeof(raw_dcc_cmd_t));

    // The packet to decode will be stored in the buffer and the output should go in currentMessage
    opcode = buffer[0];

    // Decode the rest of the pack if there are params
    switch (opcode)
    {
    // Track Power, Version and Max cabs don't have any parameters, just the opcode
    case ('0'):
    case ('1'):
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
    }
}

PicoDccExPacket::~PicoDccExPacket()
{
    if (this->dcc_update)
    {
        free(this->dcc_update);
    }

    if (this->raw_dcc_cmd)
    {
        free(this->raw_dcc_cmd);
    }
}

raw_dcc_cmd_t *PicoDccExPacket::getRawDccSpeedCmd()
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

char *PicoDccExPacket::getDccExUpdate()
{
    // If we have already constructed the string, simply return it.
    // We don't need to regenerate it as there is no means to alter the packet in this object.
    if (this->dcc_update) {
        return this->dcc_update;
    }

    uint8_t speed128 = (this->getSpeed() & 0x7f);
    int8_t responseSpeed = 0;
    if (speed128 == 1)
        responseSpeed = -1;

    if (speed128 > 1)
        speed128 = speed128 - 1;

    speed128 = speed128 | (this->getDirection() * 128);

    snprintf(this->dcc_update, sizeof(this->dcc_update), "<l %d 0 %d 0>", this->getCab(), speed128);
    return this->dcc_update;
}