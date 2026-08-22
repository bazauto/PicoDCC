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
// The BUILD date is a hardcoded literal and does not track the actual build --
// see the follow-up issue. It is at least wrong in only one place now.
#define PICODCC_IDENTITY "<iDCC-EX V-5.0.0 / PICODCC / BUILD Oct 20 2025>"

#endif // PICO_DCC_DCCEX_COMMUNICATION_H