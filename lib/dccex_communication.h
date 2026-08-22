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
#else
#include <hardware/uart.h>
#endif

#define DCCEX_RESPONSE(msg) uart_puts(uart0, msg)

// The command station's identity, sent unprompted at startup and again in reply
// to <s>. Defined once because the two had drifted apart: startup announced
// "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>", a literal left
// over from upstream DCC-EX during bring-up. JMRI reading that would believe it
// was talking to an Arduino Mega with a standard motor shield.
//
// The string itself is generated at build time (cmake/generate_version.cmake) so
// it carries the real build date and git hash. That is what makes the identity
// useful: it tells you which image is actually running on the board.
#include "version.h"

#define PICODCC_IDENTITY PICODCC_VERSION_STRING

#endif // PICO_DCC_DCCEX_COMMUNICATION_H