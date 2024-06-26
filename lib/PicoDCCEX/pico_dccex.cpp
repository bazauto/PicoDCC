#include <string.h>

#include "pico_dccex.h"

PicoDccEx::PicoDccEx(int maxCab)
{
    maxSupportedCabs = maxCab;

    setup_default_uart();
    uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");
}

void PicoDccEx::reset()
{
    bufferLength = 0;
    memset(buffer, 0, COMMAND_BUFFER_SIZE);
    processState = DCCEX_IDLE;
}

void PicoDccEx::loop(queue_t *dcc_cmd_queue, queue_t *dccex_cmd_queue)
{
    if (!uart_is_readable(uart0))
    {
        return;
    }

    PicoDccExPacket *packet;
    if (queue_try_remove(dccex_cmd_queue, packet))
    {
        if (packet->isThrottleCommand() || packet->isFunctionCommand())
            uart_puts(uart0, packet->getDccExCabUpdate());

        if (packet->isPowerCommand())
            uart_puts(uart0, packet->getDccExPowerUpdate());
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
                // End of the packet, decode the packet
                currentPacket = new PicoDccExPacket((char *)&buffer);
                if (currentPacket->isValid())
                {
                    processState = DCCEX_PACKET;
                }
                else
                {
                    delete currentPacket;
                    currentPacket = NULL;
                }

                break;
            }
            buffer[bufferLength++] = newChar;
        }
        break;
    }

    if (processState == DCCEX_PACKET)
    {
        if (currentPacket->isVersionCommand())
        {
            uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");
        }
        else if (currentPacket->isNumCabsCommand())
        {
            char s[10];
            snprintf(s, sizeof(s), "<# %d>", maxSupportedCabs);
            uart_puts(uart0, s);
        }
        else
        {
            queue_add_blocking(dcc_cmd_queue, currentPacket);
        }

        // Clear out the packet now we have processed it
        delete currentPacket;
        currentPacket = NULL;

        processState = DCCEX_IDLE;
    }
}
