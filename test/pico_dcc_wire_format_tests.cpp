// Wire-format characterization tests.
//
// Every other suite tests a component's behaviour. This one tests the *bytes* --
// what actually reaches the rails, and what actually goes back to the host --
// for a fixed set of inputs.
//
// It exists because the throttle encoding was arrived at empirically while
// driving the station from JMRI, and the reasoning was never written down. Some
// of what is asserted below is correct and some of it has an open issue against
// it. The point is not to settle that here; it is that any change to the
// encoding shows up as a diff in this file, so nothing a real host was relying
// on can move without somebody noticing.
//
// Every value below was measured from the current implementation, not derived
// by hand. When fixing one of the referenced issues, expect to change the
// corresponding assertion -- that is the intended workflow. An assertion
// changing here is a prompt to re-test against real hardware, not a test to be
// quietly updated.

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCLoco/pico_dcclocos.h"
#include "../lib/PicoDCCEX/pico_dccexpacket.h"
#include "../lib/pico_diagnostic.h"

extern std::vector<std::string> uart_output_log;

// ---------------------------------------------------------------------------
// Helpers
//
// PicoDccExPacket takes a mutable char*, so each helper needs its own buffer.
// Copied byte by byte rather than with strncpy(), per the memory-safety rules
// in CLAUDE.md -- these run on the host, but the idiom gets copied.
// ---------------------------------------------------------------------------

static void copy_command(char *dest, size_t dest_size, const char *src)
{
    size_t i = 0;
    for (; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// The DCC packet a freshly-seen locomotive produces (the addLoco path).
static raw_dcc_cmd_t throttle_for(const char *command)
{
    char buffer[64];
    copy_command(buffer, sizeof(buffer), command);

    PicoDccLocos locos;
    PicoDccExPacket packet(buffer);
    raw_dcc_cmd_t cmd = {};
    locos.addLoco(&packet, cmd);
    return cmd;
}

// The DCC packet produced when an *already known* locomotive is updated. This is
// a different code path from throttle_for(): it goes through
// PicoDccLoco::update(), which validates nothing.
static raw_dcc_cmd_t throttle_update_for(const char *first, const char *second)
{
    char buf1[64];
    char buf2[64];
    copy_command(buf1, sizeof(buf1), first);
    copy_command(buf2, sizeof(buf2), second);

    PicoDccLocos locos;
    PicoDccExPacket p1(buf1);
    raw_dcc_cmd_t cmd = {};
    locos.addLoco(&p1, cmd);

    PicoDccExPacket p2(buf2);
    raw_dcc_cmd_t updated = {};
    locos.updateLocoThrottle(p2.getCab(), &p2, updated);
    return updated;
}

static raw_dcc_cmd_t accessory_for(const char *command)
{
    char buffer[64];
    copy_command(buffer, sizeof(buffer), command);

    PicoDccExPacket packet(buffer);
    return *packet.getRawDccAccessoryCmd();
}

static int setup(void **state)
{
    (void)state;
    uart_output_log.clear();
    diag_log_init();
    return 0;
}

// ---------------------------------------------------------------------------
// Speed and direction encoding
//
// generateThrottleCommand() converts the 128-step speed off the wire into a
// 28-step packet; issue #8 covers the missing 128-step support. The byte is
// 01DCSSSS: bits 7-6 select the speed/direction instruction, bit 5 is
// direction, and the 5-bit speed value is (SSSS << 1) | C.
// ---------------------------------------------------------------------------

static void test_speed_zero_encodes_as_a_stop(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 0 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x03);

    // 0x71 == 0b0111_0001: forward, SSSS=0001, C=1, so the 5-bit speed value is
    // 3. That is a stop encoding rather than a moving step -- the locomotive
    // does stop. It is the emergency-stop form rather than the controlled-stop
    // form (value 0), which is worth confirming at the bench when issue #11 is
    // addressed, since that fix has to pick an emergency-stop encoding too.
    assert_int_equal(cmd.data[1], 0x71);
}

static void test_speed_zero_reverse(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 0 0");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x51);  // same as forward, direction bit clear
}

static void test_speed_mid_range(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 10 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x72);
}

static void test_speed_max_128_step_input(void **state)
{
    (void)state;
    // 126 is the highest speed a DCC-EX host sends.
    raw_dcc_cmd_t cmd = throttle_for("t 3 126 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x7F);  // 28-step code 31, full speed forward
}

static void test_low_speeds_quantise_together(void **state)
{
    (void)state;
    // 128 steps folded into 28 means adjacent inputs collide. Speeds 1 and 2
    // both land on 28-step code 2. This is the resolution loss issue #8 is
    // about, pinned here so a 128-step implementation shows up as a change.
    assert_int_equal(throttle_for("t 3 1 1").data[1], 0x62);
    assert_int_equal(throttle_for("t 3 2 1").data[1], 0x62);
}

static void test_direction_bit_is_0x20(void **state)
{
    (void)state;
    raw_dcc_cmd_t fwd = throttle_for("t 3 60 1");
    raw_dcc_cmd_t rev = throttle_for("t 3 60 0");

    assert_int_equal(fwd.data[1], 0x68);
    assert_int_equal(rev.data[1], 0x48);
    assert_int_equal(fwd.data[1] ^ rev.data[1], 0x20);
}

// ---------------------------------------------------------------------------
// Address encoding
// ---------------------------------------------------------------------------

static void test_short_address_is_one_byte(void **state)
{
    (void)state;
    // 127 is HIGHEST_SHORT_ADDR: still a single address byte.
    raw_dcc_cmd_t cmd = throttle_for("t 127 0 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x7F);
    assert_int_equal(cmd.data[1], 0x71);
}

static void test_long_address_is_two_bytes(void **state)
{
    (void)state;
    // 128 crosses into long-address territory: 0xC0 | high byte, then low byte.
    raw_dcc_cmd_t cmd = throttle_for("t 128 0 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0xC0);
    assert_int_equal(cmd.data[1], 0x80);
    assert_int_equal(cmd.data[2], 0x71);
}

static void test_highest_legal_long_address(void **state)
{
    (void)state;
    // 10239 is the highest address the DCC long-address scheme defines, and
    // 0xE7 is the top of the valid long-address prefix range.
    raw_dcc_cmd_t cmd = throttle_for("t 10239 0 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0xE7);
    assert_int_equal(cmd.data[1], 0xFF);
    assert_int_equal(cmd.data[2], 0x71);
}

// ---------------------------------------------------------------------------
// Repeat semantics
// ---------------------------------------------------------------------------

static void test_explicit_command_repeats_three_times(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 20 1");

    assert_int_equal(cmd.repeats, 3);
    assert_false(cmd.is_prog);
}

static void test_reminder_does_not_repeat(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 3 20 1");

    PicoDccLocos locos;
    PicoDccExPacket packet(buffer);
    raw_dcc_cmd_t cmd = {};
    locos.addLoco(&packet, cmd);

    raw_dcc_cmd_t reminder = {};
    assert_true(locos.getNextReminder(reminder));

    // Reminders are hardware-paced: repeating them would double-count against
    // the single-buffered hardware queue.
    assert_int_equal(reminder.repeats, 0);
    // Identical bytes to the explicit command otherwise.
    assert_int_equal(reminder.length, cmd.length);
    assert_int_equal(reminder.data[0], cmd.data[0]);
    assert_int_equal(reminder.data[1], cmd.data[1]);
}

static void test_reminders_rotate_between_locos(void **state)
{
    (void)state;
    char b3[64];
    char b4[64];
    copy_command(b3, sizeof(b3), "t 3 20 1");
    copy_command(b4, sizeof(b4), "t 4 20 1");

    PicoDccLocos locos;
    raw_dcc_cmd_t cmd = {};
    PicoDccExPacket p3(b3);
    locos.addLoco(&p3, cmd);
    PicoDccExPacket p4(b4);
    locos.addLoco(&p4, cmd);

    raw_dcc_cmd_t first = {};
    raw_dcc_cmd_t second = {};
    raw_dcc_cmd_t third = {};
    assert_true(locos.getNextReminder(first));
    assert_true(locos.getNextReminder(second));
    assert_true(locos.getNextReminder(third));

    // Round-robin, so no locomotive is starved of reminders.
    assert_int_not_equal(first.data[0], second.data[0]);
    assert_int_equal(first.data[0], third.data[0]);
}

// ---------------------------------------------------------------------------
// Accessory encoding
// ---------------------------------------------------------------------------

static void test_accessory_low_address(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = accessory_for("a 1 0 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x81);
    assert_int_equal(cmd.data[1], 0x89);
    assert_int_equal(cmd.repeats, 3);
}

// ---------------------------------------------------------------------------
// Responses to the host
//
// These are what JMRI actually parses, so they matter as much as the rail bytes.
// ---------------------------------------------------------------------------

static void test_cab_update_response_format(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 3 10 1");
    PicoDccExPacket packet(buffer);

    // <l cab reg speedByte functMap>. The speed byte carries direction in bit 7
    // and the 128-step value shifted down by one below it: 10 -> 9 | 0x80 = 137.
    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 137 0>");
}

static void test_cab_update_speed_extremes(void **state)
{
    (void)state;
    char zero[64];
    copy_command(zero, sizeof(zero), "t 3 0 1");
    PicoDccExPacket p_zero(zero);
    assert_string_equal(p_zero.getDccExCabUpdate(), "<l 3 0 128 0>");

    char full[64];
    copy_command(full, sizeof(full), "t 3 126 1");
    PicoDccExPacket p_full(full);
    assert_string_equal(p_full.getDccExCabUpdate(), "<l 3 0 253 0>");
}

static void test_cab_update_speed_one_reports_one_not_estop(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 3 1 1");
    PicoDccExPacket packet(buffer);

    // getDccExCabUpdate() computes an emergency-stop value for wire speed 1 into
    // a local `responseSpeed` and then never uses it (the compiler reports it as
    // set-but-unused). The reported speed is a plain 1, i.e. 0x80 | 1 = 129.
    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 129 0>");
}

static void test_power_update_response_format(void **state)
{
    (void)state;
    char all[64];
    copy_command(all, sizeof(all), "1");
    PicoDccExPacket p_all(all);
    assert_string_equal(p_all.getDccExPowerUpdate(), "<p1>");

    char main_on[64];
    copy_command(main_on, sizeof(main_on), "1 MAIN");
    PicoDccExPacket p_main(main_on);
    assert_string_equal(p_main.getDccExPowerUpdate(), "<p1 MAIN>");

    char prog_off[64];
    copy_command(prog_off, sizeof(prog_off), "0 PROG");
    PicoDccExPacket p_prog(prog_off);
    assert_string_equal(p_prog.getDccExPowerUpdate(), "<p0 PROG>");
}

// ---------------------------------------------------------------------------
// Current behaviour of open defects
//
// Each of these asserts what the firmware does *today*, naming the issue that
// covers it. They are here so that a fix shows up as an explicit, reviewable
// change to the wire format rather than a silent one.
// ---------------------------------------------------------------------------

static void test_ISSUE_11_speed_minus_one_currently_means_full_speed(void **state)
{
    (void)state;
    // DCC-EX sends speed -1 to emergency-stop a single locomotive.
    raw_dcc_cmd_t cmd = throttle_update_for("t 3 10 1", "t 3 -1 1");

    // -1 becomes uint8_t 255, masked to 127, encoded as 28-step code 31.
    // 0x7F is maximum speed -- byte for byte what "t 3 126 1" produces.
    assert_int_equal(cmd.data[1], 0x7F);
    assert_int_equal(cmd.data[1], throttle_for("t 3 126 1").data[1]);
}

static void test_ISSUE_12_cab_zero_currently_emits_a_broadcast(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 0 126 1");
    PicoDccExPacket packet(buffer);

    // Accepted as valid; no address range check exists for opcode 't'.
    assert_true(packet.isValid());

    raw_dcc_cmd_t cmd = throttle_for("t 0 126 1");
    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x00);  // address 0 == DCC broadcast
    assert_int_equal(cmd.data[1], 0x7F);  // at full speed
}

static void test_ISSUE_16_address_above_14_bits_emits_idle_address(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 65535 126 1");

    // (65535 >> 8) | 0xC0 == 0xFF, which is the idle packet address rather than
    // anything in the 0xC0-0xE7 long-address range.
    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0xFF);
    assert_int_equal(cmd.data[1], 0xFF);
}

static void test_ISSUE_15_accessory_high_bits_not_ones_complemented(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = accessory_for("a 100 0 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0xA4);
    // NMRA S-9.2.1 requires the AAA field (bits 4-6) in ones complement, which
    // would make this 0xF9. It is emitted uninverted as 0x99, so any accessory
    // address of 64 or above reaches the wrong decoder.
    assert_int_equal(cmd.data[1], 0x99);
}

static void test_ISSUE_15_accessory_activate_bit_is_stuck_on(void **state)
{
    (void)state;
    raw_dcc_cmd_t activate = accessory_for("a 100 0 1");
    raw_dcc_cmd_t deactivate = accessory_for("a 100 0 0");

    // The C bit (0x08) is hardcoded on, so it is set in both. The activate
    // parameter lands in bit 0 instead, which is an output-select bit.
    assert_int_equal(activate.data[1] & 0x08, 0x08);
    assert_int_equal(deactivate.data[1] & 0x08, 0x08);
    assert_int_equal(activate.data[1] ^ deactivate.data[1], 0x01);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    printf("Running Wire Format Characterization Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_speed_zero_encodes_as_a_stop, setup),
        cmocka_unit_test_setup(test_speed_zero_reverse, setup),
        cmocka_unit_test_setup(test_speed_mid_range, setup),
        cmocka_unit_test_setup(test_speed_max_128_step_input, setup),
        cmocka_unit_test_setup(test_low_speeds_quantise_together, setup),
        cmocka_unit_test_setup(test_direction_bit_is_0x20, setup),
        cmocka_unit_test_setup(test_short_address_is_one_byte, setup),
        cmocka_unit_test_setup(test_long_address_is_two_bytes, setup),
        cmocka_unit_test_setup(test_highest_legal_long_address, setup),
        cmocka_unit_test_setup(test_explicit_command_repeats_three_times, setup),
        cmocka_unit_test_setup(test_reminder_does_not_repeat, setup),
        cmocka_unit_test_setup(test_reminders_rotate_between_locos, setup),
        cmocka_unit_test_setup(test_accessory_low_address, setup),
        cmocka_unit_test_setup(test_cab_update_response_format, setup),
        cmocka_unit_test_setup(test_cab_update_speed_extremes, setup),
        cmocka_unit_test_setup(test_cab_update_speed_one_reports_one_not_estop, setup),
        cmocka_unit_test_setup(test_power_update_response_format, setup),
        cmocka_unit_test_setup(test_ISSUE_11_speed_minus_one_currently_means_full_speed, setup),
        cmocka_unit_test_setup(test_ISSUE_12_cab_zero_currently_emits_a_broadcast, setup),
        cmocka_unit_test_setup(test_ISSUE_16_address_above_14_bits_emits_idle_address, setup),
        cmocka_unit_test_setup(test_ISSUE_15_accessory_high_bits_not_ones_complemented, setup),
        cmocka_unit_test_setup(test_ISSUE_15_accessory_activate_bit_is_stuck_on, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
