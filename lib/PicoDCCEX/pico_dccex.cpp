#include <string.h>

#include "pico_dccex.h"

PicoDccEx::PicoDccEx(int maxCab)
{
    maxSupportedCabs = maxCab;

    setup_default_uart();
    uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");
    processState = DCCEX_IDLE;
}

void PicoDccEx::reset()
{
    if (currentPacket)
    {
        delete currentPacket;
        currentPacket = NULL;
    }

    bufferLength = 0;
    memset(buffer, 0, COMMAND_BUFFER_SIZE);
    processState = DCCEX_IDLE;
}

void PicoDccEx::processDccFromController(queue_t *dccex_cmd_queue)
{

    // Skip if we can't send to JRMI
    if (!uart_is_writable(uart0))
    {
        return;
    }

    // Process any incoming messages form the controller to be sent to JMRI
    pico_dccex_packet rawPacket;
    if (queue_try_remove(dccex_cmd_queue, &rawPacket))
    {
        PicoDccExPacket packet(rawPacket);
        if (packet.isValid())
        {
            if (packet.isThrottleCommand() || packet.isFunctionCommand())
                uart_puts(uart0, packet.getDccExCabUpdate()->text);

            if (packet.isPowerCommand())
                uart_puts(uart0, packet.getDccExPowerUpdate()->text);
        }
    }
}

void PicoDccEx::processDccExFromJMRI(queue_t *dcc_cmd_queue)
{

    if (!uart_is_readable(uart0))
    {
        return;
    }

    // Process any incoming messages from JMRI
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
                currentPacket = new PicoDccExPacket(buffer);
                if (currentPacket->isValid())
                {
                    processState = DCCEX_PACKET;
                }
                else
                {
                    reset();
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
            printf("Adding command to queue");
            if (!queue_try_add(dcc_cmd_queue, currentPacket->getPacketData()))
            {
                uart_puts(uart0, "<X>");
            }
            printf("Added command to queue");
        }

        reset();
    }
}

void PicoDccEx::loop(queue_t *dcc_cmd_queue, queue_t *dccex_cmd_queue)
{

    processDccFromController(dccex_cmd_queue);
    processDccExFromJMRI(dcc_cmd_queue);
}
