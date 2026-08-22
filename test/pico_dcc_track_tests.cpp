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

    track.powerOn();
    mock_adc_set_channel(0, 500);

    // One short of a full window: nothing published yet.
    for (int i = 0; i < TRACK_POWER_CURRENT_SAMPLES - 1; i++) {
        track.loop();
    }
    assert_float_equal(track.getAverageCurrent(), 0.0, 0.01);

    // The sample that closes the window must be counted, not discarded, and the
    // sum must be divided by the window size -- not by size + 1 (#36).
    track.loop();
    assert_float_equal(track.getAverageCurrent(), 500.0, 0.01);
}

// Every sample in the window contributes exactly once. Feeding a window that is
// half one value and half another must land exactly on the midpoint; the old
// code's dropped sample and off-by-one divisor both skewed this.
static void test_current_average_counts_every_sample_exactly_once(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);
    track.powerOn();

    for (int i = 0; i < TRACK_POWER_CURRENT_SAMPLES / 2; i++) {
        mock_adc_set_channel(0, 1000);
        track.loop();
    }
    for (int i = 0; i < TRACK_POWER_CURRENT_SAMPLES / 2; i++) {
        mock_adc_set_channel(0, 2000);
        track.loop();
    }

    assert_float_equal(track.getAverageCurrent(), 1500.0, 0.01);
}

// ---------------------------------------------------------------------------
// Overcurrent trip threshold (issue #36)
//
// The threshold was written `TRACK_POWER_ADC_RANGE / 100 * 90`, where the
// integer division truncates and the trip actually landed at 3600 (87.9%) while
// the comment claimed 90%. These tests pin the boundary so the arithmetic
// cannot silently drift again.
// ---------------------------------------------------------------------------

static void test_trip_threshold_is_ninety_percent_of_full_scale(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    // 90% of a 4096 range, computed multiply-first.
    assert_int_equal(TRACK_POWER_TRIP_THRESHOLD, 3686);

    // Exactly at the threshold is not over it -- the test is `>`.
    PicoDccTrack at_threshold(false, settings);
    at_threshold.powerOn();
    mock_adc_set_channel(0, TRACK_POWER_TRIP_THRESHOLD);
    at_threshold.loop();
    assert_true(at_threshold.getPower());

    // One count above trips.
    PicoDccTrack over_threshold(false, settings);
    over_threshold.powerOn();
    mock_adc_set_channel(0, TRACK_POWER_TRIP_THRESHOLD + 1);
    over_threshold.loop();
    assert_false(over_threshold.getPower());
    assert_true(over_threshold.isTripped());

    // A reading between the old 3600 and the corrected 3686 must no longer trip.
    PicoDccTrack between(false, settings);
    between.powerOn();
    mock_adc_set_channel(0, 3650);
    between.loop();
    assert_true(between.getPower());
}

// ---------------------------------------------------------------------------
// Overcurrent is not evaluated on an unpowered track (issue #36)
//
// With the H-bridge disabled the sense input is not driving a meaningful value.
// Sampling it anyway could latch `tripped` on a track that was already off,
// which the LCD then shows as a fault with no fault present.
// ---------------------------------------------------------------------------

static void test_unpowered_track_does_not_trip_or_sample(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);
    assert_false(track.getPower());  // constructed with power off

    mock_adc_set_channel(0, 4095);  // sense input floating at full scale

    uint32_t selects_before = mock_adc_select_count();
    track.loop();

    // Not read at all, so nothing to misinterpret.
    assert_int_equal(mock_adc_select_count(), selects_before);
    assert_false(track.isTripped());
    assert_false(gpio_states[16]);
    assert_false(track.getPower());
}

// An unpowered track draws nothing, so the LCD must not keep showing the last
// current that flowed before power was cut.
static void test_power_off_clears_the_displayed_average(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);
    track.powerOn();

    mock_adc_set_channel(0, 800);
    for (int i = 0; i < TRACK_POWER_CURRENT_SAMPLES; i++) {
        track.loop();
    }
    assert_float_equal(track.getAverageCurrent(), 800.0, 0.01);

    track.powerOff();
    assert_float_equal(track.getAverageCurrent(), 0.0, 0.01);
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
// The ADC mux is shared. Selecting a channel at construction says nothing about
// which channel is live by the time loop() reads, because the other track's
// constructor -- and, once running, the other track's own loop() -- moves it.
// loop() must therefore select immediately before each read.
//
// These two tests pin that from both directions: a short on a track's own
// channel must trip it, and a short on the other track's channel must not.
// ---------------------------------------------------------------------------

static void test_main_track_trips_on_a_short_on_its_own_channel(void **state)
{
    track_settings_t settings;
    settings.signal_pin = 18;
    settings.ctrl_pin = 22;
    settings.adc_num = 0;      // main track senses on ADC 0
    settings.short_pin = 16;

    PicoDccTrack track(false, settings);

    // Stand in for the programming track being constructed afterwards, or for
    // its loop() having just sampled -- either way the mux is left elsewhere.
    adc_select_input(1);

    mock_adc_set_channel(0, 4000);  // main track: hard short, well over the trip
    mock_adc_set_channel(1, 0);     // programming track: quiet

    track.setPower(true);
    track.loop();

    // loop() reselected ADC 0 for itself, saw the short, and cut power.
    assert_int_equal(mock_adc_selected_channel(), 0);
    assert_false(track.getPower());
    assert_true(track.isTripped());
    assert_true(gpio_states[16]);  // short LED lit
}

static void test_main_track_ignores_the_other_tracks_current(void **state)
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

    // A healthy main track must not be cut because the programming track drew
    // current.
    assert_int_equal(mock_adc_selected_channel(), 0);
    assert_true(track.getPower());
    assert_false(track.isTripped());
    assert_false(gpio_states[16]);
}

// Two tracks alternating, which is what actually happens on Core 1: each loop()
// must reclaim the mux rather than trusting what it finds.
static void test_two_tracks_each_read_their_own_channel(void **state)
{
    track_settings_t main_settings;
    main_settings.signal_pin = 18;
    main_settings.ctrl_pin = 22;
    main_settings.adc_num = 0;
    main_settings.short_pin = 16;

    track_settings_t prog_settings;
    prog_settings.signal_pin = 19;
    prog_settings.ctrl_pin = 23;
    prog_settings.adc_num = 1;
    prog_settings.short_pin = 17;

    // Construction order matches PicoDccController: main first, prog second.
    PicoDccTrack main_track(false, main_settings);
    PicoDccTrack prog_track(true, prog_settings);

    mock_adc_set_channel(0, 0);     // main: quiet
    mock_adc_set_channel(1, 4000);  // prog: short

    main_track.setPower(true);
    prog_track.setPower(true);

    main_track.loop();
    prog_track.loop();

    assert_true(main_track.getPower());   // untouched by the prog track's fault
    assert_false(prog_track.getPower());  // tripped on its own channel
    assert_true(prog_track.isTripped());

    // And the reverse, with the fault moved to the main track.
    mock_adc_set_channel(0, 4000);
    mock_adc_set_channel(1, 0);
    prog_track.setPower(true);

    main_track.loop();
    prog_track.loop();

    assert_false(main_track.getPower());
    assert_true(main_track.isTripped());
    assert_true(prog_track.getPower());
}

// ---------------------------------------------------------------------------
// ADC block initialisation (issue #14)
//
// adc_init() resets and enables the whole ADC block. Called once per track, the
// second track reset the peripheral the first had configured. adc_gpio_init()
// is per pin and must still happen for each track that senses current.
// ---------------------------------------------------------------------------

static void test_adc_block_is_initialised_once_but_each_pin_is_configured(void **state)
{
    track_settings_t main_settings;
    main_settings.signal_pin = 18;
    main_settings.ctrl_pin = 22;
    main_settings.adc_num = 0;

    track_settings_t prog_settings;
    prog_settings.signal_pin = 19;
    prog_settings.ctrl_pin = 23;
    prog_settings.adc_num = 1;

    mock_adc_reset_init_counts();

    PicoDccTrack main_track(false, main_settings);
    PicoDccTrack prog_track(true, prog_settings);

    // The guard is process-lifetime, so an earlier test in this binary may
    // already have consumed the one permitted call. What must never happen is a
    // second reset of a configured block, which is what "at most one" pins.
    assert_true(mock_adc_init_count() <= 1);

    // Both pins are still configured -- the per-track work did not get lost.
    assert_int_equal(mock_adc_gpio_init_count(), 2);
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
        cmocka_unit_test_setup_teardown(test_current_average_counts_every_sample_exactly_once, setup, teardown),
        cmocka_unit_test_setup_teardown(test_trip_threshold_is_ninety_percent_of_full_scale, setup, teardown),
        cmocka_unit_test_setup_teardown(test_unpowered_track_does_not_trip_or_sample, setup, teardown),
        cmocka_unit_test_setup_teardown(test_power_off_clears_the_displayed_average, setup, teardown),
        cmocka_unit_test_setup_teardown(test_current_monitoring_improvement, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_transmission_monitoring, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_transmission_stall, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_idle_packet_tracking, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_transmission_failure, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pio_health_status, setup, teardown),
        cmocka_unit_test_setup_teardown(test_main_track_trips_on_a_short_on_its_own_channel, setup, teardown),
        cmocka_unit_test_setup_teardown(test_main_track_ignores_the_other_tracks_current, setup, teardown),
        cmocka_unit_test_setup_teardown(test_two_tracks_each_read_their_own_channel, setup, teardown),
        cmocka_unit_test_setup_teardown(test_adc_block_is_initialised_once_but_each_pin_is_configured, setup, teardown),
        cmocka_unit_test_setup_teardown(test_construction_fires_no_asserts, setup, teardown),
    };

    printf("Running Track Tests\n");
    return cmocka_run_group_tests(tests, NULL, NULL);
}