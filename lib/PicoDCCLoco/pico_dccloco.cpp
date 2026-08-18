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

    // Check speed first to give more specific error messages
    if (packet->getSpeed() < 0 || packet->getSpeed() > 255)
    {
        throw std::invalid_argument("Loco speed outside allowed range.");
    }

    if (packet->getCab() < 0 || packet->getCab() > 65535)
    {
        throw std::invalid_argument("Loco address outside allowed range.");
    }

    // Initiallising locally to allow the above validation ahead of initialisation
    this->address = (uint16_t)packet->getCab();
    
    // Initialize all functions to off
    memset(functions, 0, sizeof(functions));

    if (packet->isThrottleCommand())
    {
        this->forward = packet->getDirection() == 1;
        this->speed = packet->getSpeed();
        generateThrottleCommand();
    }
    else if (packet->isFunctionCommand())
    {
        // Function command: initialize with default speed/direction, update function
        this->forward = true;
        this->speed = 0;
        generateThrottleCommand();
        
        // Update the specific function
        updateFunct(packet->getFunct(), packet->getState() == 1);
    }
}

PicoDccLoco::PicoDccLoco(uint16_t address)
    : PicoDccLoco(address, (uint8_t)0, true)
{
}

PicoDccLoco::PicoDccLoco(uint16_t address, uint8_t speed, bool forward)
    : address(address), speed(speed), forward(forward)
{
    // Initialize all functions to off
    memset(functions, 0, sizeof(functions));
    generateThrottleCommand();
}

bool PicoDccLoco::update(PicoDccExPacket *packet)
{
    if (packet->isThrottleCommand())
    {
        bool direction = packet->getDirection() == 1;
        uint8_t speed = packet->getSpeed();
        return updateControl(direction, speed);
    }
    else if (packet->isFunctionCommand())
    {
        // Function command: update the specific function state
        updateFunct(packet->getFunct(), packet->getState() == 1);
        return true;  // Function was updated
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
    // Initialize all fields to prevent undefined behavior
    cmd.is_prog = false;
    cmd.length = 0;
    cmd.cmd_data = 0;
    cmd.repeats = 3;  // Repeat 3 times when explicitly commanded (e.g., throttle change)
    
    if (address > HIGHEST_SHORT_ADDR)
    {
        cmd.data[cmd.length++] = (address >> 8) | 0xc0;
    }
    cmd.data[cmd.length++] = address & 0xff;
    // We are supporting only 128 speed steps here
    cmd.data[cmd.length++] = 0x3f;
    cmd.data[cmd.length++] = (speed & 0x7f) | (forward ? 0x80 : 0x00);
}

uint8_t PicoDccLoco::updateFunct(uint8_t function, bool value)
{
    // Validate function number (0-28)
    if (function > 28) {
        return 0;  // Invalid function number
    }
    
    // Update the function state
    functions[function] = value;
    
    // Return which group this function belongs to
    if (function <= 4) return 1;      // F0-F4
    if (function <= 8) return 2;      // F5-F8
    if (function <= 12) return 3;     // F9-F12
    if (function <= 20) return 4;     // F13-F20
    return 5;                         // F21-F28
}



raw_dcc_cmd_t PicoDccLoco::getThrottleCommand()
{
    return cmd;
}

raw_dcc_cmd_t PicoDccLoco::getFunctionCommand(uint8_t fnGroup)
{
    raw_dcc_cmd_t fn_cmd;
    fn_cmd.is_prog = false;
    fn_cmd.length = 0;
    fn_cmd.cmd_data = 0;
    fn_cmd.repeats = 3;  // Repeat function commands 3 times
    
    // Add address bytes
    if (address > HIGHEST_SHORT_ADDR)
    {
        fn_cmd.data[fn_cmd.length++] = (address >> 8) | 0xc0;
    }
    fn_cmd.data[fn_cmd.length++] = address & 0xff;
    
    // Generate instruction byte based on function group
    switch (fnGroup) {
        case 1:  // F0-F4 (instruction byte: 100DDDDD)
            // Bit 4 = F0, Bits 3-0 = F4-F1
            fn_cmd.data[fn_cmd.length++] = 0x80 | 
                                          (functions[0] ? 0x10 : 0) |  // F0
                                          (functions[1] ? 0x01 : 0) |  // F1
                                          (functions[2] ? 0x02 : 0) |  // F2
                                          (functions[3] ? 0x04 : 0) |  // F3
                                          (functions[4] ? 0x08 : 0);   // F4
            break;
            
        case 2:  // F5-F8 (instruction byte: 1011DDDD)
            fn_cmd.data[fn_cmd.length++] = 0xB0 | 
                                          (functions[5] ? 0x01 : 0) |  // F5
                                          (functions[6] ? 0x02 : 0) |  // F6
                                          (functions[7] ? 0x04 : 0) |  // F7
                                          (functions[8] ? 0x08 : 0);   // F8
            break;
            
        case 3:  // F9-F12 (instruction byte: 1010DDDD)
            fn_cmd.data[fn_cmd.length++] = 0xA0 | 
                                          (functions[9] ? 0x01 : 0) |   // F9
                                          (functions[10] ? 0x02 : 0) |  // F10
                                          (functions[11] ? 0x04 : 0) |  // F11
                                          (functions[12] ? 0x08 : 0);   // F12
            break;
            
        case 4:  // F13-F20 (instruction: 11011110 DDDDDDDD)
            fn_cmd.data[fn_cmd.length++] = 0xDE;
            fn_cmd.data[fn_cmd.length++] = (functions[13] ? 0x01 : 0) |  // F13
                                          (functions[14] ? 0x02 : 0) |  // F14
                                          (functions[15] ? 0x04 : 0) |  // F15
                                          (functions[16] ? 0x08 : 0) |  // F16
                                          (functions[17] ? 0x10 : 0) |  // F17
                                          (functions[18] ? 0x20 : 0) |  // F18
                                          (functions[19] ? 0x40 : 0) |  // F19
                                          (functions[20] ? 0x80 : 0);   // F20
            break;
            
        case 5:  // F21-F28 (instruction: 11011111 DDDDDDDD)
            fn_cmd.data[fn_cmd.length++] = 0xDF;
            fn_cmd.data[fn_cmd.length++] = (functions[21] ? 0x01 : 0) |  // F21
                                          (functions[22] ? 0x02 : 0) |  // F22
                                          (functions[23] ? 0x04 : 0) |  // F23
                                          (functions[24] ? 0x08 : 0) |  // F24
                                          (functions[25] ? 0x10 : 0) |  // F25
                                          (functions[26] ? 0x20 : 0) |  // F26
                                          (functions[27] ? 0x40 : 0) |  // F27
                                          (functions[28] ? 0x80 : 0);   // F28
            break;
            
        default:
            // Invalid group, return empty command
            fn_cmd.length = 0;
            break;
    }
    
    return fn_cmd;
}

const char* PicoDccLoco::getDccExStatus()
{
    // Calculate speed byte (DCC-EX format)
    int speed128 = (speed & 0x7f);
    
    // Map DCC speed to DCC-EX speed. 2-127 in DCC maps to 1-126 in DCC-EX. 0 remains stop
    if (speed > 1)
        speed128 = speed128 - 1;

    speed128 = speed128 + (forward * 128);
    
    // Build function state bitmask (F0-F28 => bits 0-28)
    uint32_t function_mask = 0;
    for (int i = 0; i < 29; i++) {
        if (functions[i]) {
            function_mask |= (1u << i);
        }
    }

    // Format: <l cab slot speedByte functionMask>
    snprintf(dccex_status, sizeof(dccex_status), "<l %d 0 %d %u>", address, speed128, function_mask);
    
    return dccex_status;
}

bool PicoDccLoco::isValid() const
{
    return address != INVALID_LOCO_ADDR;
}
