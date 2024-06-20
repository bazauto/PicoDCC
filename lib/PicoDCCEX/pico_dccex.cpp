#include <string.h>

#include "pico_dccex.h"

PicoDccEx::PicoDccEx(int maxCab)
{
    maxSupportedCabs = maxCab;

    setup_default_uart();
    uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");
}

void PicoDccEx::setCallback(std::function<void(const PicoDccExPacket *)> callback)
{
    packetCallback = callback;
}

void PicoDccEx::reset()
{
    bufferLength = 0;
    memset(buffer, 0, COMMAND_BUFFER_SIZE);
    processState = DCCEX_IDLE;
}

void PicoDccEx::loop(queue_t *cmd_queue)
{
    if (!uart_is_readable(uart0))
    {
        return;
    }

    char newChar = uart_getc(uart0);
    switch (processState)
    {
    case (DCCEX_IDLE):
        if (newChar == '<')
        {
            processState = DCCEX_RECIVING;
        }
        break;

    case (DCCEX_RECIVING):
        if (bufferLength >= COMMAND_BUFFER_SIZE)
        {
            // out of space in the buffer ditch the packet and return to idle
            reset();
        }
        else
        {
            if (newChar == '>')
            {
                // End of the packet, decode the packate
                currentPacket = new PicoDccExPacket((char *)&buffer);
                if (currentPacket->isValid())
                {
                    // Add message to DCC queue

                }
                delete currentPacket;
                break;
            }
            buffer[bufferLength++] = newChar;
        }
        break;
    }
}
