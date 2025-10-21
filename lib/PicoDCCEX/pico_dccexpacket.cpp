#include "pico_dccexpacket.h"
#include "../pico_diagnostic.h"

PicoDccExPacket::PicoDccExPacket(char *buffer)
{
    // Initialize raw_dcc_cmd to zero
    memset(&raw_dcc_cmd, 0, sizeof(raw_dcc_cmd));
    
    decodePacket(buffer);
    validatePacket();
}

PicoDccExPacket::PicoDccExPacket(pico_dccex_packet packetData) {
    // Initialize raw_dcc_cmd to zero
    memset(&raw_dcc_cmd, 0, sizeof(raw_dcc_cmd));

    // Validate memory alignment
    if (&packetData == nullptr) {
        return;
    }

    packet = packetData;

    validatePacket();
}

void PicoDccExPacket::decodePacket(char *buffer)
{
    // The packet to decode will be stored in the buffer and the decoded output should go in packet
    packet.opcode = buffer[0];

    // Decode the rest of the pack if there are params
    switch (packet.opcode)
    {
    // Track Power
    case ('0'):
    case ('1'):
        packet.power_on = (packet.opcode == '1');
        
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

    // Accessory Decoder
    case ('a'):
        // <a addr subaddr activate>
        if (sscanf(buffer, "%*c %d %d %d", &packet.addr, &packet.param1, &packet.param2) == 3)
        {
            // Validate accessory address range (1-2044 per DCC standard)
            if (packet.addr >= 1 && packet.addr <= 2044 && 
                packet.param1 >= 0 && packet.param1 <= 7 &&
                (packet.param2 == 0 || packet.param2 == 1))
            {
                break;
            }
            else
            {
                LOG_WARNING(COMPONENT_DCCEX, "Invalid 3-param accessory command");
            }
        }

        // <a linear_addr activate>
        packet.param1 = 0; // Reset to avoid getting value from above
        if (sscanf(buffer, "%*c %d %d", &packet.addr, &packet.param2) == 2)
        {
            // Validate linear accessory address and activation state
            if (packet.addr >= 1 && packet.addr <= 2044 &&
                (packet.param2 == 0 || packet.param2 == 1))
            {
                break;
            }
            else
            {
                LOG_WARNING(COMPONENT_DCCEX, "Invalid 2-param accessory command");
            }
        }
        
        packet.addr = -1; // Invalid accessory
        break;

    // Sensors
    case ('S'):
        break;

    // Throttle Control and Functions have the same parameter layout
    // <t cab speed direction> and <F cab funct state>
    case ('t'):
    case ('F'):
        if (sscanf(buffer, "%*c %d %d %d", &packet.addr, &packet.param1, &packet.param2) == 3)
        {
            // Always parse successfully, validation happens in consumer classes
            // This allows for specific error messages based on which field is invalid
            break; // Valid parsing
        }
        // Only mark as invalid if parsing completely failed
        packet.addr = -1; 
        break;

    // Emergency Stop
    case ('!'):
        // No parameters
        break;

    // Config commands
    case ('D'):  // <D ACK ...> commands
        // Parse: <D ACK LIMIT value>, <D ACK MIN value>, <D ACK MAX value>
        {
            char subcommand[16] = {0};
            char param_name[16] = {0};
            int value = 0;
            
            if (sscanf(buffer, "D %15s %15s %d", subcommand, param_name, &value) == 3) {
                // Check for ACK subcommand
                if (strcmp(subcommand, "ACK") == 0) {
                    packet.addr = 1;  // ACK subcommand = 1
                    
                    // Parse parameter type
                    if (strcmp(param_name, "LIMIT") == 0) {
                        packet.param1 = 1;  // LIMIT = 1
                    } else if (strcmp(param_name, "MIN") == 0) {
                        packet.param1 = 2;  // MIN = 2
                    } else if (strcmp(param_name, "MAX") == 0) {
                        packet.param1 = 3;  // MAX = 3
                    } else {
                        packet.addr = -1;  // Invalid parameter name
                        break;
                    }
                    
                    packet.param2 = value;
                } else {
                    packet.addr = -1;  // Unknown subcommand
                }
            } else {
                packet.addr = -1;  // Failed to parse
            }
        }
        break;
        
    case ('E'):  // <E> save command
    case ('s'):  // <s> status command
    case ('#'):  // <#> capacity command
        // These commands have no parameters, just validate opcode
        break;
    }
}

void PicoDccExPacket::validatePacket() {

    valid_packet = false;

    // Here we validate that the packet is supported by this controller
    // And if so it has the required data to perform the operation
    switch (packet.opcode) {
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

    // Config commands with parameters - validated through parsing
    case ('D'):
        // <D ACK ...> commands validated if addr != -1 (successful parse)
        if (packet.addr != -1) {
            // Additional range validation based on parameter type
            int param_type = packet.param1;
            int value = packet.param2;
            
            if (param_type == 1) {  // LIMIT (ACK threshold in mA)
                // NMRA S-9.2.3 specifies 60mA, allow 30-100mA range
                if (value >= 30 && value <= 100) {
                    valid_packet = true;
                }
            } else if (param_type == 2) {  // MIN (duration in microseconds)
                // Minimum ACK pulse: 3000-8000µs (3-8ms)
                if (value >= 3000 && value <= 8000) {
                    valid_packet = true;
                }
            } else if (param_type == 3) {  // MAX (duration in microseconds)
                // Maximum ACK pulse: 6000-10000µs (6-10ms)
                if (value >= 6000 && value <= 10000) {
                    valid_packet = true;
                }
            }
        }
        break;

    // Save command - no parameters
    case ('E'):
        valid_packet = true;
        break;

    // Read address command - no parameters
    case ('R'):
        valid_packet = true;
        break;

    // These opcodes are validated through having successfully parsed parameters
    case ('t'):
    case ('F'):
    case ('a'):
        if (packet.addr != -1) {
            valid_packet = true;
        } 
        break;

    // Emergency Stop
    case ('!'):
        valid_packet = true;
        break;

    default:
        break;
    }
}

raw_dcc_cmd_t *PicoDccExPacket::getRawDccAccessoryCmd()
{
    // Only need to build the command once, if it has already been built then return it
    if (raw_dcc_cmd.length == 0)
    {
        // 10AAAAAA
        // First byte is just a control bit and the 6 LSB bits of the address
        raw_dcc_cmd.data[raw_dcc_cmd.length] = 0x80 | (getAccessoryAddr() & 0x3f);
        raw_dcc_cmd.length++;

        // 1AAACPPG
        // Second byte is the upper 3 bits of the address, the activate bit, subaddress, and gate bit
        raw_dcc_cmd.data[raw_dcc_cmd.length] = 0x80;                                      // Control bit
        raw_dcc_cmd.data[raw_dcc_cmd.length] |= ((getAccessoryAddr() >> 6) & 0x07) << 4;  // Upper 3 bits of address (AAA)
        raw_dcc_cmd.data[raw_dcc_cmd.length] |= 1 << 3;                                   // Activate bit (C)
        raw_dcc_cmd.data[raw_dcc_cmd.length] |= (getAccessorySubAddr() & 0x03) << 1;      // Subaddress / Port (PP)
        raw_dcc_cmd.data[raw_dcc_cmd.length] |= (getAccessoryActivate() & 0x01);          // Gate bit (G)
        raw_dcc_cmd.length++;
        
        // Repeat the command 3 times when sent to the track
        raw_dcc_cmd.repeats = 3;
    }
    return &raw_dcc_cmd;
}

char *PicoDccExPacket::getDccExCabUpdate()
{
    if (strlen(dccex_cab_update) != 0)
    {
        return dccex_cab_update;
    }

    uint8_t speed128 = (getSpeed() & 0x7f);
    int8_t responseSpeed = 0;
    if (speed128 == 1)
        responseSpeed = -1;

    if (speed128 > 1)
        speed128 = speed128 - 1;

    speed128 = speed128 | (getDirection() * 128);

    snprintf(dccex_cab_update, sizeof(dccex_cab_update), "<l %d 0 %d 0>", getCab(), speed128);

    return dccex_cab_update;
}

char *PicoDccExPacket::getDccExPowerUpdate()
{
    if (strlen(dccex_power_update) != 0)
    {
        return dccex_power_update;
    }

    if (getTrack() == DCCEX_TRACK_ALL)
        snprintf(dccex_power_update, sizeof(dccex_power_update), "<p%d>", (packet.power_on ? 1 : 0));

    if (getTrack() == DCCEX_TRACK_MAIN)
        snprintf(dccex_power_update, sizeof(dccex_power_update), "<p%d MAIN>", (packet.power_on ? 1 : 0));

    if (getTrack() == DCCEX_TRACK_PROG)
        snprintf(dccex_power_update, sizeof(dccex_power_update), "<p%d PROG>", (packet.power_on ? 1 : 0));

    return dccex_power_update;
}
