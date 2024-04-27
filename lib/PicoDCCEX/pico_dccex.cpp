#include <string.h>

#include "pico_dccex.h"

PicoDccExPacket::PicoDccExPacket(int maxCab)
{
    maxSupportedCabs = maxCab;
}

void PicoDccExPacket::reset()
{
    bufferLength = 0;
    memset(buffer, 0, COMMAND_BUFFER_SIZE);
    processState = IDLE;
}

void PicoDccExPacket::processInput(char chr)
{
    switch(processState) {
        case(IDLE):
            if (chr == '<') {
                processState = IN_PACKET;
            }
            break;

        case(IN_PACKET):
            if (bufferLength >= COMMAND_BUFFER_SIZE) {
                // out of space in the buffer ditch the packet and return to idle
                reset();
            } else {
                if (chr == '>') {
                    // End of the packet, decode the packate
                    decodePacket();
                    break;
                }
                buffer[bufferLength++] = chr;
            }
            break;
    }
};

void PicoDccExPacket::decodePacket()
{
    // The packet to decode will be stored in the buffer and the output should go in currentMessage
    opcode = buffer[0];

    // Decode the rest of the pack if there are params
    switch(opcode)
    {
        // Track Power
        case('0'):
        case('1'):
            processState = PACKET_WAITING;
            break;

        //Version info
        case('s'):
            // Hard code response for now
            //printf("<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>");
            uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>");
            reset();
            break;

        // Max supported cab
        case('#'):
            char s[10];
            snprintf(s, sizeof(s), "<# %d>", maxSupportedCabs);
            uart_puts(uart0, s);
            reset();
            break;

        // Throttle Control and Functions have the same parameter layout
        // <t cab speed direction> and <F cab funct state>
        case('t'):
        case('F'):
            if (sscanf(buffer, "%*c %d %d %d", &cab, &speed_funct, &direction_state) == 3) {
                processState = PACKET_WAITING;
            } else {
                reset();
            }
            break;

        // Anything we don't understand / support we respond with the error response
        default:
            uart_puts(uart0, "<X>");
            reset();
    }
};

