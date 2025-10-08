// Auto-generated file
#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#endif
#include <string>
#include <functional>
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

bool PicoDccEx::processCommand(pico_dccex_packet* packet)
{
    while (uart_is_readable(uart0))
    {
        char newChar = uart_getc(uart0);
        
        switch (processState)
        {
        case (DCCEX_IDLE):
            if (newChar == '<')
            {
                processState = DCCEX_RECIVING;
                bufferLength = 0; // Clear the buffer for new command
                memset(buffer, 0, COMMAND_BUFFER_SIZE);
            }
            break;

        case (DCCEX_RECIVING):
            if (bufferLength >= COMMAND_BUFFER_SIZE)
            {
                reset();
                return false;
            }
            else
            {
                if (newChar == '>')
                {
                    buffer[bufferLength] = '\0'; // Null-terminate the string
                    currentPacket = new PicoDccExPacket(buffer);
                    if (currentPacket->isValid())
                    {
                        processState = DCCEX_PACKET;

                        // Process packet immediately
                        if (currentPacket->isVersionCommand())
                        {
                            uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");
                            reset();
                            return false;
                        }
                        else if (currentPacket->isNumCabsCommand())
                        {
                            char s[10];
                            snprintf(s, sizeof(s), "<# %d>", maxSupportedCabs);
                            uart_puts(uart0, s);
                            reset();
                            return false;
                        }
                        else if (currentPacket->isPowerCommand())
                        {
                            *packet = *currentPacket->getPacketData();
                            reset();
                            return true;
                        }
                        else
                        {
                            *packet = *currentPacket->getPacketData();
                            reset();
                            return true;
                        }
                    }
                    else
                    {
                        reset();
                        return false;
                    }
                }
                else
                {
                    buffer[bufferLength++] = newChar;
                }
            }
            break;
        }
    }
    
    return false;
}
