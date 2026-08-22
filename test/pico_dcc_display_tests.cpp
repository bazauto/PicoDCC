/* test/pico_dcc_display_tests.cpp */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCDisplay/pico_dcc_display.h"
#include "../lib/PicoDCCDisplay/mocks/mock_display_renderer.h"

// Forward declaration (avoid including controller headers in test)
class PicoDccController;
struct track_settings_t;

// Mock timing control for tests (external linkage for pico_dcc_display.cpp)
uint32_t mock_time_ms = 0;

// This suite deliberately links a reduced set and not test/mocks.cpp, so it
// supplies the two time functions dcc_millis() is built from itself. They match
// mocks.cpp's definitions: absolute_time_t is milliseconds here.
extern "C" {
uint32_t get_absolute_time(void) { return mock_time_ms; }
uint32_t to_ms_since_boot(uint32_t t) { return t; }
}

void test_advance_time_ms(uint32_t ms) {
    mock_time_ms += ms;
}

void test_reset_time() {
    mock_time_ms = 0;
}

/**
 * Test: Display initialization
 */
static void test_display_init(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    PicoDCCDisplay display(lcd, renderer);
    
    // Should not be initialized yet
    assert_false(display.isInitialized());
    assert_false(renderer.wasInitCalled());
    
    // Initialize display
    bool result = display.init();
    
    // Should succeed and call renderer init
    assert_true(result);
    assert_true(display.isInitialized());
    assert_true(renderer.wasInitCalled());
}

/**
 * Test: Boot sequence phases
 */
static void test_boot_sequence(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    PicoDCCDisplay display(lcd, renderer);
    
    display.init();
    test_reset_time();
    
    // Run boot sequence with 100ms delay (for faster tests)
    display.runBootSequence(100);

    // Boot goes straight to the diagnostic screen. The colour test pattern was
    // dropped deliberately, so assert it stays gone rather than merely not
    // checking for it -- putting it back should fail here first.
    assert_false(renderer.wasTestPatternShown());
    assert_true(renderer.wasDiagnosticScreenShown());
}

/**
 * Test: Double initialization is safe
 */
static void test_double_init(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    PicoDCCDisplay display(lcd, renderer);
    
    // First init
    assert_true(display.init());
    assert_true(display.isInitialized());
    
    // Second init should succeed but not re-initialize
    assert_true(display.init());
    assert_true(display.isInitialized());
}

/**
 * Test: Loop with null controller is safe
 */
static void test_loop_with_null_controller(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    PicoDCCDisplay display(lcd, renderer);
    
    display.init();
    test_reset_time();
    
    // Should not crash with null controller
    display.loop(nullptr);
    
    // No updates should have occurred
    assert_int_equal(renderer.getUpdateCount(), 0);
}

/**
 * Test: Loop updates at 10Hz (100ms intervals)
 */
static void test_loop_update_timing(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    
    // Use a fake controller pointer (we're testing timing, not data gathering)
    PicoDccController* fake_controller = reinterpret_cast<PicoDccController*>(0x1000);
    
    PicoDCCDisplay display(lcd, renderer);
    display.init();
    test_reset_time();
    
    // First loop call at t=0ms - should not update (waiting for first interval)
    display.loop(fake_controller);
    assert_int_equal(renderer.getUpdateCount(), 0);
    
    // Advance 50ms - still too early
    test_advance_time_ms(50);
    display.loop(fake_controller);
    assert_int_equal(renderer.getUpdateCount(), 0);
    
    // Advance another 50ms (total 100ms) - should update
    test_advance_time_ms(50);
    display.loop(fake_controller);
    assert_int_equal(renderer.getUpdateCount(), 1);
    assert_int_equal(renderer.getTickCount(), 1);
    
    // Advance another 100ms - should update again
    test_advance_time_ms(100);
    display.loop(fake_controller);
    assert_int_equal(renderer.getUpdateCount(), 2);
    assert_int_equal(renderer.getTickCount(), 2);
    
    // Advance 50ms - too early for next update
    test_advance_time_ms(50);
    display.loop(fake_controller);
    assert_int_equal(renderer.getUpdateCount(), 2);  // No change
}

/**
 * Test: Track status data gathering
 * Note: In test mode, gatherTrackStatus returns zeros, but we verify the call chain
 */
static void test_track_status_gathering(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    
    // Use fake controller pointer  
    PicoDccController* fake_controller = reinterpret_cast<PicoDccController*>(0x1000);
    
    PicoDCCDisplay display(lcd, renderer);
    display.init();
    test_reset_time();
    
    // Trigger update
    test_advance_time_ms(100);
    display.loop(fake_controller);
    
    // Check that status was gathered and passed to renderer
    // In test mode, values will be zeros, but call chain should work
    const TrackStatus* status = renderer.getLastStatus();
    assert_non_null(status);
}

/**
 * Test: Controller reference is passed to renderer
 */
static void test_controller_reference(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    
    PicoDccController* fake_controller = reinterpret_cast<PicoDccController*>(0x1000);
    
    PicoDCCDisplay display(lcd, renderer);
    display.init();
    
    // Loop should set controller reference in renderer
    display.loop(fake_controller);
    
    assert_ptr_equal(renderer.getController(), fake_controller);
}

/**
 * Test: Boot sequence without initialization is safe
 */
static void test_boot_sequence_without_init(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    PicoDCCDisplay display(lcd, renderer);
    
    // Should not crash
    display.runBootSequence();
    
    // Nothing should have been shown
    assert_false(renderer.wasTestPatternShown());
    assert_false(renderer.wasDiagnosticScreenShown());
}

/**
 * Test: Loop without initialization is safe
 */
static void test_loop_without_init(void** state) {
    LcdDriver lcd;
    MockDisplayRenderer renderer;
    
    PicoDccController* fake_controller = reinterpret_cast<PicoDccController*>(0x1000);
    
    PicoDCCDisplay display(lcd, renderer);
    
    // Should not crash
    display.loop(fake_controller);
    
    // No updates should occur
    assert_int_equal(renderer.getUpdateCount(), 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_display_init),
        cmocka_unit_test(test_boot_sequence),
        cmocka_unit_test(test_double_init),
        cmocka_unit_test(test_loop_with_null_controller),
        cmocka_unit_test(test_loop_update_timing),
        cmocka_unit_test(test_track_status_gathering),
        cmocka_unit_test(test_controller_reference),
        cmocka_unit_test(test_boot_sequence_without_init),
        cmocka_unit_test(test_loop_without_init),
    };
    
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
