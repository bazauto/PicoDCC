#include <string.h>

#include "pico_dccloco.h"
#include "../pico_diagnostic.h"

PicoDccLoco::PicoDccLoco(PicoDccExPacket *packet, uint8_t speed_steps)
{
    // Set before any generateThrottleCommand() call below -- the encoding
    // branch reads it.
    this->speed_steps = dcc_is_valid_speed_step_mode(speed_steps)
                            ? speed_steps
                            : (uint8_t)DCC_DEFAULT_SPEED_STEPS;
    this->speed_steps_overridden = false;

    // A loco can only exist from a throttle or function command. Anything else
    // (a malformed or unrelated packet reaching here) used to throw and abort
    // the firmware (#2); it now yields an inert loco instead. Every member is
    // assigned on every path below -- with the throws gone, an early return
    // would otherwise leave some of them uninitialised.
    if (!packet->isThrottleCommand() && !packet->isFunctionCommand())
    {
        address = INVALID_LOCO_ADDR;
        speed = 0;
        forward = true;
        generateThrottleCommand();
        LOG_WARNING(COMPONENT_DCCEX, "Loco not created: unsupported opcode");
        return;
    }

    if (!dcc_is_valid_loco_address(packet->getCab()))
    {
        address = INVALID_LOCO_ADDR;
        speed = 0;
        forward = true;
        generateThrottleCommand();
        LOG_WARNING(COMPONENT_DCCEX, "Loco not created: address out of range");
        return;
    }

    address = (uint16_t)packet->getCab();

    // A function command carries the function number in param1 and its state in
    // param2 -- the same fields a throttle command uses for speed and direction.
    // Reading them as speed and direction meant <F 3 8 1> set loco 3 to speed 8
    // forward, so pressing F8 accelerated the locomotive (#1). An <F>-created
    // loco starts stopped and forward; the function state itself is not stored
    // here, because function support is still a stub.
    if (packet->isFunctionCommand())
    {
        speed = 0;
        forward = true;
        generateThrottleCommand();
        return;
    }

    // Throttle command.
    if (!dcc_is_valid_throttle_speed(packet->getSpeed()))
    {
        // The address is good; fail safe to stop rather than reject the whole
        // loco. Unreachable once validatePacket() gates this at the protocol
        // layer -- this is defence in depth for callers that construct a
        // PicoDccLoco directly.
        speed = 0;
        forward = packet->getDirection() == 1;
        generateThrottleCommand();
        LOG_WARNING(COMPONENT_DCCEX, "Loco speed out of range, using stop");
        return;
    }

    forward = packet->getDirection() == 1;
    speed = dcc_speed_code(packet->getSpeed());
    generateThrottleCommand();
}

PicoDccLoco::PicoDccLoco(uint16_t address, uint8_t speed_steps)
    : PicoDccLoco(address, (uint8_t)0, true, speed_steps)
{
}

PicoDccLoco::PicoDccLoco(uint16_t address, uint8_t speed, bool forward, uint8_t speed_steps)
{
    // Validated rather than trusted: this parameter sits next to `speed` in
    // the argument list, and a step count is not a speed.
    if (!dcc_is_valid_speed_step_mode(speed_steps))
    {
        LOG_WARNING(COMPONENT_DCCEX, "Invalid speed step mode, using default");
        speed_steps = (uint8_t)DCC_DEFAULT_SPEED_STEPS;
    }
    this->speed_steps = speed_steps;
    this->speed_steps_overridden = false;

    if (dcc_is_valid_loco_address(address))
    {
        this->address = address;
    }
    else
    {
        this->address = INVALID_LOCO_ADDR;
        LOG_WARNING(COMPONENT_DCCEX, "Loco not created: address out of range");
    }

    if (speed <= DCC_MAX_THROTTLE_SPEED || speed == DCC_SPEED_ESTOP)
    {
        this->speed = speed;
    }
    else
    {
        this->speed = 0;
        LOG_WARNING(COMPONENT_DCCEX, "Loco speed out of range, using stop");
    }

    this->forward = forward;

    generateThrottleCommand();
}

bool PicoDccLoco::update(PicoDccExPacket *packet)
{
    if (!packet->isThrottleCommand())
    {
        // Function commands reach here too, and must not touch speed or
        // direction -- param1/param2 are the function number and state, not a
        // throttle setting (#1). Making <F> inert is the fix; implementing
        // functions is a separate piece of work (updateFunct() is still a stub).
        return false;
    }

    if (!dcc_is_valid_throttle_speed(packet->getSpeed()))
    {
        LOG_WARNING(COMPONENT_DCCEX, "Throttle speed out of range, ignored");
        return false;
    }

    return updateControl(packet->getDirection() == 1, dcc_speed_code(packet->getSpeed()));
}

bool PicoDccLoco::updateControl(bool _forward, uint8_t _speed)
{
    if (_speed > DCC_MAX_THROTTLE_SPEED && _speed != DCC_SPEED_ESTOP)
    {
        LOG_WARNING(COMPONENT_DCCEX, "Throttle speed out of range, ignored");
        return false;
    }

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
    // Initialize all fields to prevent undefined behavior
    cmd.is_prog = false;
    cmd.length = 0;
    cmd.cmd_data = 0;
    cmd.repeats = 3;  // Repeat 3 times when explicitly commanded (e.g., throttle change)

    // Defence in depth (#16): even if isValid() is ever loosened, no code path
    // through here may put anything on the rails for an address outside
    // 1..10239. Emitting nothing is the correct failure -- a silent loco is a
    // bug report, a broadcast or idle-address packet is a layout incident.
    if (!isValid())
    {
        cmd.length = 0;
        cmd.repeats = 0;
        return;
    }

    if (address > HIGHEST_SHORT_ADDR)
    {
        cmd.data[cmd.length++] = ((address >> 8) & 0x3F) | 0xc0;
    }
    cmd.data[cmd.length++] = address & 0xff;

    if (speed_steps == DCC_SPEED_STEPS_128)
    {
        // S-9.2.1 advanced operations: instruction 0x3F followed by one byte of
        // (direction << 7) | value, where value 0 is the controlled stop, 1 is
        // the emergency stop, and 2..127 are speed steps 1..126 (#8).
        //
        // So there is an off-by-one against the <t> wire value that the 28-step
        // path below does not have: <t> speed 0 encodes as 0, but <t> speed N
        // encodes as N + 1. Getting this wrong shifts every train by one step,
        // which is almost invisible by eye and completely visible to a braking
        // profile.
        cmd.data[cmd.length++] = 0x3F;
        uint8_t value;
        if (speed == DCC_SPEED_ESTOP)
        {
            value = 1;
        }
        else if (speed == 0)
        {
            value = 0;
        }
        else
        {
            value = (uint8_t)(speed + 1);  // 1..126 -> 2..127
        }
        cmd.data[cmd.length++] = (forward ? 0x80 : 0x00) | value;
        return;
    }

    if (speed == DCC_SPEED_ESTOP)
    {
        // Same instruction byte as the <!> broadcast estop (0x00 0x41 built in
        // PicoDccController::dccexLoop()), addressed to this one locomotive,
        // with the direction bit preserved so the decoder knows which way to
        // go when the throttle resumes (#11).
        cmd.data[cmd.length++] = 0x41 | (forward ? 0x20 : 0x00);
        return;
    }

    // S-9.2 speed and direction is 01DCSSSS, where the 5-bit speed value is
    // (SSSS << 1) | C. Value 0 is the controlled stop, 1 and 3 are emergency
    // stop, 2 is the alternate stop, and a moving step N is value N + 3.
    //
    // The old expression was written for N in 1..28 and produced garbage for 0:
    // ((0 + 3) / 2) | 16 == 17, which is value 3 -- an emergency stop. Every
    // ordinary stop on the layout was therefore slamming the locomotive to a
    // halt instead of letting it decelerate under its own momentum CV, and an
    // orchestrator braking ramp had its final command discarded (#48).
    uint8_t speed128 = (speed & 0x7f);
    uint8_t speed28 = (speed128 * 10 + 36) / 46;
    uint8_t value = (speed28 == 0) ? 0 : (uint8_t)(speed28 + 3);
    uint8_t code28 = (uint8_t)((value >> 1) | ((value & 1) << 4));
    cmd.data[cmd.length++] = 0x40 | code28 | (forward ? 0x20 : 0x00);
}

bool PicoDccLoco::setSpeedSteps(uint8_t steps)
{
    if (!dcc_is_valid_speed_step_mode(steps))
    {
        LOG_WARNING(COMPONENT_DCCEX, "Speed step mode rejected: not 28 or 128");
        return false;
    }

    // The override is recorded even when the mode is unchanged: a loco already
    // sitting on the station default has still been *named*, and must not be
    // dragged along by a later <D SPEED128> with no cab.
    speed_steps_overridden = true;

    if (speed_steps == steps)
    {
        return false;
    }

    speed_steps = steps;
    generateThrottleCommand();
    return true;
}

bool PicoDccLoco::applyDefaultSpeedSteps(uint8_t steps)
{
    if (speed_steps_overridden || !dcc_is_valid_speed_step_mode(steps) || speed_steps == steps)
    {
        return false;
    }

    speed_steps = steps;
    generateThrottleCommand();
    return true;
}

void PicoDccLoco::updateFunct(uint8_t function, bool value)
{
}



raw_dcc_cmd_t PicoDccLoco::getThrottleCommand()
{
    return cmd;
}

raw_dcc_cmd_t PicoDccLoco::getFunctionCommand(uint8_t fnGroup)
{
    return raw_dcc_cmd_t();
}

bool PicoDccLoco::isValid() const
{
    return address >= DCC_MIN_LOCO_ADDR && address <= DCC_MAX_LOCO_ADDR;
}
