#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdexcept>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCEX/pico_dccex.h"

extern "C" {
void __wrap_setup_default_uart();
void __wrap_setup_default_uart() {
    function_called();
}

void __wrap_uart_puts(void *uart, const char *str);
void __wrap_uart_puts(void *uart, const char *str) {
    check_expected_ptr(uart);
    check_expected_ptr(str);
}
}

void test_create(void **state)
{
    expect_function_call(__wrap_setup_default_uart);

    expect_memory(__wrap_uart_puts, uart, uart0, sizeof(void *));
    expect_string(__wrap_uart_puts, str, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");

    PicoDccEx dccex(10);

    assert_int_equal(dccex.getProcessState(), DCCEX_IDLE);
    assert_int_equal(dccex.getMaxSupportedCabs(), 10);
}

// void test_reset(void **state)
// {
//     PicoDccEx dccex(10);
//     expect_function_call(setup_default_uart);

//     dccex.reset();
//     assert_int_equal(dccex.getProcessState(), DCCEX_IDLE);
// }

// void test_process_dcc_from_controller(void **state)
// {
//     queue_t dccex_cmd_queue;
//     queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
//     PicoDccEx dccex(10);

//     // Mock a valid packet
//     pico_dccex_packet packet = { /* fill with valid data */ };
//     queue_try_add(&dccex_cmd_queue, &packet);

//     dccex.processDccFromController(&dccex_cmd_queue);
//     // Add assertions based on expected behavior
// }

// void test_process_dcc_ex_from_jmri(void **state)
// {
//     queue_t dcc_cmd_queue;
//     queue_init(&dcc_cmd_queue, sizeof(pico_dccex_packet), 10);
//     PicoDccEx dccex(10);

//     // Mock a valid packet
//     char buffer[] = "<valid_packet>";
//     for (char c : buffer) {
//         //uart_putc(uart0, c);
//     }

//     dccex.processDccExFromJMRI(&dcc_cmd_queue);
//     // Add assertions based on expected behavior
// }

// void test_loop(void **state)
// {
//     queue_t dcc_cmd_queue;
//     queue_init(&dcc_cmd_queue, sizeof(pico_dccex_packet), 10);
//     queue_t dccex_cmd_queue;
//     queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
//     PicoDccEx dccex(10);

//     dccex.loop(&dcc_cmd_queue, &dccex_cmd_queue);
//     // Add assertions based on expected behavior
// }

int main(int argc, char *argv[])
{
    printf("Running Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create)
        //cmocka_unit_test(test_reset)
        //cmocka_unit_test(test_process_dcc_from_controller)
        //cmocka_unit_test(test_process_dcc_ex_from_jmri)
        //cmocka_unit_test(test_loop)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}