#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoConfigStorage/pico_config_storage.h"

// Test: Default configuration values
static void test_default_config_values(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();  // Should load defaults in test mode
    
    // Verify all default values
    assert_float_equal(storage.getADCToMAConversion(), DEFAULT_ADC_TO_MA, 0.0001f);
    assert_float_equal(storage.getACKThreshold(), DEFAULT_ACK_THRESHOLD, 0.1f);
    assert_float_equal(storage.getACKMinDuration(), DEFAULT_ACK_MIN_DURATION, 0.1f);
    assert_float_equal(storage.getACKMaxDuration(), DEFAULT_ACK_MAX_DURATION, 0.1f);
    assert_float_equal(storage.getBaselineCurrent(), DEFAULT_BASELINE_CURRENT, 0.1f);
    assert_int_equal(storage.getMainTrackCurrentLimit(), DEFAULT_MAIN_CURRENT_LIMIT);
    assert_int_equal(storage.getProgTrackCurrentLimit(), DEFAULT_PROG_CURRENT_LIMIT);
    
    assert_true(storage.isValid());
}

// Test: Setting and getting ADC conversion factor
static void test_set_adc_conversion(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    // Change ADC conversion factor (e.g., after calibration)
    float new_conversion = 0.0512f;
    storage.setADCToMAConversion(new_conversion);
    
    assert_float_equal(storage.getADCToMAConversion(), new_conversion, 0.0001f);
    assert_true(storage.isValid());
}

// Test: Setting and getting ACK threshold
static void test_set_ack_threshold(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    // Tune ACK threshold for finicky decoder
    float new_threshold = 55.0f;
    storage.setACKThreshold(new_threshold);
    
    assert_float_equal(storage.getACKThreshold(), new_threshold, 0.1f);
    assert_true(storage.isValid());
}

// Test: Setting and getting ACK duration limits
static void test_set_ack_durations(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    storage.setACKMinDuration(4.5f);
    storage.setACKMaxDuration(7.5f);
    
    assert_float_equal(storage.getACKMinDuration(), 4.5f, 0.1f);
    assert_float_equal(storage.getACKMaxDuration(), 7.5f, 0.1f);
    assert_true(storage.isValid());
}

// Test: Setting and getting current limits
static void test_set_current_limits(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    storage.setMainTrackCurrentLimit(2500);
    storage.setProgTrackCurrentLimit(300);
    
    assert_int_equal(storage.getMainTrackCurrentLimit(), 2500);
    assert_int_equal(storage.getProgTrackCurrentLimit(), 300);
    assert_true(storage.isValid());
}

// Test: Reset to defaults
static void test_reset_to_defaults(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    // Change some values
    storage.setADCToMAConversion(0.0999f);
    storage.setACKThreshold(70.0f);
    
    // Reset to defaults
    storage.resetToDefaults();
    
    // Verify back to defaults
    assert_float_equal(storage.getADCToMAConversion(), DEFAULT_ADC_TO_MA, 0.0001f);
    assert_float_equal(storage.getACKThreshold(), DEFAULT_ACK_THRESHOLD, 0.1f);
    assert_true(storage.isValid());
}

// Test: Save and load cycle (mock in test mode)
static void test_save_load_cycle(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    // Set custom values
    float custom_conversion = 0.0555f;
    storage.setADCToMAConversion(custom_conversion);
    
    // Save (mocked in test mode, should succeed)
    bool save_result = storage.save();
    assert_true(save_result);
    
    // In test mode, load always returns defaults, but we can verify
    // that save didn't corrupt the in-memory config
    assert_float_equal(storage.getADCToMAConversion(), custom_conversion, 0.0001f);
    assert_true(storage.isValid());
}

// Test: Configuration structure has correct magic number
static void test_config_magic_number(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    const pico_config_t *cfg = storage.getConfig();
    assert_int_equal(cfg->magic, CONFIG_MAGIC);
    assert_int_equal(cfg->version, CONFIG_VERSION);
}

// Test: Configuration checksum calculation
static void test_config_checksum(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    // Get config and verify checksum is non-zero
    const pico_config_t *cfg = storage.getConfig();
    assert_int_not_equal(cfg->checksum, 0);
    
    // After save, checksum should still be valid
    storage.save();
    assert_int_not_equal(cfg->checksum, 0);
}

// Test: Multiple configuration instances (independence)
static void test_multiple_instances(void **state) {
    (void)state;
    
    PicoConfigStorage storage1;
    PicoConfigStorage storage2;
    
    storage1.load();
    storage2.load();
    
    // Modify storage1
    storage1.setADCToMAConversion(0.0600f);
    
    // storage2 should still have defaults
    assert_float_equal(storage1.getADCToMAConversion(), 0.0600f, 0.0001f);
    assert_float_equal(storage2.getADCToMAConversion(), DEFAULT_ADC_TO_MA, 0.0001f);
}

// Test: Baseline current setting
static void test_baseline_current(void **state) {
    (void)state;
    
    PicoConfigStorage storage;
    storage.load();
    
    // Measure and store baseline current
    float measured_baseline = 12.5f;  // mA
    storage.setBaselineCurrent(measured_baseline);
    
    assert_float_equal(storage.getBaselineCurrent(), measured_baseline, 0.1f);
    assert_true(storage.isValid());
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_config_values),
        cmocka_unit_test(test_set_adc_conversion),
        cmocka_unit_test(test_set_ack_threshold),
        cmocka_unit_test(test_set_ack_durations),
        cmocka_unit_test(test_set_current_limits),
        cmocka_unit_test(test_reset_to_defaults),
        cmocka_unit_test(test_save_load_cycle),
        cmocka_unit_test(test_config_magic_number),
        cmocka_unit_test(test_config_checksum),
        cmocka_unit_test(test_multiple_instances),
        cmocka_unit_test(test_baseline_current),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
