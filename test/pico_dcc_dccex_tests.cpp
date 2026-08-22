#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <cmocka.h>
}

#include <cstring>
#include "../lib/PicoDCCEX/pico_dccex.h"
#include "../lib/dccex_communication.h"

// Use direct mocking like other tests
extern std::vector<std::string> uart_output_log;

// Test fixtures
static int setup(void **state)
{
    uart_output_log.clear();
    return 0;
}

static int teardown(void **state)
{
    uart_output_log.clear();
    return 0;
}

void test_create(void **state)
{
    PicoDccEx dccex(10);

    // Verify that the constructor sent the startup message
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), PICODCC_IDENTITY);

    // The startup banner used to be a literal left over from upstream DCC-EX,
    // announcing an Arduino Mega with a standard motor shield. It reached JMRI
    // and it was believable, which is what made it dangerous rather than untidy.
    assert_non_null(strstr(uart_output_log[0].c_str(), "PICODCC"));
    assert_null(strstr(uart_output_log[0].c_str(), "MEGA"));
    assert_null(strstr(uart_output_log[0].c_str(), "STANDARD_MOTOR_SHIELD"));

    assert_int_equal(dccex.getProcessState(), DCCEX_IDLE);
    assert_int_equal(dccex.getMaxSupportedCabs(), 10);
}

void test_reset(void **state)
{
    PicoDccEx dccex(10);

    // Clear startup messages
    uart_output_log.clear();

    dccex.reset();
    assert_int_equal(dccex.getProcessState(), DCCEX_IDLE);
    
    // Reset should not generate additional UART output
    assert_int_equal(uart_output_log.size(), 0);
}

void test_process_command(void **state)
{
    PicoDccEx dccex(10);
    pico_dccex_packet packet;

    // Clear startup messages
    uart_output_log.clear();

    // Test that process returns false when no data
    assert_false(dccex.processCommand(&packet));
    
    assert_int_equal(dccex.getProcessState(), DCCEX_IDLE);
    
    // Should not generate UART output when no commands processed
    assert_int_equal(uart_output_log.size(), 0);
}



int main(int argc, char *argv[])
{
    printf("Running Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reset, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_command, setup, teardown)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}