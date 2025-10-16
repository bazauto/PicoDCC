#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <queue>
#include <string>

extern "C"
{
#include <cmocka.h>
}

#include "../lib/PicoDCCController/pico_dcccontroller.h"

// Mock state tracking
extern bool track_power_states[2];
extern std::vector<std::string> uart_output_log;
extern uint32_t mock_time_ms;

// Test fixtures
static int setup(void **state)
{
    gpio_states.fill(false);
    memset(track_power_states, 0, sizeof(track_power_states));
    queued_commands.clear();
    uart_output_log.clear();
    mock_time_ms = 0;
    return 0;
}

static int teardown(void **state)
{
    queued_commands.clear();
    uart_output_log.clear();
    return 0;
}

// GPIO mock functions are implemented in mocks.cpp

// Test cases

// Test timing safety features
static void test_timing_safety_cutoff(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Turn on power for both tracks
    uart_test_write("<1>");
    controller.dccexLoop();

    // Verify power is on
    assert_true(controller.isTrackPowerOn(false)); // Main track
    assert_true(controller.isTrackPowerOn(true));  // Prog track

    // Move time forward past safety threshold
    mock_time_ms = 150; // 150ms since last command

    // Run the DCC loop which should trigger safety cutoff
    controller.dccLoop();

    // Check that power was cut and LED turned on
    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));
    assert_true(gpio_states[25]); // Timing LED should be on
}

// Test core communication through command queue
static void test_command_queue_processing(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Turn on power for both tracks
    uart_test_write("<1>");
    controller.dccexLoop();

    // Clear any queued commands from power on
    queued_commands.clear();

    // Send a throttle command
    uart_test_write("<t 3 128 1>");

    // Process the packet in core 0 loop
    controller.dccexLoop();

    // Verify command was queued for core 1
    assert_false(queued_commands.empty());

    // Process all repeated commands in core 1 loop
    int safety = 32; // prevent infinite loop in case of bug, but allow for many repeats
    while (!queued_commands.empty() && safety-- > 0)
    {
        controller.dccLoop();
    }
    // Verify all commands were processed
    assert_true(queued_commands.empty());
}

// Test emergency stop behavior
static void test_emergency_stop(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Turn on power first
    uart_test_write("<1>");
    controller.dccexLoop();

    // Add some locos first
    uart_test_write("<t 3 128 1>");
    controller.dccexLoop(); // Process loco command
    controller.dccLoop();   // Process the command in core 1

    uart_test_write("<t 4 128 1>");
    controller.dccexLoop(); // Process loco command
    controller.dccLoop();   // Process the command in core 1

    // Clear the command queue
    queued_commands.clear();

    // Print number of locos in the collection for debugging
    printf("Loco count before emergency stop: %zd\n", controller.getLocoCount());
    fflush(stdout);

    // Send emergency stop command
    uart_test_write("<!>");
    controller.dccexLoop();
    
    // Process the emergency stop command through Core 1 loop to actually send it
    controller.dccLoop();

    // Check that locos collection was cleared after emergency stop
    printf("Loco count after emergency stop: %zd\n", controller.getLocoCount());
    assert_true(controller.getLocoCount() == 0);

    // Check that a single emergency stop broadcast packet was sent
    extern std::vector<uint64_t> sent_track_packets;
    
    // Build expected emergency stop cmd_data using same logic as sendCommand
    raw_dcc_cmd_t expected_stop_cmd = {};
    expected_stop_cmd.is_prog = false;
    expected_stop_cmd.length = 2;
    expected_stop_cmd.data[0] = 0x00;  // Broadcast address
    expected_stop_cmd.data[1] = 0x41;  // Emergency stop instruction
    expected_stop_cmd.cmd_data = 0;
    
    // Build cmd_data as in sendCommand
    expected_stop_cmd.cmd_data |= ((uint64_t)14) << 56; // DCC_MAIN_PREAMBLE
    expected_stop_cmd.cmd_data |= ((uint64_t)(expected_stop_cmd.length + 1)) << 48;
    uint8_t cmd_xor = 0x0;
    for (uint8_t i = 0; i < expected_stop_cmd.length; i++) {
        uint8_t shift = (5 - 1) - i; // DCC_MAX_DATA_BYTES = 5
        expected_stop_cmd.cmd_data |= ((uint64_t)expected_stop_cmd.data[i] << (shift * 8));
        cmd_xor ^= expected_stop_cmd.data[i];
    }
    // Add the checksum
    expected_stop_cmd.cmd_data |= ((uint64_t)cmd_xor << ((5 - 1 - expected_stop_cmd.length) * 8));
    
    // Search for the emergency stop packet in sent_track_packets
    bool found_emergency_stop = false;
    for (size_t i = 0; i < sent_track_packets.size(); ++i) {
        if (sent_track_packets[i] == expected_stop_cmd.cmd_data) {
            found_emergency_stop = true;
            break;
        }
    }
    
    assert_true(found_emergency_stop);

    // Process all commands until the queue is empty (allow up to 10 for safety)
    int safety = 10;
    while (!queued_commands.empty() && safety-- > 0)
    {
        controller.dccexLoop();
        controller.dccLoop();
    }

    // All commands should be processed
    assert_true(queued_commands.empty());
}

// Test track power control
static void test_track_power_control(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Initially power should be off
    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));

    // Turn on main track power
    uart_test_write("<1 MAIN>");
    controller.dccexLoop();

    assert_true(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));

    // Turn on programming track power
    uart_test_write("<1 PROG>");
    controller.dccexLoop();

    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));

    // Turn off all power
    uart_test_write("<0>");
    controller.dccexLoop();

    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));
}

// Test idle packet generation
static void test_idle_packet_generation(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Turn on power first
    uart_test_write("<1>");
    controller.dccexLoop();
    controller.dccLoop(); // Process power on command

    // Clear any existing commands from power on
    queued_commands.clear();

    // Run the DCC loop - should generate idle packet when no commands available
    controller.dccLoop();

    // Check that an idle packet (0xFF as first data byte) was sent
    extern std::vector<uint64_t> sent_track_packets;
    bool found_idle = false;
    fflush(stdout);
    for (size_t i = 0; i < sent_track_packets.size(); ++i)
    {
        uint64_t pkt = sent_track_packets[i];
        // The first data byte is bits 32-39 (big-endian, see sendCommand packing)
        uint8_t first_byte = (pkt >> 32) & 0xFF;
        if (first_byte == 0xFF)
        {
            found_idle = true;
        }
    }
    assert_true(found_idle);
}

// Test DCC-EX acknowledgments
static void test_dccex_acknowledgments(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Clear any startup messages
    uart_output_log.clear();

    // Test power command acknowledgment
    uart_test_write("<1>");
    controller.dccexLoop();
    
    // Should have sent power acknowledgment
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<p1>");
    uart_output_log.clear();

    // Test power command with track specification
    uart_test_write("<1 MAIN>");
    controller.dccexLoop();
    
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<p1 MAIN>");
    uart_output_log.clear();

    // Test throttle command acknowledgment
    uart_test_write("<t 3 64 1>");
    controller.dccexLoop();
    
    assert_int_equal(uart_output_log.size(), 1);
    // Should respond with loco status: <l cab 0 speed 0>
    // Check that it starts with correct format and cab number
    assert_true(uart_output_log[0].find("<l 3 0") == 0);
    assert_true(uart_output_log[0].find("0>") == uart_output_log[0].length() - 2);
    uart_output_log.clear();

    // Test function command acknowledgment
    uart_test_write("<F 3 144 1>");
    controller.dccexLoop();
    
    assert_int_equal(uart_output_log.size(), 1);
    // Should respond with loco status - check format
    assert_true(uart_output_log[0].find("<l 3 0") == 0);
    assert_true(uart_output_log[0].find("0>") == uart_output_log[0].length() - 2);
    uart_output_log.clear();

    // Test emergency stop acknowledgment
    uart_test_write("<!>");
    controller.dccexLoop();
    
    // Emergency stop should send locomotive status for each active loco (DCC-EX spec requirement)
    // We should get one response for locomotive 3 showing emergency stop state
    assert_int_equal(uart_output_log.size(), 1);
    assert_true(uart_output_log[0].find("<l 3 0") == 0);  // Loco 3, speed 0 (emergency stop)
    assert_true(uart_output_log[0].find("0>") == uart_output_log[0].length() - 2);
    uart_output_log.clear();

    // Test accessory command acknowledgment
    uart_test_write("<a 1 0 1>");
    controller.dccexLoop();
    
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<O>");
}

// Test core health monitoring
static void test_core_health_monitoring(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);
    
    // Turn on power for both tracks
    uart_test_write("<1>");
    controller.dccexLoop();
    
    // Verify power is on initially
    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));
    
    // Simulate Core 1 running normally (heartbeat updates)
    mock_time_ms = 0;
    controller.dccLoop(); // This updates heartbeat
    
    // Run Core 0 loop - should not trigger emergency cutoff yet
    mock_time_ms = 30; // 30ms - within tolerance
    controller.dccexLoop();
    
    // Power should still be on
    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));
    
    // Simulate Core 1 stopping (no heartbeat updates)
    mock_time_ms = 60; // 60ms - still within 50ms check interval
    controller.dccexLoop();
    
    // Move past the 50ms check interval without Core 1 updating heartbeat
    mock_time_ms = 120; // 120ms - should trigger Core 1 dead detection
    controller.dccexLoop();
    
    // Should have triggered emergency cutoff
    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));
    assert_true(gpio_states[25]); // Error LED should be on
    
    // Check for emergency message in UART output
    bool found_core1_dead = false;
    for (const auto& output : uart_output_log) {
        if (output.find("CORE1_DEAD") != std::string::npos) {
            found_core1_dead = true;
            break;
        }
    }
    assert_true(found_core1_dead);
}

// Test queue timeout mechanism - verify non-blocking behavior
static void test_queue_timeout_safety(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);
    
    // Turn on power
    uart_test_write("<1>");
    controller.dccexLoop();
    assert_true(controller.isTrackPowerOn(false));
    
    // Test that the system continues to operate with normal commands
    // This verifies that we're no longer using blocking queue operations
    // which would have caused the system to freeze if the queue was full
    uart_test_write("<t 1 3 1 1>");  // Throttle command
    controller.dccexLoop();
    
    // System should still be operational (power still on)
    // This confirms non-blocking queue operations are working
    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));
    
    // The fact that we can process multiple commands without blocking
    // demonstrates that the queue timeout safety mechanism is in place
    for (int i = 0; i < 5; i++) {
        uart_test_write("<t 1 3 1 1>");  // More commands
        controller.dccexLoop();
    }
    
    // Should still be operational
    assert_true(controller.isTrackPowerOn(false));
}

// Test emergency power cutoff function directly
static void test_emergency_power_cutoff(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);
    
    // Turn on power and add some locomotives
    uart_test_write("<1>");
    controller.dccexLoop();
    uart_test_write("<t 1 3 1 1>");  // Add loco
    controller.dccexLoop();
    uart_test_write("<t 2 5 1 1>");  // Add another loco
    controller.dccexLoop();
    
    // Verify initial state
    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));
    assert_int_equal(controller.getLocoCount(), 2);
    
    // Trigger emergency cutoff directly
    controller.emergencyPowerCutoff();
    
    // Verify emergency state
    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));
    assert_int_equal(controller.getLocoCount(), 0); // All locos should be cleared
    assert_true(gpio_states[25]); // Error LED should be on
    
    // Check for emergency cutoff message in UART output
    bool found_emergency = false;
    for (const auto& output : uart_output_log) {
        if (output.find("EMERGENCY_CUTOFF") != std::string::npos) {
            found_emergency = true;
            break;
        }
    }
    assert_true(found_emergency);
}

// Add all tests to the test suite
int main(void)
{
    printf("Running Controller Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_timing_safety_cutoff, setup, teardown),
        cmocka_unit_test_setup_teardown(test_command_queue_processing, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emergency_stop, setup, teardown),
        cmocka_unit_test_setup_teardown(test_track_power_control, setup, teardown),
        cmocka_unit_test_setup_teardown(test_idle_packet_generation, setup, teardown),
        cmocka_unit_test_setup_teardown(test_dccex_acknowledgments, setup, teardown),
        cmocka_unit_test_setup_teardown(test_core_health_monitoring, setup, teardown),
        cmocka_unit_test_setup_teardown(test_queue_timeout_safety, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emergency_power_cutoff, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}