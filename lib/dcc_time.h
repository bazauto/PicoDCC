/*
    Millisecond clock.

    There is exactly one correct way to get a millisecond timestamp in this
    firmware, and this is it. It lives in its own header so the broken idiom
    cannot be copied into the next component that needs a timer.

    Unsigned delta arithmetic (`now - then`) survives counter wrap only when the
    counter wraps at 2^32. `time_us_32()` does -- but `time_us_32() / 1000` does
    not: dividing first produces a value that wraps at 4,294,967, i.e. every 71.6
    minutes of uptime. Across that boundary `now - then` no longer yields a small
    positive delta, it yields roughly 2^32, and every timeout in the firmware
    fires at once. That was issue #32: false "DCC timing violation" entries with
    both tracks powered off and left off, observed in operation.

    `to_ms_since_boot(get_absolute_time())` reads the 64-bit hardware timer and
    returns 32-bit milliseconds, so it wraps at 49.7 days and its deltas are
    correct across that wrap. It is a latched read of TIMERAWH/TIMERAWL and is
    just as multicore-safe as `time_us_32()` -- the comment that motivated the
    original change ("Multicore-safe timer") was true of both.

    Deltas remain correct across the 49.7-day wrap. Absolute comparisons against
    a stored timestamp are not, so keep using differences.
*/
#ifndef PICO_DCC_TIME_H
#define PICO_DCC_TIME_H

#include <cstdint>

#ifdef TEST_BUILD
#include "../test/mocks.h"
#else
#include <pico/time.h>
#endif

static inline uint32_t dcc_millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

#endif
