#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

extern "C"
{
#include <cmocka.h>
}

#include "../lib/PicoDCCTrack/pico_dcctrack.h"

// Test state tracking
extern std::array<bool, 30> gpio_states;
extern std::vector<uint64_t> sent_track_packets;
extern uint32_t mock_adc_reading;
extern uint32_t mock_time_ms;

// Test fixtures
static int setup(void **state)
{
    gpio_states.fill(false);
    sent_track_packets.clear();
    mock_adc_reading = 0;
    mock_adc_clear_channels();  // per-channel values must not leak between tests
    mock_reset_pio();
    mock_time_ms = 1000; // Start at 1 second
    return 0;
}

static int teardown(void **state)
{
    sent_track_packets.clear();
    return 0;
}

// Test Cases

// Test track construction and initialization
static void test_track_constructor_main(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings); // Main track

    // Verify basic properties
    assert_false(track.getIsProg());
    assert_false(track.getPower()); // Should start with power off
    assert_int_equal(track.getPowerCtrlPin(), 22);
    assert_int_equal(track.getPowerAdcPin(), 26); // BASE_ADC_PIN + adc_num
    assert_int_equal(track.getPowerAdcNumber(), 0);
    assert_true(track.canReadCurrent());

    // Verify GPIO initialization
    assert_false(gpio_states[22]); // Power control pin should be off initially
    assert_false(gpio_states[16]); // Short LED should be off initially
}

static void test_track_constructor_prog(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 19;
    settings.ctrl_pin = 21;
    settings.adc_num = 1;
    settings.short_pin = 17;

    PicoDccTrack track(true, settings); // Programming track

    // Verify basic properties
    assert_true(track.getIsProg());
    assert_false(track.getPower());
    assert_int_equal(track.getPowerCtrlPin(), 21);
    assert_int_equal(track.getPowerAdcPin(), 27); // BASE_ADC_PIN + adc_num
    assert_int_equal(track.getPowerAdcNumber(), 1);
    assert_true(track.canReadCurrent());
}

static void test_track_constructor_no_adc(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN; // No ADC
    settings.short_pin = UNUSED_PIN; // No short LED

    PicoDccTrack track(false, settings);

    // Verify ADC is disabled
    assert_false(track.canReadCurrent());
}

// Test power control
static void test_power_control(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Initially power should be off
    assert_false(track.getPower());
    assert_false(gpio_states[22]); // Power control pin

    // Turn power on
    track.powerOn();
    assert_true(track.getPower());
    assert_true(gpio_states[22]); // Power control pin should be on
    assert_false(gpio_states[16]); // Short LED should be off

    // Turn power off
    track.powerOff();
    assert_false(track.getPower());
    assert_false(gpio_states[22]); // Power control pin should be off

    // Test setPower directly
    track.setPower(true);
    assert_true(track.getPower());
    assert_true(gpio_states[22]);

    track.setPower(false);
    assert_false(track.getPower());
    assert_false(gpio_states[22]);
}

// Test command queuing and sending
static void test_queue_command(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Create a test command
    raw_dcc_cmd_t test_cmd;
    test_cmd.is_prog = false;
    test_cmd.length = 3;
    test_cmd.data[0] = 0x03; // Address
    test_cmd.data[1] = 0x3F; // Speed command
    test_cmd.data[2] = 0x3C; // Checksum (0x03 ^ 0x3F)
    test_cmd.cmd_data = 0;
    test_cmd.repeats = 1;

    // Queue the command
    track.queueCommand(&test_cmd);

    // Call loop to process the command
    track.loop();

    // Verify command was sent (check sent_track_packets)
    assert_int_equal(sent_track_packets.size(), 1);
    
    // Verify timing was updated
    assert_true(track.getLastCommandTime() > 0);
}

// Test idle packet generation
static void test_idle_packet_generation(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Call loop with no queued commands - should send idle
    track.loop();

    // Verify idle packet was sent
    assert_int_equal(sent_track_packets.size(), 1);
    
    // Verify timing was updated
    assert_true(track.getLastCommandTime() > 0);
}

// Test direct send command
static void test_send_command_main_track(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings); // Main track

    raw_dcc_cmd_t test_cmd;
    test_cmd.is_prog = false;
    test_cmd.length = 2;
    test_cmd.data[0] = 0x00; // Broadcast address
    test_cmd.data[1] = 0x41; // Emergency stop
    test_cmd.cmd_data = 0;
    test_cmd.repeats = 0;

    uint32_t time_before = track.getLastCommandTime();
    
    track.sendCommand(&test_cmd);

    // Verify timing was updated
    assert_true(track.getLastCommandTime() > time_before);
    
    // Verify packet was built and sent
    assert_int_equal(sent_track_packets.size(), 1);
    
    // Verify the cmd_data was built (should be non-zero after sendCommand)
    assert_true(test_cmd.cmd_data != 0);
}

static void test_send_command_prog_track(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 19;
    settings.ctrl_pin = 21;
    settings.adc_num = 1;
    settings.short_pin = 17;

    PicoDccTrack track(true, settings); // Programming track

    raw_dcc_cmd_t test_cmd;
    test_cmd.is_prog = true;
    test_cmd.length = 3;
    test_cmd.data[0] = 0x03; // Address
    test_cmd.data[1] = 0x3F; // Speed command  
    test_cmd.data[2] = 0x3C; // Data
    test_cmd.cmd_data = 0;
    test_cmd.repeats = 0;

    track.sendCommand(&test_cmd);

    // Verify packet was sent
    assert_int_equal(sent_track_packets.size(), 1);
    
    // Programming track should use different preamble
    // The exact cmd_data format verification can be added here
}

// Test current monitoring and short circuit protection
static void test_current_monitoring_normal(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);
    
    // Turn power on first
    track.powerOn();
    assert_true(track.getPower());
    
    // Set normal current reading (below 90% threshold)
    mock_adc_reading = 1000; // Well below 90% of 4096
    
    // Call loop - should not trigger short circuit protection
    track.loop();
    
    // Power should still be on
    assert_true(track.getPower());
    assert_true(gpio_states[22]); // Power control pin
    assert_false(gpio_states[16]); // Short LED should be off
}

static void test_current_monitoring_short_circuit(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);
    
    // Turn power on first
    track.powerOn();
    assert_true(track.getPower());
    
    // Set high current reading (above 90% threshold)
    mock_adc_reading = 3800; // Above 90% of 4096 (3686)
    
    // Call loop - should trigger short circuit protection
    track.loop();
    
    // Power should be turned off automatically
    assert_false(track.getPower());
    assert_false(gpio_states[22]); // Power control pin should be off
    assert_true(gpio_states[16]); // Short LED should be on
}

// The LCD reads isTripped() to tell "operator switched the track off" apart from
// "overcurrent switched it off" -- both leave getPower() false, so the flag is the
// only thing carrying that distinction to the display.
static void test_trip_flag_set_on_overcurrent_and_cleared_on_power_on(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    track.powerOn();
    assert_false(track.isTripped()); // Healthy track is not tripped

    // Operator-initiated power off must not look like a trip
    track.powerOff();
    assert_false(track.isTripped());

    track.powerOn();
    mock_adc_reading = 3800; // Above 90% of 4096 (3686)
    track.loop();

    assert_false(track.getPower());
    assert_true(track.isTripped()); // Overcurrent did this, not the operator

    // Trip state survives until power is deliberately restored
    track.loop();
    assert_true(track.isTripped());

    mock_adc_reading = 1000;
    track.powerOn();
    assert_false(track.isTripped());
}

static void test_current_monitoring_no_adc(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN; // No ADC
    settings.short_pin = UNUSED_PIN; // No short LED

    PicoDccTrack track(false, settings);
    
    // Turn power on
    track.powerOn();
    assert_true(track.getPower());
    
    // Set high current reading (should be ignored since no ADC is configured)
    mock_adc_reading = 3000; // Above overcurrent threshold but should be ignored
    
    // Call loop - should not affect power since no current monitoring is available
    track.loop();
    
    // Power should still be on because overcurrent protection is skipped
    assert_true(track.getPower());
}

// Test timing safety features
static void test_timing_tracking(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Initial state
    assert_int_equal(track.getLastCommandTime(), 0);
    assert_int_equal(track.getMaxCommandGap(), 0);

    // Send a command
    raw_dcc_cmd_t test_cmd;
    test_cmd.is_prog = false;
    test_cmd.length = 2;
    test_cmd.data[0] = 0x00;
    test_cmd.data[1] = 0x41;
    test_cmd.cmd_data = 0;
    test_cmd.repeats = 0;

    uint32_t time_before = mock_time_ms;
    mock_time_ms += 100; // Advance time by 100ms
    
    track.sendCommand(&test_cmd);

    // Verify timing was updated
    assert_true(track.getLastCommandTime() > time_before);
    
    // Test gap tracking (this would be tested in controller integration)
    track.resetMaxCommandGap();
    assert_int_equal(track.getMaxCommandGap(), 0);
}

// Test sendIdle functionality
static void test_send_idle(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    track.sendIdle();

    // Verify idle packet was sent
    assert_int_equal(sent_track_packets.size(), 1);
    
    // Verify timing was updated
    assert_true(track.getLastCommandTime() > 0);
}

// Test command data building
static void test_command_data_building(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Test with emergency stop command
    raw_dcc_cmd_t test_cmd;
    test_cmd.is_prog = false;
    test_cmd.length = 2;
    test_cmd.data[0] = 0x00; // Broadcast address
    test_cmd.data[1] = 0x41; // Emergency stop instruction
    test_cmd.cmd_data = 0;   // Should be built by sendCommand
    test_cmd.repeats = 0;

    track.sendCommand(&test_cmd);

    // Verify cmd_data was built (non-zero)
    assert_true(test_cmd.cmd_data != 0);
    
    // Verify packet was sent
    assert_int_equal(sent_track_packets.size(), 1);
    
    // The sent packet should match the built cmd_data
    assert_int_equal(sent_track_packets[0], test_cmd.cmd_data);
}

// Test current averaging
static void test_current_averaging(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);
    
    // Initial average should be 0
    assert_float_equal(track.getAverageCurrent(), 0.0, 0.01);
    
    // Note: Full current averaging testing would require multiple loop() calls
    // with different mock_adc_reading values to simulate the averaging process
    // This is a basic test to verify the getter works
}

// Test that current monitoring improvement works correctly
static void test_current_monitoring_improvement(void **state)
{
    // Test with ADC configured - should monitor current
    track_settings_t settings_with_adc;
    settings_with_adc.signal_pin = 18;
    settings_with_adc.ctrl_pin = 22;
    settings_with_adc.adc_num = 0;
    settings_with_adc.short_pin = 16;

    PicoDccTrack track_with_adc(false, settings_with_adc);
    track_with_adc.powerOn();
    
    // High current should trigger overcurrent protection
    mock_adc_reading = 3800; // Above 90% of 4096 (3686)
    track_with_adc.loop();
    assert_false(track_with_adc.getPower()); // Power should be cut
    assert_true(gpio_states[16]); // Short LED should be on
    
    // Test without ADC configured - should skip monitoring entirely
    track_settings_t settings_no_adc;
    settings_no_adc.signal_pin = 19;
    settings_no_adc.ctrl_pin = 23;
    settings_no_adc.adc_num = UNUSED_PIN; // No ADC
    settings_no_adc.short_pin = UNUSED_PIN; // No short LED

    PicoDccTrack track_no_adc(false, settings_no_adc);
    track_no_adc.powerOn();
    
    // Even with extreme current reading, power should remain on
    mock_adc_reading = 4095; // Maximum ADC reading
    track_no_adc.loop();
    assert_true(track_no_adc.getPower()); // Power should remain on
}

// Test PIO Health Monitoring - Option 3: Transmission Counters
static void test_pio_transmission_monitoring(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN;
    settings.short_pin = UNUSED_PIN;

    PicoDccTrack track(false, settings);

    // Initially should be healthy
    assert_true(track.isPIOHealthy());
    assert_int_equal(track.getCommandsQueued(), 0);
    assert_int_equal(track.getCommandsSent(), 0);
    
    // Queue some commands and verify counters
    raw_dcc_cmd_t cmd = {0};
    cmd.length = 2;
    cmd.data[0] = 0x03;
    cmd.data[1] = 0x60;
    
    track.queueCommand(&cmd);
    assert_int_equal(track.getCommandsQueued(), 1);
    assert_int_equal(track.getCommandsSent(), 0);
    
    // Process commands through loop
    track.loop();
    assert_int_equal(track.getCommandsSent(), 1);
    assert_true(track.isPIOHealthy());
}

// Test PIO Health - Transmission Stall Detection
static void test_pio_transmission_stall(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN;
    settings.short_pin = UNUSED_PIN;

    PicoDccTrack track(false, settings);
    
    // Queue commands but simulate PIO not processing them
    raw_dcc_cmd_t cmd = {0};
    cmd.length = 2;
    cmd.data[0] = 0x03;
    cmd.data[1] = 0x60;
    
    track.queueCommand(&cmd);
    track.queueCommand(&cmd);
    assert_int_equal(track.getCommandsQueued(), 2);
    
    // Advance time to simulate stall (commands queued but not sent for >100ms)
    mock_time_ms += 150; // 150ms gap
    
    // Check health - should detect stall since commands are queued but not processed
    assert_false(track.isPIOHealthy());
}

// Test PIO Health - Idle Packet Generation
static void test_pio_idle_packet_tracking(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN;
    settings.short_pin = UNUSED_PIN;

    PicoDccTrack track(false, settings);
    
    // Process loop with no commands - should generate idle packets
    track.loop();
    track.loop();
    track.loop();
    
    assert_int_equal(track.getIdlePacketsSent(), 3);
    assert_true(track.isPIOHealthy());
    
    // Verify idle packets are being tracked for health monitoring
    assert_int_equal(track.getCommandsQueued(), 0);
    assert_int_equal(track.getCommandsSent(), 0);
}

// Test PIO Health - Complete Transmission Failure
static void test_pio_transmission_failure(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN;
    settings.short_pin = UNUSED_PIN;

    PicoDccTrack track(false, settings);
    
    // Establish baseline activity
    track.loop(); // Generate idle packet
    uint32_t initial_idle_count = track.getIdlePacketsSent();
    assert_true(initial_idle_count > 0);
    
    // Advance time significantly with no new transmissions (simulate PIO dead)
    mock_time_ms += 200; // 200ms
    
    // Multiple health checks with no transmission progress
    track.checkPIOHealth(); // First check (50ms)
    mock_time_ms += 50;
    track.checkPIOHealth(); // Second check (100ms) 
    mock_time_ms += 50;
    track.checkPIOHealth(); // Third check (150ms) - should detect failure
    mock_time_ms += 50;
    track.checkPIOHealth(); // Fourth check (200ms) - definitely failed
    
    // Should detect complete transmission failure
    assert_false(track.isPIOHealthy());
}

// Test PIO Health Status Reporting
static void test_pio_health_status(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = UNUSED_PIN;
    settings.short_pin = UNUSED_PIN;

    PicoDccTrack track(false, settings);
    
    // Initially healthy
    assert_true(track.getPIOHealthStatus());
    
    // Process normal operation
    track.loop(); // Generate idle
    track.loop(); // Generate idle
    assert_true(track.getPIOHealthStatus());
    
    // Verify health counters are accessible
    assert_int_equal(track.getCommandsQueued(), 0);
    assert_int_equal(track.getCommandsSent(), 0);
    assert_int_equal(track.getIdlePacketsSent(), 2);
}


// ---------------------------------------------------------------------------
// ADC channel routing (issue #14)
//
// adc_select_input() is called once, in the constructor, and loop() then calls
// a bare adc_read(). PicoDccController constructs the main track first and the
// programming track second, so the mux is left on the programming track's
// channel for the life of the firmware and both tracks read it.
//
// These two tests pin that down from both directions. When #14 is fixed they
// should invert: a short on the main channel must trip the main track, and a
// short on the programming channel must not.
// ---------------------------------------------------------------------------

static void test_ISSUE_14_main_track_misses_a_short_on_its_own_channel(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;      // main track senses on ADC 0
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Stand in for the programming track being constructed afterwards, which is
    // what leaves the mux pointing at ADC 1.
    adc_select_input(1);

    mock_adc_set_channel(0, 4000);  // main track: hard short, well over the 90% trip
    mock_adc_set_channel(1, 0);     // programming track: quiet

    track.setPower(true);
    track.loop();

    // loop() never reselected ADC 0, so it sampled the programming track and saw
    // nothing wrong. Power stays on into a dead short.
    assert_int_equal(mock_adc_selected_channel(), 1);
    assert_true(track.getPower());
    assert_false(gpio_states[16]);  // short LED never lit
}

static void test_ISSUE_14_main_track_trips_on_the_other_tracks_current(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    adc_select_input(1);

    mock_adc_set_channel(0, 0);     // main track: quiet
    mock_adc_set_channel(1, 4000);  // programming track: short

    track.setPower(true);
    track.loop();

    // The failure is wrong in both directions: a healthy main track is cut
    // because the programming track drew current.
    assert_false(track.getPower());
    assert_true(gpio_states[16]);
}

// ---------------------------------------------------------------------------
// Construction asserts
//
// PicoDccTrack's constructor asserts that it managed to claim a PIO state
// machine. The mock previously discarded every assert; it now records them, so
// a clean construction can be asserted to be clean.
// ---------------------------------------------------------------------------

static void test_construction_fires_no_asserts(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    mock_reset_asserts();
    PicoDccTrack track(false, settings);

    assert_int_equal(mock_assert_failures, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_track_constructor_main, setup, teardown),
        cmocka_unit_test_setup_teardown(test_track_constructor_prog, setup, teardown),
        cmocka_unit_test_setup_teardown(test_track_constructor_no_adc, setup, teardown),
        cmocka_unit_test_setup_teardown(test_power_control, setup, teardown),
        cmocka_unit_test_setup_teardown(test_queue_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_idle_packet_generation, setup, teardown),
        cmocka_unit_test_setup_teardown(test_send_command_main_track, setup, teardown),
        cmocka_unit_test_setup_teardown(test_send_command_prog_track, setup, teardown),
        cmocka_unit_test_setup_teardown(test_current_monitoring_normal, setup, teardown),
        cmocka_unit_test_setup_teardown(test_current_monitoring_short_circuit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_trip_flag_set_on_overcurrent_and_cleared_on_power_on, setup, teardown),
        cmocka_unit_test_setup_teardown(test_current_monitoring_no_adc, setup, teardown),
        cmocka_unit_test_setup_teardown(test_timing_tracking, setup, teardown),
        cmocka_unit_test_setup_teardown(test_send_idle, setup, teardown),
        cmocka_unit_test_setup_teardown(test_command_data_building, setup, teardown),
        cmocka_unit_test_setup_teardown(test_current_averaging, setup, teardown),
        cmocka_unit_test_setup_teardown(test_current_monitoring_improvement, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_transmission_monitoring, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_transmission_stall, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_idle_packet_tracking, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_transmission_failure, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_health_status, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_14_main_track_misses_a_short_on_its_own_channel, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_14_main_track_trips_on_the_other_tracks_current, setup, teardown),
        cmocka_unit_test_setup_teardown(test_construction_fires_no_asserts, setup, teardown),
    };

    printf("Running Track Tests\n");
    return cmocka_run_group_tests(tests, NULL, NULL);
}