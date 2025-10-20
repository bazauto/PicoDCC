/* test/pico_diagnostic_tests.cpp */
#define TEST_BUILD  // Ensure test mode is defined

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

extern "C" {
    #include <cmocka.h>
}

#include "../lib/pico_diagnostic.h"
#include "mocks.h"

// Mock timing control for tests
uint32_t mock_time_ms = 0;

// Mock globals (needed by mocks.cpp but not used in diagnostic tests)
std::vector<raw_dcc_cmd_t> queued_commands;
uint32_t mock_adc_reading = 0;
std::vector<std::string> uart_output_log;
bool track_power_states[2] = {false, false};
std::vector<uint64_t> sent_track_packets;

// Test fixture: Initialize and clear log buffer before each test
static int setup(void **state) {
    (void)state;  // Unused
    diag_log_init();
    return 0;
}

static int teardown(void **state) {
    (void)state;  // Unused
    diag_log_clear();
    return 0;
}

/**
 * Test: Buffer initialization
 */
static void test_log_buffer_init(void **state) {
    (void)state;
    
    // After init, buffer should be empty
    assert_int_equal(diag_log_get_count(), 0);
    
    // Should be able to get count without crash
    uint8_t count = diag_log_get_count();
    assert_int_equal(count, 0);
}

/**
 * Test: Adding single entry
 */
static void test_log_buffer_add_single(void **state) {
    (void)state;
    
    // Add one entry
    diagnostic_msg_t msg;
    msg.level = DIAG_ERROR;
    msg.timestamp = 1000;
    msg.component = COMPONENT_TRACK;
    msg.message = "Test error message";
    
    diag_log_add(msg);
    
    // Should have 1 entry
    assert_int_equal(diag_log_get_count(), 1);
    
    // Retrieve and verify (using new thread-safe copy API)
    diagnostic_msg_t retrieved;
    assert_true(diag_log_get_entry(0, &retrieved));
    assert_int_equal(retrieved.level, DIAG_ERROR);
    assert_int_equal(retrieved.timestamp, 1000);
    assert_string_equal(retrieved.component, COMPONENT_TRACK);
    assert_string_equal(retrieved.message, "Test error message");
}

/**
 * Test: Adding multiple entries
 */
static void test_log_buffer_add_multiple(void **state) {
    (void)state;
    
    // Add 5 entries
    for (int i = 0; i < 5; i++) {
        diagnostic_msg_t msg;
        msg.level = DIAG_INFO;
        msg.timestamp = 1000 + i;
        msg.component = COMPONENT_CONTROLLER;
        msg.message = "Test message";
        diag_log_add(msg);
    }
    
    // Should have 5 entries
    assert_int_equal(diag_log_get_count(), 5);
    
    // Verify order (oldest to newest)
    for (int i = 0; i < 5; i++) {
        diagnostic_msg_t entry;
        assert_true(diag_log_get_entry(i, &entry));
        assert_int_equal(entry.timestamp, 1000 + i);
    }
}

/**
 * Test: Circular buffer wrap-around
 */
static void test_log_buffer_wraparound(void **state) {
    (void)state;
    
    // Fill buffer beyond capacity (30 entries + 5 more)
    for (int i = 0; i < DIAG_LOG_BUFFER_SIZE + 5; i++) {
        diagnostic_msg_t msg;
        msg.level = DIAG_WARNING;
        msg.timestamp = 1000 + i;
        msg.component = COMPONENT_POWER;
        msg.message = "Wraparound test";
        diag_log_add(msg);
    }
    
    // Should be capped at buffer size
    assert_int_equal(diag_log_get_count(), DIAG_LOG_BUFFER_SIZE);
    
    // Oldest entry should be at index 0, which is now entry #5 (0-4 were overwritten)
    diagnostic_msg_t oldest;
    assert_true(diag_log_get_entry(0, &oldest));
    assert_int_equal(oldest.timestamp, 1000 + 5);  // Entry 5 is now oldest
    
    // Newest entry should be at index 29, which is entry #34
    diagnostic_msg_t newest;
    assert_true(diag_log_get_entry(DIAG_LOG_BUFFER_SIZE - 1, &newest));
    assert_int_equal(newest.timestamp, 1000 + (DIAG_LOG_BUFFER_SIZE + 5 - 1));  // Entry 34
}

/**
 * Test: Invalid index returns NULL
 */
static void test_log_buffer_invalid_index(void **state) {
    (void)state;
    
    // Add 3 entries
    for (int i = 0; i < 3; i++) {
        diagnostic_msg_t msg;
        msg.level = DIAG_CRITICAL;
        msg.timestamp = 2000 + i;
        msg.component = COMPONENT_CORE;
        msg.message = "Critical error";
        diag_log_add(msg);
    }
    
    assert_int_equal(diag_log_get_count(), 3);
    
    // Valid indices: 0, 1, 2
    diagnostic_msg_t entry;
    assert_true(diag_log_get_entry(0, &entry));
    assert_true(diag_log_get_entry(1, &entry));
    assert_true(diag_log_get_entry(2, &entry));
    
    // Invalid indices: 3, 4, 255
    assert_false(diag_log_get_entry(3, &entry));
    assert_false(diag_log_get_entry(4, &entry));
    assert_false(diag_log_get_entry(255, &entry));
}

/**
 * Test: Clear buffer
 */
static void test_log_buffer_clear(void **state) {
    (void)state;
    
    // Add 10 entries
    for (int i = 0; i < 10; i++) {
        diagnostic_msg_t msg;
        msg.level = DIAG_ERROR;
        msg.timestamp = 3000 + i;
        msg.component = COMPONENT_DCCEX;
        msg.message = "Error entry";
        diag_log_add(msg);
    }
    
    assert_int_equal(diag_log_get_count(), 10);
    
    // Clear buffer
    diag_log_clear();
    
    // Should be empty
    assert_int_equal(diag_log_get_count(), 0);
    
    // Should return false for any index
    diagnostic_msg_t entry;
    assert_false(diag_log_get_entry(0, &entry));
}

/**
 * Test: LOG_* macros integration
 */
static void test_log_macros(void **state) {
    (void)state;
    
    // Use LOG macros to add entries
    LOG_INFO(COMPONENT_CONTROLLER, "Info message");
    LOG_WARNING(COMPONENT_TRACK, "Warning message");
    LOG_ERROR(COMPONENT_POWER, "Error message");
    LOG_CRITICAL(COMPONENT_CORE, "Critical message");
    
    // Should have 4 entries
    assert_int_equal(diag_log_get_count(), 4);
    
    // Verify severity levels
    diagnostic_msg_t entry;
    assert_true(diag_log_get_entry(0, &entry));
    assert_int_equal(entry.level, DIAG_INFO);
    assert_true(diag_log_get_entry(1, &entry));
    assert_int_equal(entry.level, DIAG_WARNING);
    assert_true(diag_log_get_entry(2, &entry));
    assert_int_equal(entry.level, DIAG_ERROR);
    assert_true(diag_log_get_entry(3, &entry));
    assert_int_equal(entry.level, DIAG_CRITICAL);
}

/**
 * Test: Component identifiers
 */
static void test_component_identifiers(void **state) {
    (void)state;
    
    // Test all component identifiers
    LOG_ERROR(COMPONENT_CONTROLLER, "Controller error");
    LOG_ERROR(COMPONENT_TRACK, "Track error");
    LOG_ERROR(COMPONENT_POWER, "Power error");
    LOG_ERROR(COMPONENT_QUEUE, "Queue error");
    LOG_ERROR(COMPONENT_CORE, "Core error");
    LOG_ERROR(COMPONENT_DCCEX, "DCCEX error");
    
    assert_int_equal(diag_log_get_count(), 6);
    
    // Verify components
    diagnostic_msg_t entry;
    assert_true(diag_log_get_entry(0, &entry));
    assert_string_equal(entry.component, "CONTROLLER");
    assert_true(diag_log_get_entry(1, &entry));
    assert_string_equal(entry.component, "TRACK");
    assert_true(diag_log_get_entry(2, &entry));
    assert_string_equal(entry.component, "POWER");
    assert_true(diag_log_get_entry(3, &entry));
    assert_string_equal(entry.component, "QUEUE");
    assert_true(diag_log_get_entry(4, &entry));
    assert_string_equal(entry.component, "CORE");
    assert_true(diag_log_get_entry(5, &entry));
    assert_string_equal(entry.component, "DCCEX");
}

/**
 * Test: Behavior before initialization
 */
static void test_uninitialized_buffer(void **state) {
    (void)state;
    
    // Clear initialization flag (simulate uninitialized state)
    extern diagnostic_log_buffer_t g_diag_log_buffer;
    g_diag_log_buffer.initialized = false;
    
    // Operations should be safe (no crash)
    diagnostic_msg_t msg;
    msg.level = DIAG_ERROR;
    msg.timestamp = 5000;
    msg.component = COMPONENT_TRACK;
    msg.message = "Should be ignored";
    
    diag_log_add(msg);  // Should do nothing
    
    // Count should return 0
    assert_int_equal(diag_log_get_count(), 0);
    
    // Get entry should return false
    diagnostic_msg_t entry;
    assert_false(diag_log_get_entry(0, &entry));
    
    // Clear should be safe
    diag_log_clear();  // Should do nothing
    
    // Re-initialize for cleanup
    diag_log_init();
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_log_buffer_init, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_add_single, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_add_multiple, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_wraparound, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_invalid_index, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_clear, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_macros, setup, teardown),
        cmocka_unit_test_setup_teardown(test_component_identifiers, setup, teardown),
        cmocka_unit_test_setup_teardown(test_uninitialized_buffer, setup, teardown),
    };

    printf("Running Diagnostic Tests\n");
    return cmocka_run_group_tests(tests, NULL, NULL);
}
