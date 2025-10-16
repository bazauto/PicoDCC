#ifndef PICO_DCC_DCCEX_COMMUNICATION_H
#define PICO_DCC_DCCEX_COMMUNICATION_H

// DCC-EX Protocol Communication Interface
// This abstracts the UART communication for sending DCC-EX protocol responses
// and acknowledgments back to JMRI or other DCC-EX compatible software.
//
// All DCC-EX protocol messages (power status, locomotive updates, acknowledgments)
// should use these macros to ensure consistent behavior between test and hardware modes.

#ifdef TEST_BUILD
#include "../test/mocks.h"
#define DCCEX_RESPONSE(msg) uart_puts(uart0, msg)
#else
#include <pico/stdio.h>
#define DCCEX_RESPONSE(msg) printf("%s", msg)
#endif

#endif // PICO_DCC_DCCEX_COMMUNICATION_H