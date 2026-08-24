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

// The DCC packet a freshly-seen locomotive produces (the addLoco path), in a
// named speed step mode. New locos inherit the station default, so setting that
// first is what selects the encoding under test.
static raw_dcc_cmd_t throttle_for_mode(const char *command, uint8_t steps)
{
    char buffer[64];
    copy_command(buffer, sizeof(buffer), command);

    PicoDccLocos locos;
    locos.setStationSpeedSteps(steps);
    PicoDccExPacket packet(buffer);
    raw_dcc_cmd_t cmd = {};
    locos.addLoco(&packet, cmd);
    return cmd;
}

// The same, in whatever mode the station starts in -- 128 steps (#8).
static raw_dcc_cmd_t throttle_for(const char *command)
{
    return throttle_for_mode(command, DCC_SPEED_STEPS_128);
}

// The DCC packet produced when an *already known* locomotive is updated. This is
// a different code path from throttle_for(): it goes through
// PicoDccLoco::update(), which validates nothing.
static raw_dcc_cmd_t throttle_update_for_mode(const char *first, const char *second,
                                             uint8_t steps)
{
    char buf1[64];
    char buf2[64];
    copy_command(buf1, sizeof(buf1), first);
    copy_command(buf2, sizeof(buf2), second);

    PicoDccLocos locos;
    locos.setStationSpeedSteps(steps);
    PicoDccExPacket p1(buf1);
    raw_dcc_cmd_t cmd = {};
    locos.addLoco(&p1, cmd);

    PicoDccExPacket p2(buf2);
    raw_dcc_cmd_t updated = {};
    locos.updateLocoThrottle(p2.getCab(), &p2, updated);
    return updated;
}

static raw_dcc_cmd_t throttle_update_for(const char *first, const char *second)
{
    return throttle_update_for_mode(first, second, DCC_SPEED_STEPS_128);
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
// Speed and direction encoding -- 128 steps (the default, #8)
//
// S-9.2.1 advanced operations: instruction byte 0x3F, then one byte of
// (direction << 7) | value. Value 0 is the controlled stop, 1 the emergency
// stop, and 2..127 are speed steps 1..126 -- so a <t> wire speed of N encodes
// as N + 1, while a wire speed of 0 encodes as 0.
//
// This is a three-byte payload for a short address where the 28-step form was
// two, so the speed byte is data[2] here and data[1] in the fallback section.
// ---------------------------------------------------------------------------

static void test_speed_zero_encodes_as_a_stop(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 0 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x3F);

    // 0x80: forward, value 0 -- the controlled stop. The locomotive decelerates
    // under its own momentum CV rather than slamming to a halt.
    assert_int_equal(cmd.data[2], 0x80);
}

static void test_speed_zero_reverse(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 0 0");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x3F);
    assert_int_equal(cmd.data[2], 0x00);  // same as forward, direction bit clear
}

static void test_speed_mid_range(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 3 10 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[2], 0x80 | 11);  // wire speed 10 -> value 11
}

static void test_speed_max_128_step_input(void **state)
{
    (void)state;
    // 126 is the highest speed a DCC-EX host sends, and 127 is the highest
    // value the instruction can carry. The two now line up exactly.
    raw_dcc_cmd_t cmd = throttle_for("t 3 126 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[2], 0x80 | 127);
}

// The point of #8. Every wire speed produces a distinct packet, so a one-step
// change the host asks for is a one-step change the decoder sees.
static void test_adjacent_speeds_are_distinct(void **state)
{
    (void)state;
    assert_int_equal(throttle_for("t 3 1 1").data[2], 0x80 | 2);
    assert_int_equal(throttle_for("t 3 2 1").data[2], 0x80 | 3);
    assert_int_not_equal(throttle_for("t 3 1 1").data[2],
                         throttle_for("t 3 2 1").data[2]);
}

// Every one of the 127 values a host can send maps to its own byte, and none
// of them collides with the emergency stop. That is the property the
// orchestrator's braking model and its crawl_speed_step depend on, so it is
// asserted exhaustively rather than at a few sample points.
static void test_every_wire_speed_maps_to_a_distinct_byte(void **state)
{
    (void)state;
    bool seen[256] = {false};

    for (int wire = 0; wire <= DCC_MAX_THROTTLE_SPEED; wire++) {
        char command[32];
        snprintf(command, sizeof(command), "t 3 %d 1", wire);
        raw_dcc_cmd_t cmd = throttle_for(command);

        assert_int_equal(cmd.length, 3);
        assert_int_equal(cmd.data[1], 0x3F);

        uint8_t value = (uint8_t)(cmd.data[2] & 0x7F);
        assert_int_equal(cmd.data[2] & 0x80, 0x80);   // direction preserved
        assert_false(seen[value]);                    // no two speeds collide
        assert_int_not_equal(value, 1);               // never the estop value
        seen[value] = true;
    }
}

static void test_direction_bit_is_0x80(void **state)
{
    (void)state;
    raw_dcc_cmd_t fwd = throttle_for("t 3 60 1");
    raw_dcc_cmd_t rev = throttle_for("t 3 60 0");

    assert_int_equal(fwd.data[2], 0x80 | 61);
    assert_int_equal(rev.data[2], 61);
    assert_int_equal(fwd.data[2] ^ rev.data[2], 0x80);
}

// #11: DCC-EX sends speed -1 to emergency-stop a single locomotive. In the
// 128-step instruction that is value 1, which is why no ordinary speed is ever
// allowed to encode as 1 (asserted exhaustively above).
static void test_speed_minus_one_is_an_emergency_stop(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_update_for("t 3 10 1", "t 3 -1 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x3F);
    assert_int_equal(cmd.data[2], 0x80 | 1);
    assert_int_not_equal(cmd.data[2], throttle_for("t 3 126 1").data[2]);
}

// #48: these two must stay different packets. A controlled stop is value 0, an
// emergency stop is value 1 -- one decelerates under the decoder's momentum CV,
// the other slams to a halt.
static void test_controlled_stop_differs_from_emergency_stop(void **state)
{
    (void)state;
    const uint8_t stop  = throttle_for("t 3 0 1").data[2];
    const uint8_t estop = throttle_update_for("t 3 10 1", "t 3 -1 1").data[2];

    assert_int_equal(stop, 0x80);
    assert_int_equal(estop, 0x81);
    assert_int_not_equal(stop, estop);

    // Both keep the direction bit, so the decoder still knows which way to go
    // when the throttle resumes.
    assert_int_equal(throttle_for("t 3 0 0").data[2], 0x00);
}

// ---------------------------------------------------------------------------
// Speed and direction encoding -- 28 steps (the per-loco fallback, #8)
//
// Retained for any decoder that cannot do 128 steps. S-9.2 speed and direction
// is 01DCSSSS, where the 5-bit speed value is (SSSS << 1) | C: value 0 is the
// controlled stop, 1 and 3 are emergency stop, 2 is the alternate stop, and a
// moving step N is value N + 3.
//
// These are the bytes this station emitted for every locomotive before #8, so
// they are unchanged from the values measured against JMRI -- they have simply
// stopped being the default.
// ---------------------------------------------------------------------------

static raw_dcc_cmd_t throttle28_for(const char *command)
{
    return throttle_for_mode(command, DCC_SPEED_STEPS_28);
}

static void test_28_step_speed_zero_encodes_as_a_stop(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle28_for("t 3 0 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[0], 0x03);

    // 0x60 == 0b0110_0000: forward, SSSS=0000, C=0, so the 5-bit speed value is
    // 0 -- the controlled stop. This used to emit 0x71 (value 3, emergency
    // stop), because the expression was written for moving steps and produced
    // garbage for step 0 (#48).
    assert_int_equal(cmd.data[1], 0x60);
}

static void test_28_step_speed_zero_reverse(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle28_for("t 3 0 0");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[1], 0x40);  // same as forward, direction bit clear
}

static void test_28_step_speed_mid_range(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle28_for("t 3 10 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[1], 0x72);
}

static void test_28_step_speed_max(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle28_for("t 3 126 1");

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[1], 0x7F);  // 28-step code 31, full speed forward
}

// The resolution loss that motivated #8, kept as a live assertion rather than a
// comment: in the fallback mode adjacent wire speeds still collide, and in the
// default mode (asserted above) they no longer do.
static void test_28_step_low_speeds_quantise_together(void **state)
{
    (void)state;
    assert_int_equal(throttle28_for("t 3 1 1").data[1], 0x62);
    assert_int_equal(throttle28_for("t 3 2 1").data[1], 0x62);
}

static void test_28_step_direction_bit_is_0x20(void **state)
{
    (void)state;
    raw_dcc_cmd_t fwd = throttle28_for("t 3 60 1");
    raw_dcc_cmd_t rev = throttle28_for("t 3 60 0");

    assert_int_equal(fwd.data[1], 0x68);
    assert_int_equal(rev.data[1], 0x48);
    assert_int_equal(fwd.data[1] ^ rev.data[1], 0x20);
}

static void test_28_step_speed_minus_one_is_an_emergency_stop(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_update_for_mode("t 3 10 1", "t 3 -1 1",
                                                DCC_SPEED_STEPS_28);

    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[1], 0x61);
    assert_int_not_equal(cmd.data[1], throttle28_for("t 3 126 1").data[1]);
}

// The two encodings are easy to conflate, so pin them side by side for one
// requested speed: same loco, same command, different mode, different bytes.
static void test_the_two_encodings_differ_for_the_same_speed(void **state)
{
    (void)state;
    raw_dcc_cmd_t s128 = throttle_for_mode("t 3 60 1", DCC_SPEED_STEPS_128);
    raw_dcc_cmd_t s28  = throttle_for_mode("t 3 60 1", DCC_SPEED_STEPS_28);

    assert_int_equal(s128.length, 3);
    assert_int_equal(s128.data[1], 0x3F);
    assert_int_equal(s128.data[2], 0x80 | 61);

    assert_int_equal(s28.length, 2);
    assert_int_equal(s28.data[1], 0x68);
}

// ---------------------------------------------------------------------------
// Address encoding
// ---------------------------------------------------------------------------

static void test_short_address_is_one_byte(void **state)
{
    (void)state;
    // 127 is HIGHEST_SHORT_ADDR: still a single address byte.
    raw_dcc_cmd_t cmd = throttle_for("t 127 0 1");

    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0x7F);
    assert_int_equal(cmd.data[1], 0x3F);
    assert_int_equal(cmd.data[2], 0x80);
}

static void test_long_address_is_two_bytes(void **state)
{
    (void)state;
    // 128 crosses into long-address territory: 0xC0 | high byte, then low byte.
    // Two address bytes plus the two-byte 128-step instruction is the longest
    // throttle payload this station emits: four bytes plus the checksum, which
    // is exactly what DCC_MAX_DATA_BYTES and DCC_PACKET_FIRST_BYTE allow (#8).
    raw_dcc_cmd_t cmd = throttle_for("t 128 0 1");

    assert_int_equal(cmd.length, 4);
    assert_int_equal(cmd.data[0], 0xC0);
    assert_int_equal(cmd.data[1], 0x80);
    assert_int_equal(cmd.data[2], 0x3F);
    assert_int_equal(cmd.data[3], 0x80);

    // The payload plus its checksum must still fit under the two header bytes.
    assert_true(cmd.length + 1 <= DCC_PACKET_FIRST_BYTE + 1);
    assert_true(cmd.length <= DCC_MAX_DATA_BYTES);
}

static void test_highest_legal_long_address(void **state)
{
    (void)state;
    // 10239 is the highest address the DCC long-address scheme defines, and
    // 0xE7 is the top of the valid long-address prefix range.
    raw_dcc_cmd_t cmd = throttle_for("t 10239 0 1");

    assert_int_equal(cmd.length, 4);
    assert_int_equal(cmd.data[0], 0xE7);
    assert_int_equal(cmd.data[1], 0xFF);
    assert_int_equal(cmd.data[2], 0x3F);
    assert_int_equal(cmd.data[3], 0x80);
}

// #12: cab 0 is the DCC broadcast address. It used to be accepted and encoded
// as an address-0 throttle packet -- a broadcast to every decoder on the
// layout. It is now refused outright: nothing is emitted, no loco is created.
static void test_cab_zero_emits_nothing(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 0 126 1");
    PicoDccExPacket packet(buffer);

    assert_false(packet.isValid());

    raw_dcc_cmd_t cmd = throttle_for("t 0 126 1");
    assert_int_equal(cmd.length, 0);
}

// #16: an address above the 14-bit long-address space used to mask down to
// 0xFF -- the idle packet address -- rather than being rejected. It is now
// refused outright: nothing is emitted, no loco is created.
static void test_address_above_14_bits_emits_nothing(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_for("t 65535 126 1");

    assert_int_equal(cmd.length, 0);
}

// ---------------------------------------------------------------------------
// Function commands
//
// D5: <F cab func state> must not write the function number into the loco's
// speed. Function support itself is still a stub; this just makes it inert
// rather than dangerous.
// ---------------------------------------------------------------------------

static void test_function_command_does_not_move_a_loco(void **state)
{
    (void)state;
    raw_dcc_cmd_t cmd = throttle_update_for("t 3 0 1", "F 3 8 1");

    assert_int_equal(cmd.data[2], 0x80);  // unchanged
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
    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 139 0>");
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
    assert_string_equal(p_full.getDccExCabUpdate(), "<l 3 0 255 0>");
}

static void test_cab_update_speed_one_reports_one_not_estop(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 3 1 1");
    PicoDccExPacket packet(buffer);

    // Wire speed 1 is step 1, which is DCC speed byte 2: 0x80 | 2 = 130.
    //
    // This test now asserts what its name says. It previously pinned 129, which
    // in the forward direction *is* emergency stop -- so the slowest possible
    // speed was reported to the host as an estop, colliding with the value the
    // test below asserts for a genuine one. Both came from an undocumented
    // subtract-one in getDccExCabUpdate().
    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 130 0>");

    // The two must not be the same value.
    char estop[64];
    copy_command(estop, sizeof(estop), "t 3 -1 1");
    PicoDccExPacket estop_packet(estop);
    assert_string_not_equal(packet.getDccExCabUpdate(), estop_packet.getDccExCabUpdate());
}

// The full <l> speed-byte mapping, against the published DCC-EX format:
// bit 7 is direction, and the low 7 bits are 0 = stop, 1 = emergency stop,
// 2..127 = speed steps 1..126. So wire speed N maps to N + 1.
//
// This whole table was two steps low before, because getDccExCabUpdate()
// subtracted one instead of adding one.
static void test_cab_update_speed_byte_matches_the_published_format(void **state)
{
    (void)state;
    struct { const char *cmd; const char *expected; } cases[] = {
        { "t 3 0 1",   "<l 3 0 128 0>" },  // stop, forward
        { "t 3 0 0",   "<l 3 0 0 0>"   },  // stop, reverse
        { "t 3 -1 1",  "<l 3 0 129 0>" },  // emergency stop, forward
        { "t 3 -1 0",  "<l 3 0 1 0>"   },  // emergency stop, reverse
        { "t 3 1 1",   "<l 3 0 130 0>" },  // slowest step, forward
        { "t 3 1 0",   "<l 3 0 2 0>"   },  // slowest step, reverse
        { "t 3 126 1", "<l 3 0 255 0>" },  // full speed, forward
        { "t 3 126 0", "<l 3 0 127 0>" },  // full speed, reverse
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char buffer[64];
        copy_command(buffer, sizeof(buffer), cases[i].cmd);
        PicoDccExPacket packet(buffer);
        assert_string_equal(packet.getDccExCabUpdate(), cases[i].expected);
    }
}

// The deprecated 4-field form <t REGISTER CAB SPEED DIR> must be rejected, not
// misparsed. sscanf stops when its format is exhausted, so without an explicit
// end-of-command check the extra field is silently ignored and every field
// shifts one place left: <t 1 3 50 1> became cab 1, speed 3, direction 50 --
// commanding a locomotive nobody addressed (#7).
static void test_deprecated_four_field_throttle_is_not_misparsed(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 1 3 50 1");
    PicoDccExPacket packet(buffer);

    assert_false(packet.isValid());
    assert_int_equal(packet.getCab(), -1);

    // The 3-field form of the same intent is still accepted.
    char ok[64];
    copy_command(ok, sizeof(ok), "t 3 50 1");
    PicoDccExPacket good(ok);
    assert_true(good.isValid());
    assert_int_equal(good.getCab(), 3);

    // Trailing whitespace is not "extra fields" and must still parse.
    char spaced[64];
    copy_command(spaced, sizeof(spaced), "t 3 50 1  ");
    PicoDccExPacket padded(spaced);
    assert_true(padded.isValid());
    assert_int_equal(padded.getCab(), 3);

    // <F> shares the parse and gets the same treatment.
    char func[64];
    copy_command(func, sizeof(func), "F 1 3 8 1");
    PicoDccExPacket fn(func);
    assert_false(fn.isValid());
}

static void test_estop_response_reports_estop_not_full_speed(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 3 -1 1");
    PicoDccExPacket packet(buffer);

    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 129 0>");
}

static void test_cab_update_is_not_truncated_for_long_addresses(void **state)
{
    (void)state;
    char buffer[64];
    copy_command(buffer, sizeof(buffer), "t 10239 126 1");
    PicoDccExPacket packet(buffer);

    assert_string_equal(packet.getDccExCabUpdate(), "<l 10239 0 255 0>");
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
//
// #11, #12 and #16 used to live here too (speed -1, cab 0, and addresses above
// the 14-bit long-address space). They are fixed now, so their tests moved up
// into the correct-behaviour sections above. Only the accessory encoding
// issues remain unaddressed.
// ---------------------------------------------------------------------------

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
        cmocka_unit_test_setup(test_adjacent_speeds_are_distinct, setup),
        cmocka_unit_test_setup(test_every_wire_speed_maps_to_a_distinct_byte, setup),
        cmocka_unit_test_setup(test_direction_bit_is_0x80, setup),
        cmocka_unit_test_setup(test_speed_minus_one_is_an_emergency_stop, setup),
        cmocka_unit_test_setup(test_controlled_stop_differs_from_emergency_stop, setup),
        cmocka_unit_test_setup(test_28_step_speed_zero_encodes_as_a_stop, setup),
        cmocka_unit_test_setup(test_28_step_speed_zero_reverse, setup),
        cmocka_unit_test_setup(test_28_step_speed_mid_range, setup),
        cmocka_unit_test_setup(test_28_step_speed_max, setup),
        cmocka_unit_test_setup(test_28_step_low_speeds_quantise_together, setup),
        cmocka_unit_test_setup(test_28_step_direction_bit_is_0x20, setup),
        cmocka_unit_test_setup(test_28_step_speed_minus_one_is_an_emergency_stop, setup),
        cmocka_unit_test_setup(test_the_two_encodings_differ_for_the_same_speed, setup),
        cmocka_unit_test_setup(test_short_address_is_one_byte, setup),
        cmocka_unit_test_setup(test_long_address_is_two_bytes, setup),
        cmocka_unit_test_setup(test_highest_legal_long_address, setup),
        cmocka_unit_test_setup(test_cab_zero_emits_nothing, setup),
        cmocka_unit_test_setup(test_address_above_14_bits_emits_nothing, setup),
        cmocka_unit_test_setup(test_function_command_does_not_move_a_loco, setup),
        cmocka_unit_test_setup(test_explicit_command_repeats_three_times, setup),
        cmocka_unit_test_setup(test_reminder_does_not_repeat, setup),
        cmocka_unit_test_setup(test_reminders_rotate_between_locos, setup),
        cmocka_unit_test_setup(test_accessory_low_address, setup),
        cmocka_unit_test_setup(test_cab_update_response_format, setup),
        cmocka_unit_test_setup(test_cab_update_speed_extremes, setup),
        cmocka_unit_test_setup(test_cab_update_speed_one_reports_one_not_estop, setup),
        cmocka_unit_test_setup(test_cab_update_speed_byte_matches_the_published_format, setup),
        cmocka_unit_test_setup(test_deprecated_four_field_throttle_is_not_misparsed, setup),
        cmocka_unit_test_setup(test_estop_response_reports_estop_not_full_speed, setup),
        cmocka_unit_test_setup(test_cab_update_is_not_truncated_for_long_addresses, setup),
        cmocka_unit_test_setup(test_power_update_response_format, setup),
        cmocka_unit_test_setup(test_ISSUE_15_accessory_high_bits_not_ones_complemented, setup),
        cmocka_unit_test_setup(test_ISSUE_15_accessory_activate_bit_is_stuck_on, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
