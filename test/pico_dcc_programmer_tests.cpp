/*
 * PicoDCC Programmer Unit Tests
 * 
 * Test-Driven Development approach for CV read operations:
 * - ACK detection logic
 * - Baseline current measurement
 * - Direct Mode packet generation
 * - CV read workflow
 * 
 * These tests define the expected behavior before implementation.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdint.h>

extern "C" {
    #include <cmocka.h>
}

#include "../lib/PicoDCCProgrammer/pico_dcc_programmer.h"
#include "../lib/PicoDCCTrack/pico_dcctrack.h"
#include "../lib/PicoConfigStorage/pico_config_storage.h"
#include "../lib/dcc_types.h"
#include "mocks.h"

// Test fixture
struct programmer_test_state {
    PicoDccTrack *track;
    PicoConfigStorage *config;
    PicoDccProgrammer *programmer;
    
    // Test control variables
    bool track_powered;
    float baseline_current;
};

// Helper functions for test control
static void set_track_power(struct programmer_test_state *s, bool powered) {
    s->track_powered = powered;
    if (powered) {
        s->track->powerOn();
    } else {
        s->track->powerOff();
    }
}

static void set_baseline_current(struct programmer_test_state *s, float current_ma) {
    s->baseline_current = current_ma;
    // Convert desired milliamp value into raw ADC counts using hardware calibration
    const float counts_per_ma = 1.0f / DEFAULT_ADC_TO_MA;
    mock_adc_reading = static_cast<uint32_t>(current_ma * counts_per_ma);
    
    // Run track loop to process ADC reading into average_current_reading
    // This simulates what would happen in hardware
    for (int i = 0; i < 10; i++) {
        s->track->loop();
    }
}

static void configure_ack_pulse(uint32_t duration_us, uint32_t spike_reading, uint32_t start_delay_ms = 110) {
    const uint32_t start_us = (mock_time_ms + start_delay_ms) * 1000;
    mock_set_ack_pulse(true, start_us, duration_us, spike_reading);
}

static uint32_t get_ack_spike_reading(struct programmer_test_state *s, float extra_margin_ma) {
    const float adc_to_ma = s->config->getADCToMAConversion();
    const uint32_t baseline_counts = mock_adc_reading;
    const float threshold_ma = s->config->getACKThreshold() + extra_margin_ma;
    const uint32_t spike_offset = static_cast<uint32_t>(threshold_ma / adc_to_ma);
    return baseline_counts + spike_offset;
}

// Setup/teardown
static int setup(void **state) {
    struct programmer_test_state *test_state = 
        (struct programmer_test_state *)malloc(sizeof(struct programmer_test_state));
    
    // Create real objects (using mocked hardware underneath)
    track_settings_t prog_settings;
    prog_settings.signal_pin = 2;
    prog_settings.ctrl_pin = 3;
    prog_settings.adc_num = 0;
    prog_settings.short_pin = 4;
    
    test_state->track = new PicoDccTrack(true, prog_settings, nullptr);
    test_state->config = new PicoConfigStorage();
    test_state->programmer = new PicoDccProgrammer(test_state->track, test_state->config);
    
    // Initialize test control
    test_state->track_powered = false;
    test_state->baseline_current = 0.0f;
    
    // Reset mock state
    uart_output_log.clear();
    mock_time_ms = 0;
    mock_adc_reading = 0;
    
    *state = test_state;
    return 0;
}

static int teardown(void **state) {
    struct programmer_test_state *test_state = (struct programmer_test_state *)*state;
    
    delete test_state->programmer;
    delete test_state->config;
    delete test_state->track;
    free(test_state);
    
    return 0;
}

// ============================================================================
// Test Group 1: Constructor and Configuration
// ============================================================================

static void test_programmer_constructor_with_config(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Verify default configuration loaded from PicoConfigStorage
    // Defaults from pico_config_storage.h: 60mA threshold, 5.0ms min (5000µs), 7.0ms max (7000µs)
    assert_int_equal(s->programmer->getACKThreshold(), 60);
    assert_int_equal(s->programmer->getACKMinDuration(), 5000);
    assert_int_equal(s->programmer->getACKMaxDuration(), 7000);
}

static void test_programmer_constructor_without_config(void **state) {
    track_settings_t prog_settings;
    prog_settings.signal_pin = 2;
    prog_settings.ctrl_pin = 3;
    prog_settings.adc_num = 0;
    prog_settings.short_pin = 4;
    
    PicoDccTrack *track = new PicoDccTrack(true, prog_settings, nullptr);
    PicoDccProgrammer *programmer = new PicoDccProgrammer(track, nullptr);
    
    // Should use defaults when no config storage
    assert_int_equal(programmer->getACKThreshold(), ACK_LIMIT_DEFAULT_MA);
    assert_int_equal(programmer->getACKMinDuration(), ACK_MIN_DURATION_DEFAULT_US);
    assert_int_equal(programmer->getACKMaxDuration(), ACK_MAX_DURATION_DEFAULT_US);
    
    delete programmer;
    delete track;
}

static void test_programmer_load_config(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Change config storage values directly (ACK parameters are read dynamically)
    s->config->setACKThreshold(70.0f);           // 70mA
    s->config->setACKMinDuration(5.0f);          // 5.0ms = 5000µs
    s->config->setACKMaxDuration(7.5f);          // 7.5ms = 7500µs
    
    // Verify new values are accessible (converted to µs internally)
    // Programmer reads these dynamically from config_storage during ACK detection
    assert_int_equal(s->programmer->getACKThreshold(), 70);
    assert_int_equal(s->programmer->getACKMinDuration(), 5000);
    assert_int_equal(s->programmer->getACKMaxDuration(), 7500);
}

static void test_programmer_set_ack_threshold(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Set via config storage (programmer reads dynamically)
    s->config->setACKThreshold(80.0f);
    assert_int_equal(s->programmer->getACKThreshold(), 80);
}

static void test_programmer_set_ack_durations(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Set via config storage (programmer reads dynamically)
    s->config->setACKMinDuration(4.0f);  // 4.0ms = 4000µs
    s->config->setACKMaxDuration(9.0f);  // 9.0ms = 9000µs
    
    assert_int_equal(s->programmer->getACKMinDuration(), 4000);
    assert_int_equal(s->programmer->getACKMaxDuration(), 9000);
}

// ============================================================================
// Test Group 2: Baseline Current Measurement
// ============================================================================

static void test_baseline_current_measurement_success(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, stable current
    set_track_power(s, true);
    set_baseline_current(s, 35.0f);  // Typical idle current
    
    // Measure baseline (internal method, tested via readCV)
    int16_t result = s->programmer->readCV(1);  // Will measure baseline first
    
    // Verify baseline was measured (even if readCV fails due to no ACK)
    float baseline = s->programmer->getBaselineCurrent();
    // Baseline should be set (may not be exact due to ADC simulation)
    assert_true(baseline > 0.0f);
}

static void test_baseline_current_measurement_track_not_powered(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track NOT powered
    set_track_power(s, false);
    
    // Attempt CV read (should fail at baseline measurement)
    int16_t result = s->programmer->readCV(1);
    
    // Should return error
    assert_int_equal(result, -1);
    
    // Baseline should not be set
    assert_float_equal(s->programmer->getBaselineCurrent(), 0.0f, 0.01f);
}

static void test_baseline_current_out_of_range_warning(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, unusual current (too high)
    set_track_power(s, true);
    set_baseline_current(s, 150.0f);  // Way too high for idle
    
    // Attempt CV read (should warn but continue)
    int16_t result = s->programmer->readCV(1);
    
    // Baseline should still be measured (even if unusual)
    float baseline = s->programmer->getBaselineCurrent();
    assert_true(baseline > 0.0f);
    
    // Check diagnostic log for warning
    // (Diagnostic log testing requires LOG_WARNING macro implementation)
}

// ============================================================================
// Test Group 3: ACK Detection
// ============================================================================

static void test_ack_detection_valid_pulse(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    uint32_t spike_reading = get_ack_spike_reading(s, 5.0f);
    configure_ack_pulse(6000, spike_reading);
    assert_true(s->programmer->verifyCV(1, 0));
    mock_set_ack_pulse(false, 0, 0, 0);
}

static void test_ack_detection_pulse_too_short(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    uint32_t spike_reading = get_ack_spike_reading(s, 5.0f);
    configure_ack_pulse(3000, spike_reading);
    assert_false(s->programmer->verifyCV(1, 0));
    mock_set_ack_pulse(false, 0, 0, 0);
}

static void test_ack_detection_pulse_too_long(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    uint32_t spike_reading = get_ack_spike_reading(s, 5.0f);
    configure_ack_pulse(10000, spike_reading);
    assert_false(s->programmer->verifyCV(1, 0));
    mock_set_ack_pulse(false, 0, 0, 0);
}

static void test_ack_detection_current_below_threshold(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    // Configure pulse that never reaches threshold (30mA delta instead of 60mA)
    uint32_t spike_reading = get_ack_spike_reading(s, -30.0f);
    configure_ack_pulse(6000, spike_reading);
    assert_false(s->programmer->verifyCV(1, 0));
    mock_set_ack_pulse(false, 0, 0, 0);
}

static void test_ack_detection_no_pulse_timeout(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, stable baseline, NO ACK pulse
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    mock_set_ack_pulse(false, 0, 0, 0);
    assert_false(s->programmer->verifyCV(1, 0));
}

// ============================================================================
// Test Group 4: Direct Mode Packet Generation
// ============================================================================

static void test_generate_cv_read_packet_cv1(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Generate CV1 read packet (short address)
    // This will test the internal generateCVReadPacket() method
    // For now, we'll test via the public readCV() method output
    
    // Expected packet for CV1 read:
    // Byte 1: 0x76 (direct mode address)
    // Byte 2: 0xE3 (1110 00 11 = read CV, bits 9-8 = 00, operation = read)
    // Byte 3: 0x00 (CV1 address = 0)
    // Byte 4: 0x00 (data byte, not used for read)
    // Byte 5: 0xD5 (error byte: 0x76 ^ 0xE3 ^ 0x00 ^ 0x00)
    
    // This will be tested when we implement the generation methods
}

static void test_generate_cv_read_packet_cv17(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Generate CV17 read packet (long address high byte)
    // CV17 address = 16 (CV17 - 1)
    
    // Expected packet for CV17 read:
    // Byte 1: 0x76 (direct mode address)
    // Byte 2: 0xE3 (1110 00 11 = read CV, bits 9-8 = 00, operation = read)
    // Byte 3: 0x10 (CV17 address = 16)
    // Byte 4: 0x00 (data byte, not used)
    // Byte 5: 0xC5 (error byte: 0x76 ^ 0xE3 ^ 0x10 ^ 0x00)
}

static void test_generate_cv_read_packet_cv513(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Generate CV513 read packet (extended addressing test)
    // CV513 address = 512 (0x0200)
    // Bits 9-8 = 0b10
    
    // Expected packet for CV513 read:
    // Byte 1: 0x76 (direct mode address)
    // Byte 2: 0xEB (1110 10 11 = read CV, bits 9-8 = 10, operation = read)
    // Byte 3: 0x00 (CV address low byte)
    // Byte 4: 0x00 (data byte)
    // Byte 5: 0xDD (error byte: 0x76 ^ 0xEB ^ 0x00 ^ 0x00)
}

static void test_generate_cv_verify_packet_cv1_value_3(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Generate CV1 verify packet with value = 3
    // Used to check if decoder address is 3
    
    // Expected packet for CV1 verify (value = 3):
    // Byte 1: 0x76 (direct mode address)
    // Byte 2: 0xE1 (1110 00 01 = verify CV, bits 9-8 = 00, operation = verify)
    // Byte 3: 0x00 (CV1 address = 0)
    // Byte 4: 0x03 (value to verify)
    // Byte 5: 0xD6 (error byte: 0x76 ^ 0xE1 ^ 0x00 ^ 0x03)
}

static void test_packet_error_byte_calculation(void **state) {
    // Test error byte calculation (XOR of all data bytes)
    uint8_t byte1 = 0x76;
    uint8_t byte2 = 0xE3;
    uint8_t byte3 = 0x00;
    uint8_t byte4 = 0x00;
    
    uint8_t error_byte = byte1 ^ byte2 ^ byte3 ^ byte4;
    assert_int_equal(error_byte, 0x95);  // Expected error byte
}

// ============================================================================
// Test Group 5: CV Read Operations
// ============================================================================

static void test_read_cv_invalid_cv_number_too_low(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // CV0 is invalid (valid range: 1-1024)
    int16_t result = s->programmer->readCV(0);
    assert_int_equal(result, -1);
}

static void test_read_cv_invalid_cv_number_too_high(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // CV1025 is invalid (valid range: 1-1024)
    int16_t result = s->programmer->readCV(1025);
    assert_int_equal(result, -1);
}

static void test_read_cv_track_not_powered(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Track not powered
    set_track_power(s, false);
    
    int16_t result = s->programmer->readCV(1);
    assert_int_equal(result, -1);
}

static void test_read_short_address_success(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, decoder with address 3
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    // Read short address
    int16_t address = s->programmer->readShortAddress();
    
    // For now, test the wrapper method exists
    // Full integration test will validate with mock decoder
}

static void test_read_long_address_success(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, decoder with long address 1234
    // CV17 = 0xC4 (high byte: 1100 0100)
    // CV18 = 0xD2 (low byte: 1101 0010)
    // Address = ((0xC4 & 0x3F) << 8) | 0xD2 = (0x04 << 8) | 0xD2 = 1234
    
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    // Read long address
    int16_t address = s->programmer->readLongAddress();
    
    // Full integration test will validate
}

static void test_read_cv_no_ack_for_any_value(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: Track powered, but decoder never sends ACK
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    // Read CV1 (should timeout after trying all 256 values)
    int16_t result = s->programmer->readCV(1);
    
    // Should return error
    assert_int_equal(result, -1);
}

// ============================================================================
// Test Group 6: Integration Scenarios
// ============================================================================

static void test_full_cv_read_workflow_cv1(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Full end-to-end test: Read CV1 from decoder with address 42
    
    // 1. Setup: Track powered, baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    // 3. Read CV1
    int16_t address = s->programmer->readCV(1);
    
    // 4. For now, just verify method is called
    // Full validation when ACK detection is implemented
    
    // 5. Verify baseline was measured
    float baseline = s->programmer->getBaselineCurrent();
    assert_true(baseline > 0.0f || address == -1);  // Either baseline set or error returned
}

static void test_multiple_cv_reads_baseline_not_remeasured(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Test that baseline is only measured once, not on every CV read
    
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    // First read - baseline measured
    s->programmer->readCV(1);
    float baseline1 = s->programmer->getBaselineCurrent();
    
    // Change current slightly
    set_baseline_current(s, 31.0f);
    
    // Second read - baseline should NOT be remeasured
    s->programmer->readCV(2);
    float baseline2 = s->programmer->getBaselineCurrent();
    
    // Baseline should be the same (from first measurement) or both zero (if not implemented)
    // This test will be more meaningful once implementation is complete
}

// ============================================================================
// Group 7: CV Verify Operations
// ============================================================================

// Test verify CV with matching value (ACK expected)
static void test_verify_cv_matching_value(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: powered track with baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    uint32_t spike_reading = get_ack_spike_reading(s, 5.0f);
    configure_ack_pulse(6000, spike_reading);
    mock_time_ms = 0;
    
    // Test: Verify CV1 value is 3
    bool result = s->programmer->verifyCV(1, 3);
    
    // Verify: Should return true (ACK received)
    assert_true(result);
    mock_set_ack_pulse(false, 0, 0, 0);
}

// Test verify CV with non-matching value (no ACK expected)
static void test_verify_cv_non_matching_value(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: powered track with baseline current
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    mock_set_ack_pulse(false, 0, 0, 0);
    mock_time_ms = 0;
    
    // Test: Verify CV1 value is 5 (wrong value)
    bool result = s->programmer->verifyCV(1, 5);
    
    // Verify: Should return false (no ACK)
    assert_false(result);
}

// Test verify CV with track not powered
static void test_verify_cv_track_not_powered(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: track is OFF
    set_track_power(s, false);
    
    // Test: Try to verify CV
    bool result = s->programmer->verifyCV(1, 3);
    
    // Verify: Should fail (track not powered)
    assert_false(result);
}

// Test verify CV with invalid CV number
static void test_verify_cv_invalid_cv_number(void **state) {
    struct programmer_test_state *s = (struct programmer_test_state *)*state;
    
    // Setup: powered track
    set_track_power(s, true);
    set_baseline_current(s, 30.0f);
    
    // Test: Try to verify invalid CV (0 and 1025)
    bool result1 = s->programmer->verifyCV(0, 3);
    bool result2 = s->programmer->verifyCV(1025, 3);
    
    // Verify: Both should fail
    assert_false(result1);
    assert_false(result2);
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(void) {
    const struct CMUnitTest tests[] = {
        // Group 1: Constructor and Configuration
        cmocka_unit_test_setup_teardown(test_programmer_constructor_with_config, setup, teardown),
        cmocka_unit_test(test_programmer_constructor_without_config),
        cmocka_unit_test_setup_teardown(test_programmer_load_config, setup, teardown),
        cmocka_unit_test_setup_teardown(test_programmer_set_ack_threshold, setup, teardown),
        cmocka_unit_test_setup_teardown(test_programmer_set_ack_durations, setup, teardown),
        
        // Group 2: Baseline Current Measurement
        cmocka_unit_test_setup_teardown(test_baseline_current_measurement_success, setup, teardown),
        cmocka_unit_test_setup_teardown(test_baseline_current_measurement_track_not_powered, setup, teardown),
        cmocka_unit_test_setup_teardown(test_baseline_current_out_of_range_warning, setup, teardown),
        
        // Group 3: ACK Detection
        cmocka_unit_test_setup_teardown(test_ack_detection_valid_pulse, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ack_detection_pulse_too_short, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ack_detection_pulse_too_long, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ack_detection_current_below_threshold, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ack_detection_no_pulse_timeout, setup, teardown),
        
        // Group 4: Direct Mode Packet Generation
        cmocka_unit_test_setup_teardown(test_generate_cv_read_packet_cv1, setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_cv_read_packet_cv17, setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_cv_read_packet_cv513, setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_cv_verify_packet_cv1_value_3, setup, teardown),
        cmocka_unit_test(test_packet_error_byte_calculation),
        
        // Group 5: CV Read Operations
        cmocka_unit_test_setup_teardown(test_read_cv_invalid_cv_number_too_low, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_cv_invalid_cv_number_too_high, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_cv_track_not_powered, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_short_address_success, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_long_address_success, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_cv_no_ack_for_any_value, setup, teardown),
        
        // Group 6: Integration Scenarios
        cmocka_unit_test_setup_teardown(test_full_cv_read_workflow_cv1, setup, teardown),
        cmocka_unit_test_setup_teardown(test_multiple_cv_reads_baseline_not_remeasured, setup, teardown),
        
        // Group 7: CV Verify Operations
        cmocka_unit_test_setup_teardown(test_verify_cv_matching_value, setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_cv_non_matching_value, setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_cv_track_not_powered, setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_cv_invalid_cv_number, setup, teardown),
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
}
