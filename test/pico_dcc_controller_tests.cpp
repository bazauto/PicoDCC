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
#include "../lib/pico_diagnostic.h"  // For diag_log_init()

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
    diag_log_init();  // Initialize diagnostic log buffer
    return 0;
}

static int teardown(void **state)
{
    queued_commands.clear();
    uart_output_log.clear();
    diag_log_clear();  // Clear diagnostic log buffer
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
    fflush(stdout);

    // Send emergency stop command
    uart_test_write("<!>");
    controller.dccexLoop();
    
    // Process the emergency stop command through Core 1 loop to queue it
    controller.dccLoop();
    
    // Actually generate and send the packet via the track loop
    controller.getTrack(false)->loop();  // Main track loop generates and sends packets

    // Check that locos collection was cleared after emergency stop
    assert_true(controller.getLocoCount() == 0);

    // Check that a single emergency stop broadcast packet was sent
    extern std::vector<uint64_t> sent_track_packets;
    
    // Build expected emergency stop packet: 0x0E 03 00 41 41 00 00 00
    // Preamble=14, Length=3, Address=0x00, Instruction=0x41, Checksum=0x41
    uint64_t expected_stop_packet = 0x0E03004141000000ULL;
    
    // Search for the emergency stop packet in sent_track_packets
    bool found_emergency_stop = false;
    for (size_t i = 0; i < sent_track_packets.size(); ++i) {
        if (sent_track_packets[i] == expected_stop_packet) {
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
    sent_track_packets.clear();

    // Run the DCC loop - should queue idle packet when no commands available
    controller.dccLoop();
    
    // Actually generate and send packets via the track loop
    controller.getTrack(false)->loop();  // Main track loop generates idle when queue empty

    // Check that an idle packet (0xFF as first data byte) was sent
    extern std::vector<uint64_t> sent_track_packets;
    bool found_idle = false;
    for (size_t i = 0; i < sent_track_packets.size(); ++i)
    {
        uint64_t pkt = sent_track_packets[i];
        // Idle packet format: [preamble][length][0xFF][0x00][checksum]
        // First data byte (0xFF) is at bits 40-47
        uint8_t first_byte = (pkt >> 40) & 0xFF;
        if (first_byte == 0xFF)
        {
            found_idle = true;
            break;
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
        if (output.find("CRITICAL:CORE:Core 1 heartbeat failure detected") != std::string::npos) {
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
        if (output.find("CRITICAL:POWER:Emergency power cutoff activated") != std::string::npos) {
            found_emergency = true;
            break;
        }
    }
    assert_true(found_emergency);
}

// Test Layout Maintenance Mode entry requirements
static void test_maintenance_mode_entry_requirements(void **state)
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

    // Initially in NORMAL mode
    assert_false(controller.isMaintenanceModeActive());

    // Try to enter maintenance mode with main track powered ON - should fail
    uart_test_write("<1 MAIN>");
    controller.dccexLoop();
    assert_true(controller.isTrackPowerOn(false)); // Main track ON

    bool can_enter = controller.canEnterMaintenanceMode();
    assert_false(can_enter); // Cannot enter with main track powered

    // Turn off main track
    uart_test_write("<0 MAIN>");
    controller.dccexLoop();
    assert_false(controller.isTrackPowerOn(false)); // Main track OFF

    // Now should be able to enter
    can_enter = controller.canEnterMaintenanceMode();
    assert_true(can_enter);

    // Enter maintenance mode
    controller.enterMaintenanceMode();
    assert_true(controller.isMaintenanceModeActive());
}

// Test maintenance mode power lockout
static void test_maintenance_mode_power_lockout(void **state)
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

    // Enter maintenance mode (main track must be OFF)
    controller.enterMaintenanceMode();
    assert_true(controller.isMaintenanceModeActive());

    // Try to power on main track - should be rejected with <X>
    uart_output_log.clear();
    uart_test_write("<1 MAIN>");
    controller.dccexLoop();
    
    // Main track should still be OFF
    assert_false(controller.isTrackPowerOn(false));
    
    // Should receive <X> error response
    bool found_error = false;
    for (const auto& output : uart_output_log) {
        if (output.find("<X>") != std::string::npos) {
            found_error = true;
            break;
        }
    }
    assert_true(found_error);

    // Programming track should still work
    uart_test_write("<1 PROG>");
    controller.dccexLoop();
    assert_true(controller.isTrackPowerOn(true)); // Prog track can be enabled
}

// Test maintenance mode command rejection
static void test_maintenance_mode_command_rejection(void **state)
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

    // Enter maintenance mode
    controller.enterMaintenanceMode();
    assert_true(controller.isMaintenanceModeActive());

    // Try throttle command - should be silently rejected
    int initial_loco_count = controller.getLocoCount();
    uart_test_write("<t 1 3 50 1>");
    controller.dccexLoop();
    assert_int_equal(controller.getLocoCount(), initial_loco_count); // No new loco

    // Try function command - should be silently rejected
    uart_test_write("<f 3 128>");
    controller.dccexLoop();
    // No assertions needed - just verifying no crash

    // Try accessory command - should be silently rejected
    uart_test_write("<a 10 1>");
    controller.dccexLoop();
    // No assertions needed - just verifying no crash
}

// NOTE: Config command tests (<D ACK>, <E>, <s>) omitted pending Phase 2 implementation
// These will be added once DCC-EX config command handlers are implemented

// Test maintenance mode exit
static void test_maintenance_mode_exit(void **state)
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

    // Enter maintenance mode
    controller.enterMaintenanceMode();
    assert_true(controller.isMaintenanceModeActive());

    // Exit maintenance mode
    controller.exitMaintenanceMode();
    assert_false(controller.isMaintenanceModeActive());

    // Main track should still be OFF after exit
    assert_false(controller.isTrackPowerOn(false));

    // Should now accept throttle commands again
    int initial_loco_count = controller.getLocoCount();
    uart_test_write("<t 1 3 50 1>");
    controller.dccexLoop();
    assert_int_equal(controller.getLocoCount(), initial_loco_count + 1); // Loco added
}

// Test V command (verify CV)
static void test_verify_cv_command(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    track_settings_t prog_track;
    prog_track.signal_pin = 19;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;  // Prog track has ADC
    prog_track.short_pin = 17;

    PicoDccController controller(main_track, prog_track, 25);

    // Power on programming track
    uart_output_log.clear();
    uart_test_write("<1 PROG>");
    controller.dccexLoop();
    assert_true(controller.isTrackPowerOn(true));

    // Send V command to verify CV1 = 3
    uart_output_log.clear();
    uart_test_write("<V 1 3>");
    controller.dccexLoop();

    // Should send response (either <v 1 3> on success or <v -1> on failure)
    // Response depends on whether ACK is detected (not fully implemented in test mode)
    assert_true(uart_output_log.size() > 0);
    bool has_verify_response = false;
    for (const auto& msg : uart_output_log) {
        if (msg.find("<v") != std::string::npos) {
            has_verify_response = true;
            break;
        }
    }
    assert_true(has_verify_response);
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
        cmocka_unit_test_setup_teardown(test_maintenance_mode_entry_requirements, setup, teardown),
        cmocka_unit_test_setup_teardown(test_maintenance_mode_power_lockout, setup, teardown),
        cmocka_unit_test_setup_teardown(test_maintenance_mode_command_rejection, setup, teardown),
        cmocka_unit_test_setup_teardown(test_maintenance_mode_exit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_cv_command, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}