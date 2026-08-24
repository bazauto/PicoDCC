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
#include "../lib/dccex_communication.h"  // For PICODCC_IDENTITY
#include "../lib/dcc_time.h"  // For dcc_millis()

// Mock state tracking
extern bool track_power_states[2];
extern std::vector<std::string> uart_output_log;
extern uint32_t mock_time_ms;
void mock_adc_set_channel(uint8_t adc_num, uint32_t reading);
void mock_adc_clear_channels(void);

// Read a packed packet word the way dcc.pio consumes it: byte 7 is the preamble
// length, byte 6 the number of bytes to send, and the payload runs downwards
// from byte 5. Tests assert on what comes out of here rather than rebuilding
// the packing locally -- a test that recomputes the implementation agrees with
// it by construction, which is how #31 stayed invisible.
//
// pico_dcc_pio_tests.cpp does this properly, by running the real assembled PIO
// program. This is the cheap equivalent for tests that only need the bytes.
static std::vector<uint8_t> unpack_sent_packet(uint64_t packet)
{
    const unsigned count = (unsigned)((packet >> 48) & 0xFF);
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < count && i <= DCC_PACKET_FIRST_BYTE; i++) {
        bytes.push_back((uint8_t)((packet >> ((DCC_PACKET_FIRST_BYTE - i) * 8)) & 0xFF));
    }
    return bytes;
}

// Test fixtures
static int setup(void **state)
{
    gpio_states.fill(false);
    memset(track_power_states, 0, sizeof(track_power_states));
    queued_commands.clear();
    uart_output_log.clear();
    mock_adc_clear_channels();  // per-channel currents must not leak between tests
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

// Search the diagnostic log for a message. The boot cascade this guards against
// was reported from the LCD log, so asserting on the log is asserting on exactly
// what the operator sees.
static bool log_contains(const char *needle)
{
    uint8_t count = diag_log_get_count();
    for (uint8_t i = 0; i < count; i++) {
        diagnostic_msg_t entry;
        if (diag_log_get_entry(i, &entry) && strstr(entry.message, needle) != nullptr) {
            return true;
        }
    }
    return false;
}

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

    // Let Core 1 run so the monitor has both a real starting point and a real
    // last-command time. Before its first pass it cannot tell "nothing sent yet"
    // from "commands have stopped", and every boot looks like a fault. Run it at
    // a non-zero time so neither value can be confused with "unset".
    mock_time_ms = 10;
    controller.dccLoop();

    // Move time forward past safety threshold
    mock_time_ms = 150; // 140ms since the last command

    // Run the DCC loop which should trigger safety cutoff
    controller.dccLoop();

    // Check that power was cut and LED turned on
    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));
    assert_true(gpio_states[25]); // Timing LED should be on

    // Prove it tripped on the command gap specifically, and not incidentally via
    // the PIO health branch that shares this cutoff.
    assert_true(log_contains("DCC timing violation detected"));
}

// Boot produces a long delay between construction and the first loop pass: LCD
// init and the boot sequence run in between, and Core 1 is not launched until
// after them. The monitors must not read that delay as Core 1 having died --
// this cascaded into "Emergency power cutoff activated", "DCC timing violation
// detected" and "Core 1 heartbeat failure detected" on every single boot.
static void test_no_false_cutoff_during_boot_delay(void **state)
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

    // Construction happened at t=0; the first Core 0 pass does not come around
    // until the display is up. Core 1 has not been launched yet, so it has never
    // ticked -- which is exactly what the old check mistook for a dead core.
    mock_time_ms = 200;
    controller.dccexLoop();

    assert_false(gpio_states[25]); // No emergency cutoff

    // These are the exact three entries that appeared on the LCD every boot.
    assert_false(log_contains("Emergency power cutoff activated"));
    assert_false(log_contains("Core 1 heartbeat failure detected"));
    assert_false(log_contains("DCC timing violation detected"));
}

// The grace period above must not become a blind spot: a Core 1 that is launched
// and never runs is a real failure, and still has to cut power.
static void test_core1_that_never_starts_still_cuts_power(void **state)
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

    mock_time_ms = 200;
    controller.dccexLoop();      // arms the monitor
    assert_false(gpio_states[25]);

    // Still nothing from Core 1 once the startup deadline has passed.
    mock_time_ms = 200 + CORE1_STARTUP_GRACE_MS + 50;
    controller.dccexLoop();

    assert_true(gpio_states[25]); // Emergency cutoff fired
    assert_true(log_contains("Core 1 failed to start"));
}

// ---------------------------------------------------------------------------
// Millisecond clock wrap (issue #32)
//
// `time_us_32() / 1000` produces a value that wraps every 4,294,967 ms -- 71.6
// minutes of uptime. Unsigned delta arithmetic only survives a wrap at 2^32, so
// across that boundary `current_time - last_cmd` yields roughly 2^32 instead of
// a small positive number and every timeout in the firmware fires at once.
//
// Observed in operation: both tracks powered off with "DCC timing violation
// detected" and nothing turning them back on. The recovery is asymmetric, which
// is what made it baffling -- the next pass agrees again so the error LED goes
// back out, but setPower(false) is never undone. The layout stays dead until
// someone sends <1>.
//
// 4,294,967 ms is the exact boundary: 4294967 * 1000 fits in a uint32, and
// 4294968 * 1000 does not.
// ---------------------------------------------------------------------------

// The helper itself, and the idiom it replaced, side by side. This is the whole
// bug in six lines.
static void test_dcc_millis_deltas_survive_the_old_wrap_point(void **state)
{
    mock_time_ms = 4294960;
    uint32_t before = dcc_millis();
    mock_time_ms = 4294970;
    uint32_t after = dcc_millis();
    assert_int_equal(after - before, 10);

    // What was being differenced before: dividing first moves the wrap from 2^32
    // to 4,294,967, and the delta across it is meaningless.
    mock_time_ms = 4294960;
    uint32_t old_before = time_us_32() / 1000;
    mock_time_ms = 4294970;
    uint32_t old_after = time_us_32() / 1000;
    assert_true((uint32_t)(old_after - old_before) > 100);
}

static void test_no_false_timing_violation_across_the_71_minute_wrap(void **state)
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

    uart_test_write("<1>");
    controller.dccexLoop();
    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));

    // Just below the boundary. Core 1 runs, establishing a real start time and a
    // real last-command time -- main_track->loop() sends an idle packet.
    mock_time_ms = 4294960;
    controller.dccLoop();
    assert_true(controller.isTrackPowerOn(false));

    // 10ms later, and now across the boundary. This is a perfectly healthy
    // 10ms gap; the old clock read it as about 4.29 billion milliseconds.
    mock_time_ms = 4294970;
    controller.dccLoop();

    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));
    assert_false(gpio_states[25]);
    assert_false(log_contains("DCC timing violation detected"));
    assert_false(log_contains("PIO transmission stall detected"));
    assert_false(log_contains("PIO transmission completely stopped"));
}

// The fix must not blunt the check it was breaking: a genuine stall on the far
// side of the wrap still has to cut power.
static void test_real_timing_violation_still_detected_after_the_wrap(void **state)
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

    uart_test_write("<1>");
    controller.dccexLoop();

    mock_time_ms = 4294960;
    controller.dccLoop();
    assert_true(controller.isTrackPowerOn(false));

    // 140ms with nothing sent, straddling the boundary. That is a real fault.
    mock_time_ms = 4294960 + 140;
    controller.dccLoop();

    assert_false(controller.isTrackPowerOn(false));
    assert_false(controller.isTrackPowerOn(true));
    assert_true(gpio_states[25]);
    assert_true(log_contains("DCC timing violation detected"));
}

// The startup banner and the <s> reply must be the same string. They were not:
// startup announced "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>"
// while <s> answered PICODCC, so what JMRI believed it was connected to depended
// on whether it caught the unprompted boot message.
static void test_version_reply_matches_startup_banner(void **state)
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

    // Constructing the controller constructs PicoDccEx, which announces itself.
    assert_false(uart_output_log.empty());
    std::string startup = uart_output_log[0];
    uart_output_log.clear();

    uart_test_write("<s>");
    controller.dccexLoop();

    assert_false(uart_output_log.empty());
    assert_string_equal(uart_output_log[0].c_str(), startup.c_str());
    assert_string_equal(uart_output_log[0].c_str(), PICODCC_IDENTITY);
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

    // Send a throttle command. 126 is the highest speed a DCC-EX host sends;
    // 128 is now rejected outright (#11), so this must stay a legal speed.
    uart_test_write("<t 3 126 1>");

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

    // Add some locos first. 128 is now rejected outright (#11); use the
    // highest legal speed instead.
    uart_test_write("<t 3 126 1>");
    controller.dccexLoop(); // Process loco command
    controller.dccLoop();   // Process the command in core 1

    uart_test_write("<t 4 126 1>");
    controller.dccexLoop(); // Process loco command
    controller.dccLoop();   // Process the command in core 1

    // Clear the command queue
    queued_commands.clear();

    // Print number of locos in the collection for debugging
    printf("Loco count before emergency stop: %lu\n", (unsigned long)controller.getLocoCount());
    fflush(stdout);

    // Send emergency stop command
    uart_test_write("<!>");
    controller.dccexLoop();
    
    // Process the emergency stop command through Core 1 loop to actually send it
    controller.dccLoop();

    // Check that locos collection was cleared after emergency stop
    printf("Loco count after emergency stop: %lu\n", (unsigned long)controller.getLocoCount());
    assert_true(controller.getLocoCount() == 0);

    // Check that a single emergency stop broadcast packet was sent
    extern std::vector<uint64_t> sent_track_packets;
    
    // Assert on the bytes the PIO would transmit, not on a locally rebuilt
    // cmd_data. Recomputing the packing here is what let #31 hide: the test
    // agreed with the implementation by construction, so both could be wrong
    // together. unpack_sent_packet() reads the word the way dcc.pio does.
    bool found_emergency_stop = false;
    for (size_t i = 0; i < sent_track_packets.size(); ++i) {
        std::vector<uint8_t> bytes = unpack_sent_packet(sent_track_packets[i]);
        // Broadcast address, emergency stop instruction, checksum.
        if (bytes == std::vector<uint8_t>{0x00, 0x41, 0x41}) {
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
        // The idle packet is 0xFF 0x00 with its checksum, as transmitted.
        std::vector<uint8_t> bytes = unpack_sent_packet(sent_track_packets[i]);
        if (!bytes.empty() && bytes[0] == 0xFF)
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

    // Test function command acknowledgment. <F> used to reply with a loco
    // status update that reported the function number as a speed (D5); it now
    // sends no reply at all -- the packet class has no way to report a
    // function map or the loco's real speed, and reporting the function
    // number as a speed is worse than reporting nothing.
    uart_test_write("<F 3 144 1>");
    controller.dccexLoop();

    assert_int_equal(uart_output_log.size(), 0);
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
    //
    // Uses the 3-field form. These commands previously read "<t 1 3 1 1>" -- the
    // deprecated 4-field form -- and relied on the parser misreading it as
    // cab=1, speed=3. That form is now rejected outright (#7), so the test would
    // have been exercising the queue with commands that never created a loco.
    uart_test_write("<t 3 1 1>");  // Throttle command
    controller.dccexLoop();
    
    // System should still be operational (power still on)
    // This confirms non-blocking queue operations are working
    assert_true(controller.isTrackPowerOn(false));
    assert_true(controller.isTrackPowerOn(true));
    
    // The fact that we can process multiple commands without blocking
    // demonstrates that the queue timeout safety mechanism is in place
    for (int i = 0; i < 5; i++) {
        uart_test_write("<t 3 1 1>");  // More commands
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
    uart_test_write("<t 3 1 1>");  // Add loco
    controller.dccexLoop();
    uart_test_write("<t 5 1 1>");  // Add another loco
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

    // Throttle command: rejected, and it must say so. A headless host cannot see
    // the LCD, so a discarded throttle that draws no reply is indistinguishable
    // from one that worked (#4).
    int initial_loco_count = controller.getLocoCount();
    uart_output_log.clear();
    uart_test_write("<t 3 50 1>");
    controller.dccexLoop();
    assert_int_equal(controller.getLocoCount(), initial_loco_count); // No new loco
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<X>");

    // Function command: same rule.
    uart_output_log.clear();
    uart_test_write("<F 3 8 1>");
    controller.dccexLoop();
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<X>");

    // Accessory command: same rule. Note this must be a *valid* accessory
    // command, or it is rejected one layer earlier by validatePacket() and never
    // reaches the maintenance-mode check at all.
    uart_output_log.clear();
    uart_test_write("<a 10 0 1>");
    controller.dccexLoop();
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<X>");
}

// A throttle command the collection cannot accept must not draw an affirmative
// reply. getDccExCabUpdate() is built from the *packet*, not from loco state, so
// the old code answered <l cab 0 speed 0> reporting the speed that had just been
// thrown away -- worse than silence, because the host is told it worked (#4).
static void test_rejected_throttle_does_not_get_an_affirmative_reply(void **state)
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

    // Fill the collection. Addresses start at 1 and every one is legal.
    for (int addr = 1; addr <= MAX_LOCO; addr++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "<t %d 50 1>", addr);
        uart_test_write(cmd);
        controller.dccexLoop();
    }
    assert_int_equal(controller.getLocoCount(), MAX_LOCO);

    // One more. The address and speed are both legal, so it passes
    // validatePacket() and is only refused by the collection being full.
    uart_output_log.clear();
    uart_test_write("<t 9999 50 1>");
    controller.dccexLoop();

    assert_int_equal(controller.getLocoCount(), MAX_LOCO);   // not added
    assert_int_equal(uart_output_log.size(), 1);
    assert_string_equal(uart_output_log[0].c_str(), "<X>");
    // Specifically: no <l ...> claiming the speed was set.
    assert_null(strstr(uart_output_log[0].c_str(), "<l"));
}

// The converse, so the rule cannot drift into "every command answers <X>": an
// accepted throttle still gets its <l> cab update and nothing else.
static void test_accepted_throttle_still_gets_its_cab_update(void **state)
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

    uart_output_log.clear();
    uart_test_write("<t 3 50 1>");
    controller.dccexLoop();

    assert_int_equal(uart_output_log.size(), 1);
    assert_non_null(strstr(uart_output_log[0].c_str(), "<l 3"));
    assert_null(strstr(uart_output_log[0].c_str(), "<X>"));
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
    uart_test_write("<t 3 50 1>");
    controller.dccexLoop();
    assert_int_equal(controller.getLocoCount(), initial_loco_count + 1); // Loco added
}

// Add all tests to the test suite

// ---------------------------------------------------------------------------
// Emergency stop response volume (issue #17)
//
// sendEmergencyStopResponses() emits one <l ...> response per known locomotive,
// and it does so while holding the locomotive lock. Core 1 takes that same lock
// every pass through getNextReminder(), so for the duration of these writes it
// generates no DCC packets at all.
//
// uart_puts() is instantaneous in the mock, so this test cannot measure the
// stall directly. What it can pin down is the volume, which is what the stall
// is proportional to: with MAX_LOCO at 50 and ~16 bytes per response, a full
// table is ~800 bytes, or roughly 70-90ms at 115200 baud -- against the 100ms
// timing-violation cutoff in dccLoop().
// ---------------------------------------------------------------------------

static void test_ISSUE_17_emergency_stop_writes_one_response_per_loco(void **state)
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

    const int loco_count = 5;
    for (int i = 1; i <= loco_count; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "<t %d 20 1>", i);
        uart_test_write(cmd);
        controller.dccexLoop();
        controller.dccLoop();
    }
    assert_int_equal(controller.getLocoCount(), (size_t)loco_count);

    uart_output_log.clear();

    uart_test_write("<!>");
    controller.dccexLoop();

    // One response per locomotive, all emitted inside the critical section.
    int cab_updates = 0;
    for (size_t i = 0; i < uart_output_log.size(); i++) {
        if (uart_output_log[i].compare(0, 3, "<l ") == 0) {
            cab_updates++;
        }
    }
    assert_int_equal(cab_updates, loco_count);

    // And the table is cleared afterwards, so no reminders follow.
    assert_int_equal(controller.getLocoCount(), 0);
}

// ---------------------------------------------------------------------------
// Construction asserts
//
// PicoDccController's constructor asserts that the two tracks do not share a
// signal pin, control pin or ADC channel. The mock used to discard every
// assert, so these had never once been exercised. They are recorded now.
//
// Note these are assert() and therefore compiled out of a release firmware
// build under NDEBUG -- they are a development guard, not a runtime one.
// ---------------------------------------------------------------------------

static void test_controller_construction_fires_no_asserts(void **state)
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

    mock_reset_asserts();
    PicoDccController controller(main_track, prog_track, 25);

    assert_int_equal(mock_assert_failures, 0);
}

static void test_controller_rejects_colliding_pins(void **state)
{
    track_settings_t main_track;
    main_track.signal_pin = 18;
    main_track.ctrl_pin = 22;
    main_track.adc_num = 0;
    main_track.short_pin = 16;

    // Same signal pin on both tracks: two PIO state machines driving one pin.
    track_settings_t prog_track;
    prog_track.signal_pin = 18;
    prog_track.ctrl_pin = 21;
    prog_track.adc_num = 1;
    prog_track.short_pin = 17;

    mock_reset_asserts();
    PicoDccController controller(main_track, prog_track, 25);

    assert_true(mock_assert_failures > 0);
}

// ---------------------------------------------------------------------------
// Throttle validation end to end (#2, #11, #12, #16)
// ---------------------------------------------------------------------------

// Before the fix, a throttle command with an out-of-range speed (999) reached
// PicoDccLoco's throwing constructor with no handler anywhere on the path, and
// std::terminate() aborted the process running this test. Reaching the
// assertions below at all -- across all four malformed inputs -- is as much
// the point of this test as what they assert.
static void test_ISSUE_2_rejected_throttle_does_not_abort(void **state)
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

    uart_test_write("<1>");
    controller.dccexLoop();
    uart_output_log.clear();
    queued_commands.clear();

    uart_test_write("<t 0 126 1>");       // cab 0: broadcast address (#12)
    controller.dccexLoop();
    uart_test_write("<t 65535 126 1>");   // above the 14-bit address space (#16)
    controller.dccexLoop();
    uart_test_write("<t 3 999 1>");       // speed out of range -- used to abort here
    controller.dccexLoop();
    uart_test_write("<t 3>");             // malformed: sscanf sentinel path
    controller.dccexLoop();

    assert_int_equal(controller.getLocoCount(), 0);
    for (const auto &output : uart_output_log) {
        assert_true(output.find("<l ") == std::string::npos);
    }
    assert_true(queued_commands.empty());
}

// D5: <F> must not write the function number into the loco's speed. Drive a
// loco to a stop, press a function key, and confirm every transmitted
// non-idle packet for that cab is still the stop encoding -- never a packet
// carrying speed 8.
static void test_function_command_does_not_queue_a_speed_change(void **state)
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

    // sent_track_packets is a global that this suite's other tests leave
    // populated -- they only ever check for the presence of a match, so the
    // accumulation is harmless there. This test checks for the *absence* of a
    // bad packet, so it needs a clean slate.
    extern std::vector<uint64_t> sent_track_packets;
    sent_track_packets.clear();

    uart_test_write("<1>");
    controller.dccexLoop();

    uart_test_write("<t 3 0 1>");
    controller.dccexLoop();
    // Drain fully: the repeat/interleave logic moves one command per
    // dccexLoop() pass, so a single pass is not enough to guarantee the
    // explicit command (repeats = 3) has actually reached the rails.
    for (int i = 0; i < 8; i++) {
        controller.dccexLoop();
        controller.dccLoop();
    }

    uart_test_write("<F 3 8 1>");
    controller.dccexLoop();
    for (int i = 0; i < 8; i++) {
        controller.dccexLoop();
        controller.dccLoop();
    }

    bool found_cab3 = false;
    for (size_t i = 0; i < sent_track_packets.size(); ++i) {
        std::vector<uint8_t> bytes = unpack_sent_packet(sent_track_packets[i]);
        if (bytes.empty() || bytes[0] != 0x03) {
            continue;
        }
        found_cab3 = true;
        assert_true(bytes == (std::vector<uint8_t>{0x03, 0x60, 0x63}));
    }
    assert_true(found_cab3);
}


// ─── Power-cutoff reporting (#4) and latched indication (#42) ────────────────

// Counts how many times `needle` appears in the UART log. The host cannot act
// on what it is never told, and it also cannot act sensibly on being told 100
// times a second -- both directions matter, so both are asserted.
static int uart_count(const char *needle)
{
    int n = 0;
    for (const std::string &line : uart_output_log) {
        if (line.find(needle) != std::string::npos) n++;
    }
    return n;
}

// Builds the standard two-track fixture used by every test below.
static void power_fault_fixture(track_settings_t *main_track, track_settings_t *prog_track)
{
    main_track->signal_pin = 18;
    main_track->ctrl_pin = 22;
    main_track->adc_num = 0;
    main_track->short_pin = 16;

    prog_track->signal_pin = 19;
    prog_track->ctrl_pin = 21;
    prog_track->adc_num = 1;
    prog_track->short_pin = 17;
}

// #4 section 1: the layout went dark and the wire stayed quiet. The orchestrator
// on the other end is a safety-cased control system whose first rule is that
// uncertainty must halt movement -- it cannot act on a fault it is never told
// about, and would go on issuing throttle commands into dead rails.
static void test_ISSUE_4_timing_cutoff_is_reported_on_the_wire(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();
    assert_true(controller.isTrackPowerOn(false));

    mock_time_ms = 10;
    controller.dccLoop();

    uart_output_log.clear();

    // Core 1 trips the cutoff.
    mock_time_ms = 150;
    controller.dccLoop();
    assert_false(controller.isTrackPowerOn(false));

    // Core 1 must NOT have written to the UART: dccLoop() is the DCC hot path,
    // and a blocking uart_puts() there is the very stall the timing monitor
    // exists to catch.
    assert_int_equal(uart_count("<p0"), 0);

    // Core 0 is what tells the host.
    controller.dccexLoop();
    assert_int_equal(uart_count("<p0 MAIN>"), 1);
    assert_int_equal(uart_count("<p0 PROG>"), 1);
}

// The report is once per cutoff, not once per pass. The cutoff condition is
// re-evaluated every 10ms and holds for as long as power is off, so announcing
// unconditionally would put 100 frames a second on a link JMRI also speaks.
static void test_ISSUE_4_cutoff_is_reported_once_not_per_pass(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 10;
    controller.dccLoop();

    uart_output_log.clear();

    for (uint32_t t = 150; t < 400; t += 10) {
        mock_time_ms = t;
        controller.dccLoop();
        controller.dccexLoop();
    }

    assert_int_equal(uart_count("<p0 MAIN>"), 1);
    assert_int_equal(uart_count("<p0 PROG>"), 1);
}

// #42: the two halves of the old cutoff had different lifetimes. Cutting power
// stops commands being sent, so on the very next pass the gap is small again,
// the `else` branch ran, and the LED went out -- leaving both tracks unpowered,
// the error LED off, and a system that looked healthy. The only surviving
// evidence was a LOG_CRITICAL on a screen nobody looks at first.
static void test_ISSUE_42_error_led_stays_lit_after_the_cutoff(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 10;
    controller.dccLoop();

    mock_time_ms = 150;
    controller.dccLoop();
    assert_true(gpio_states[25]);

    // Exactly the passes that used to clear it: power is off, so no commands are
    // being sent, so the measured gap is small and the PIO reads healthy again.
    for (uint32_t t = 160; t < 400; t += 10) {
        mock_time_ms = t;
        controller.dccLoop();
        assert_true(gpio_states[25]);       // The indication survives
        assert_false(controller.isTrackPowerOn(false));  // ...and so does the cutoff
    }

    assert_true(controller.isPowerFaultLatched());
}

// The latch is not a one-way door. Restoring power deliberately -- <1>, or the
// LCD's own power control -- clears the fault indication, mirroring how
// PicoDccTrack::setPower(true) clears `tripped`.
static void test_ISSUE_42_deliberate_power_restore_clears_the_indication(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 10;
    controller.dccLoop();
    mock_time_ms = 150;
    controller.dccLoop();
    assert_true(gpio_states[25]);

    // The operator restores power. Core 1 observes it on its next pass.
    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 155;
    controller.dccLoop();

    assert_false(controller.isPowerFaultLatched());
    assert_false(gpio_states[25]);
}

// ...and restoring power into a station that is still faulty re-trips, and
// tells the host again. A cutoff that could be cleared without being fixed
// would be worse than one that never cleared.
static void test_ISSUE_4_restoring_power_into_a_live_fault_re_reports(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 10;
    controller.dccLoop();
    mock_time_ms = 150;
    controller.dccLoop();
    controller.dccexLoop();

    uart_output_log.clear();

    // Power back on, but nothing has sent a command, so the gap is still fatal.
    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 160;
    controller.dccLoop();   // observes power restored, clears the latch
    mock_time_ms = 300;
    controller.dccLoop();   // gap is fatal again -- re-trips
    controller.dccexLoop();

    assert_false(controller.isTrackPowerOn(false));
    assert_true(controller.isPowerFaultLatched());
    assert_int_equal(uart_count("<p0 MAIN>"), 1);
}

// The Core 1 heartbeat cutoff runs on Core 0, through emergencyPowerCutoff().
// It was equally silent, and is the case where the host is least able to guess:
// Core 1 has stopped, so the rails are dark AND nothing is generating packets.
static void test_ISSUE_4_core1_heartbeat_cutoff_is_reported(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();

    // Core 1 ticks once, so the monitor sees it alive...
    mock_time_ms = 10;
    controller.dccLoop();
    mock_time_ms = 60;
    controller.dccexLoop();

    uart_output_log.clear();

    // ...and then stops. Two Core 0 passes 50ms apart with no Core 1 tick.
    mock_time_ms = 120;
    controller.dccexLoop();
    mock_time_ms = 180;
    controller.dccexLoop();

    assert_false(controller.isTrackPowerOn(false));
    assert_true(gpio_states[25]);
    assert_int_equal(uart_count("<p0 MAIN>"), 1);
    assert_int_equal(uart_count("<p0 PROG>"), 1);
}

// Boot is not a fault. Nothing has cut power, so nothing is announced and the
// LED stays dark -- the guard that this latch did not just make every startup
// report a cutoff.
static void test_no_power_fault_reported_on_a_clean_boot(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_output_log.clear();

    mock_time_ms = 5;
    controller.dccexLoop();
    controller.dccLoop();

    assert_false(controller.isPowerFaultLatched());
    assert_false(gpio_states[25]);
    assert_int_equal(uart_count("<p0"), 0);
}


// An overcurrent trip is the cutoff most likely to actually happen in service --
// a derailment shorting the rails -- and it was silent too. It happens inside
// PicoDccTrack::loop(), so the controller picks it up from `tripped` rather than
// causing it.
//
// The programming track can trip ON ITS OWN while the main track stays live, and
// that case is why the announcement reports each track's real state instead of
// assuming a cutoff means both are dark. Saying <p0 MAIN> here would be a lie
// about the one track that matters most.
static void test_ISSUE_4_prog_only_overcurrent_reports_each_track_honestly(void **state)
{
    track_settings_t main_track, prog_track;
    power_fault_fixture(&main_track, &prog_track);
    PicoDccController controller(main_track, prog_track, 25);

    uart_test_write("<1>");
    controller.dccexLoop();
    mock_time_ms = 10;
    controller.dccLoop();

    uart_output_log.clear();

    // Programming track (adc_num 1) goes over the trip threshold; main is fine.
    mock_adc_set_channel(1, TRACK_POWER_TRIP_THRESHOLD + 1);
    mock_time_ms = 20;
    controller.dccLoop();
    controller.dccexLoop();

    assert_true(controller.isTrackPowerOn(false));   // Main still live
    assert_false(controller.isTrackPowerOn(true));   // Prog tripped out
    assert_true(controller.isPowerFaultLatched());
    assert_true(gpio_states[25]);

    assert_int_equal(uart_count("<p1 MAIN>"), 1);
    assert_int_equal(uart_count("<p0 PROG>"), 1);
    assert_int_equal(uart_count("<p0 MAIN>"), 0);

    // The latch must not flip-flop. `tripped` persists until power is
    // deliberately restored, and the main track's power never went away -- so a
    // clear condition that only looked at the main track would clear the latch
    // on this same pass and re-raise it on the next, announcing a fault at the
    // loop rate for as long as the short lasted.
    for (uint32_t t = 30; t < 200; t += 10) {
        mock_time_ms = t;
        controller.dccLoop();
        controller.dccexLoop();
    }

    assert_true(controller.isPowerFaultLatched());
    assert_int_equal(uart_count("<p1 MAIN>"), 1);
    assert_int_equal(uart_count("<p0 PROG>"), 1);
}

int main(void)
{
    printf("Running Controller Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_timing_safety_cutoff, setup, teardown),
        cmocka_unit_test_setup_teardown(test_dcc_millis_deltas_survive_the_old_wrap_point, setup, teardown),
        cmocka_unit_test_setup_teardown(test_no_false_timing_violation_across_the_71_minute_wrap, setup, teardown),
        cmocka_unit_test_setup_teardown(test_real_timing_violation_still_detected_after_the_wrap, setup, teardown),
        cmocka_unit_test_setup_teardown(test_no_false_cutoff_during_boot_delay, setup, teardown),
        cmocka_unit_test_setup_teardown(test_core1_that_never_starts_still_cuts_power, setup, teardown),
        cmocka_unit_test_setup_teardown(test_version_reply_matches_startup_banner, setup, teardown),
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
        cmocka_unit_test_setup_teardown(test_rejected_throttle_does_not_get_an_affirmative_reply, setup, teardown),
        cmocka_unit_test_setup_teardown(test_accepted_throttle_still_gets_its_cab_update, setup, teardown),
        cmocka_unit_test_setup_teardown(test_maintenance_mode_exit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_17_emergency_stop_writes_one_response_per_loco, setup, teardown),
        cmocka_unit_test_setup_teardown(test_controller_construction_fires_no_asserts, setup, teardown),
        cmocka_unit_test_setup_teardown(test_controller_rejects_colliding_pins, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_2_rejected_throttle_does_not_abort, setup, teardown),
        cmocka_unit_test_setup_teardown(test_function_command_does_not_queue_a_speed_change, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_4_timing_cutoff_is_reported_on_the_wire, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_4_cutoff_is_reported_once_not_per_pass, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_42_error_led_stays_lit_after_the_cutoff, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_42_deliberate_power_restore_clears_the_indication, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_4_restoring_power_into_a_live_fault_re_reports, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_4_core1_heartbeat_cutoff_is_reported, setup, teardown),
        cmocka_unit_test_setup_teardown(test_no_power_fault_reported_on_a_clean_boot, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ISSUE_4_prog_only_overcurrent_reports_each_track_honestly, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}