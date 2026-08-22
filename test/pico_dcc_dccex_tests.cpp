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



// ---------------------------------------------------------------------------
// Rejected commands answer <X> (issue #4)
//
// A headless host cannot see the LCD. A rejected command that draws no reply is
// indistinguishable from one that worked, from one that was lost in transit, and
// from a hung station. <X> is the DCC-EX generic rejection, and is what real
// DCC-EX answers here.
// ---------------------------------------------------------------------------

// Feed a complete <...> command in and return what went out on the UART.
static std::vector<std::string> replies_to(PicoDccEx &dccex, const char *command)
{
    pico_dccex_packet packet;
    uart_output_log.clear();
    uart_test_write(command);
    dccex.processCommand(&packet);
    return uart_output_log;
}

static void assert_rejected(PicoDccEx &dccex, const char *command)
{
    std::vector<std::string> out = replies_to(dccex, command);
    assert_int_equal(out.size(), 1);
    assert_string_equal(out[0].c_str(), "<X>");
}

void test_out_of_range_throttle_is_rejected_on_the_wire(void **state)
{
    PicoDccEx dccex(10);
    uart_output_log.clear();

    assert_rejected(dccex, "<t 0 126 1>");      // cab 0 is the broadcast address
    assert_rejected(dccex, "<t 65535 126 1>");  // above the 14-bit address space
    assert_rejected(dccex, "<t 3 200 1>");      // speed above 126
    assert_rejected(dccex, "<t 3 -2 1>");       // below the -1 estop sentinel
}

void test_out_of_range_function_is_rejected_on_the_wire(void **state)
{
    PicoDccEx dccex(10);
    uart_output_log.clear();

    assert_rejected(dccex, "<F 0 1 1>");
    assert_rejected(dccex, "<F 65535 1 1>");
}

// The 4-field form is deprecated in DCC-EX and deliberately not implemented.
// What matters is that it is *rejected* rather than misparsed: read as three
// fields it becomes cab=REGISTER, speed=CAB, and commands a locomotive nobody
// addressed. Rejection depends on the real address exceeding the speed limit,
// which is why the guard and the reply matter more than the parse.
void test_deprecated_four_field_throttle_is_rejected(void **state)
{
    PicoDccEx dccex(10);
    uart_output_log.clear();

    assert_rejected(dccex, "<t 1 3000 50 1>");
}

// Unsupported opcodes fall through validatePacket()'s default case. <S> is the
// one that already existed -- it is consumed by the parser but never marked
// valid -- and JMRI's roster/route queries land here too. Answering <X> is
// correct: they genuinely are unsupported.
void test_unsupported_opcodes_are_rejected_on_the_wire(void **state)
{
    PicoDccEx dccex(10);
    uart_output_log.clear();

    assert_rejected(dccex, "<S 1 2 3>");   // sensors: parsed, never supported
    assert_rejected(dccex, "<JA>");        // JMRI roster query
    assert_rejected(dccex, "<Z 1 2 3>");   // unknown opcode
}

// A '<' with no closing '>' swallows everything after it, including any complete
// commands that followed. Silence here is indistinguishable from a hang.
void test_unterminated_command_is_rejected_once_the_buffer_fills(void **state)
{
    PicoDccEx dccex(10);
    pico_dccex_packet packet;
    uart_output_log.clear();

    std::string junk = "<";
    junk.append(COMMAND_BUFFER_SIZE + 10, 'x');   // never terminated
    uart_test_write(junk.c_str());
    dccex.processCommand(&packet);

    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<X>");
}

// The rule is "rejected commands answer <X>", not "commands answer <X>". A valid
// command must not start drawing one.
void test_valid_command_is_not_rejected(void **state)
{
    PicoDccEx dccex(10);
    pico_dccex_packet packet;
    uart_output_log.clear();

    uart_test_write("<t 3 50 1>");
    assert_true(dccex.processCommand(&packet));

    // Accepted at the parser layer: it is handed to the controller, and nothing
    // is written here at all.
    assert_int_equal(uart_output_log.size(), 0);
}

int main(int argc, char *argv[])
{
    printf("Running Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reset, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_out_of_range_throttle_is_rejected_on_the_wire, setup, teardown),
        cmocka_unit_test_setup_teardown(test_out_of_range_function_is_rejected_on_the_wire, setup, teardown),
        cmocka_unit_test_setup_teardown(test_deprecated_four_field_throttle_is_rejected, setup, teardown),
        cmocka_unit_test_setup_teardown(test_unsupported_opcodes_are_rejected_on_the_wire, setup, teardown),
        cmocka_unit_test_setup_teardown(test_unterminated_command_is_rejected_once_the_buffer_fills, setup, teardown),
        cmocka_unit_test_setup_teardown(test_valid_command_is_not_rejected, setup, teardown)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}