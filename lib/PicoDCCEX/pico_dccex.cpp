// Auto-generated file
#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#endif
#include <string>
#include <functional>
#include "pico_dccex.h"
#include "../dccex_communication.h"
#include "../pico_diagnostic.h"

PicoDccEx::PicoDccEx(int maxCab)
{
    maxSupportedCabs = maxCab;

    // currentPacket and bufferLength are initialised at their declarations, and
    // repeated here because the constructor sets every other member and so reads
    // as the place initialisation happens. reset() does
    // `if (currentPacket) delete currentPacket;`, and the safety of that delete
    // rests entirely on currentPacket having started as nullptr -- which is not
    // obvious from a constructor that never mentions it (#38).
    currentPacket = nullptr;
    bufferLength = 0;

    setup_default_uart();
    DCCEX_RESPONSE(PICODCC_IDENTITY);
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
                // A '<' with no closing '>' within COMMAND_BUFFER_SIZE. Everything
                // typed since that '<' has been swallowed, including any complete
                // commands that followed it, so this is exactly when the host most
                // needs telling -- silence here is indistinguishable from a hang.
                DCCEX_RESPONSE("<X>");
                LOG_WARNING(COMPONENT_DCCEX, "Command discarded: no terminator within buffer");
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
                        // Note: <s> and <#> are now handled in PicoDCCController for enhanced status
                        if (currentPacket->isVersionCommand() || currentPacket->isNumCabsCommand())
                        {
                            // Let controller handle these for full status reporting
                            *packet = *currentPacket->getPacketData();
                            reset();
                            return true;
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
                        // The single choke point for every command that fails
                        // validatePacket(): out-of-range throttle, function or
                        // accessory parameters, a <D> subcommand that is not a
                        // valid ACK tuning value, and every unsupported opcode
                        // (which is what <S> and any JMRI query fall through to).
                        //
                        // <X> is the DCC-EX generic rejection, and is what real
                        // DCC-EX answers here. Silence is the worse failure: a
                        // headless host cannot tell it apart from a dropped
                        // command or a hung station (#4).
                        DCCEX_RESPONSE("<X>");
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
