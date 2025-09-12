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

    // Initiallising locally to allow the above validation ahead of initialisation
    this->address = (uint16_t)packet->getCab();
    
    if (packet->isThrottleCommand() || packet->isFunctionCommand())
    {
        this->forward = packet->getDirection() == 1;
        this->speed = packet->getSpeed();
        generateThrottleCommand();
    }
}

PicoDccLoco::PicoDccLoco(uint16_t address)
    : PicoDccLoco(address, (uint8_t)0, true)
{
}

PicoDccLoco::PicoDccLoco(uint16_t address, uint8_t speed, bool forward)
    : address(address), speed(speed), forward(forward)
{
    generateThrottleCommand();
}

bool PicoDccLoco::update(PicoDccExPacket *packet)
{
    // A loco can only exist from a throttle or function command
    if (packet->isThrottleCommand() || packet->isFunctionCommand())
    {
        bool direction = packet->getDirection() == 1;
        uint8_t speed = packet->getSpeed();
        return updateControl(direction, speed);
    }

    return false;
}

bool PicoDccLoco::updateControl(bool _forward, uint8_t _speed)
{
    if (forward != _forward || speed != _speed)
    {
        forward = _forward;
        speed = _speed;

        generateThrottleCommand();

        return true;
    }

    return false;
}

void PicoDccLoco::generateThrottleCommand()
{
    // Create the DCC command that will be sent to the track when needed
    cmd.length = 0;
    if (address > HIGHEST_SHORT_ADDR)
    {
        cmd.data[cmd.length++] = (address >> 8) | 0xc0;
    }
    cmd.data[cmd.length++] = address & 0xff;

    uint8_t speed128 = (speed & 0x7f);
    uint8_t speed28 = (speed128 * 10 + 36) / 46;
    uint8_t code28 = ((speed28 + 3) / 2) | ((speed28 & 1) ? 0 : 16);
    cmd.data[cmd.length++] = 64 | code28 | ((forward ? 1 : 0) * 32);
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
    return cmd;
}

raw_dcc_cmd_t PicoDccLoco::getFunctionCommand(uint8_t fnGroup)
{
    return raw_dcc_cmd_t();
}

bool PicoDccLoco::isValid() const {
    return address != INVALID_LOCO_ADDR;
}
