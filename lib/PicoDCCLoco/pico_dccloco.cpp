#include <string.h>
#include <stdexcept>

#include "pico_dccloco.h"

PicoDccLoco::PicoDccLoco(PicoDccExPacket *packet)
{
    // A loco can only exist from a throttle or function command
    if (!packet->isThrottleCommand() && !packet->isFunctionCommand())
    {
        throw std::invalid_argument("Only throttle or function commands can be used to create a loco.");
    }

    if (packet->getCab() < 0 || packet->getCab() > 65535)
    {
        throw std::invalid_argument("Loco address outside allowed range.");
    }

    if (packet->getSpeed() < 0 || packet->getSpeed() > 255)
    {
        throw std::invalid_argument("Loco speed outside allowed range.");
    }

    //PicoDccLoco((uint16_t)packet->getCab(), (uint8_t)packet->getSpeed(), (packet->getDirection() == 1));
    this->address = (uint16_t)packet->getCab();
    this->speed = (uint8_t)packet->getSpeed();
    this->forward = (packet->getDirection() == 1);
}

PicoDccLoco::PicoDccLoco(uint16_t address)
{
    PicoDccLoco(address, (uint8_t)0, true);
}

PicoDccLoco::PicoDccLoco(uint16_t address, uint8_t speed, bool forward)
{
    this->address = address;
    this->speed = speed;
    this->forward = forward;
}

void PicoDccLoco::update(PicoDccExPacket *packet)
{
    // A loco can only exist from a throttle or function command
    if (packet->isThrottleCommand() && packet->isFunctionCommand())
    {
        updateControl((packet->getDirection() == 1), (uint8_t)packet->getSpeed());
    }
}

void PicoDccLoco::updateControl(bool forward, uint8_t speed)
{
    if (speed > 127)
        return;

    if (this->forward != forward || this->speed != speed)
    {
        // Notify
    }

    this->forward = forward;
    this->speed = speed;
}

void PicoDccLoco::updateFunct(uint8_t function, bool value)
{
}

raw_dcc_cmd_t PicoDccLoco::getEmergecyStopCommand()
{

    return raw_dcc_cmd_t();
}

raw_dcc_cmd_t PicoDccLoco::getThrottleCommand()
{
    raw_dcc_cmd_t cmd{0x0, {}};
    if (address > HIGHEST_SHORT_ADDR)
    {
        cmd.data[cmd.length++] = (address >> 8) | 0xc0;
    }
    cmd.data[cmd.length++] = address & 0xff;

    uint8_t speed128 = (speed & 0x7f);
    uint8_t speed28 = (speed128 * 10 + 36) / 46;
    uint8_t code28 = ((speed28 + 3) / 2) | ((speed28 & 1) ? 0 : 16);
    cmd.data[cmd.length++] = 64 | code28 | ((forward ? 1 : 0) * 32);

    return cmd;
}

raw_dcc_cmd_t PicoDccLoco::getFunctionCommand(uint8_t fnGroup)
{
    return raw_dcc_cmd_t();
}
