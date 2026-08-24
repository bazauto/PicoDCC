#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCLoco/pico_dccloco.h"

// Unless a test says otherwise, a PicoDccLoco is built in the station default
// speed step mode, which is 128 steps (#8). That is a three-byte payload for a
// short address -- address, 0x3F, then (direction << 7) | value -- so the speed
// byte is data[1] + 1 places along from where the 28-step form put it. The
// 28-step fallback and the exact bytes of both encodings are pinned in
// pico_dcc_wire_format_tests.cpp; this suite is about address handling,
// validation and state, and reads the speed byte only to prove those.

void test_create_from_packet(void **state)
{
  const char *buffer = "t 3 0 0";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x3F);
  assert_int_equal(cmd.data[2], 0x00);  // controlled stop, reverse (#48)
}
void test_create_from_address(void **state)
{
  PicoDccLoco loco(3);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x3F);
  assert_int_equal(cmd.data[2], 0x80);  // controlled stop, forward (#48)
}

// Was PicoDccLoco(3, 128, false) asserting data[1] == 81 -- a nonsense speed
// (128 is outside the 0..126 throttle range) that happened to mask to 0 under
// the old unchecked `speed & 0x7f`. Now that 128 is caught by the constructor
// and clamped to a safe stop, this exercises a real, in-range speed instead.
void test_create_from_address_speed_direction(void **state)
{
  PicoDccLoco loco(3, 126, false);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x3F);
  assert_int_equal(cmd.data[2], 127);  // top speed, reverse
}

void test_create_from_address_rejects_speed_above_max(void **state)
{
  PicoDccLoco loco(3, 200, true);

  assert_true(loco.isValid());

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 0x03);
  assert_int_equal(cmd.data[2], 0x80);
}

// A non-throttle, non-function packet used to reach std::terminate here and
// abort the firmware (#2). It now yields an inert loco: invalid, zero-length
// command, no crash.
void test_non_throttle_packet_yields_inert_loco(void **state)
{
  const char *buffer = "s";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_false(loco.isValid());
  assert_int_equal(loco.getThrottleCommand().length, 0);
}

// Cab 0 is the DCC broadcast address (#12): it must be refused, not silently
// retargeted to a real locomotive.
void test_cab_zero_yields_inert_loco(void **state)
{
  const char *buffer = "t 0 10 1";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_false(loco.isValid());
  assert_int_equal(loco.getThrottleCommand().length, 0);
}

// Anything above the 14-bit long-address space must be refused, not encoded
// into an idle/reserved packet (#16).
void test_cab_above_14_bits_yields_inert_loco(void **state)
{
  {
    const char *buffer = "t 65535 10 1";
    PicoDccExPacket packet((char *)buffer);
    PicoDccLoco loco(&packet);
    assert_false(loco.isValid());
    assert_int_equal(loco.getThrottleCommand().length, 0);
  }
  {
    const char *buffer = "t 10240 10 1";
    PicoDccExPacket packet((char *)buffer);
    PicoDccLoco loco(&packet);
    assert_false(loco.isValid());
    assert_int_equal(loco.getThrottleCommand().length, 0);
  }
}

void test_highest_legal_cab_is_encoded(void **state)
{
  const char *buffer = "t 10239 0 1";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_true(loco.isValid());

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 4);
  assert_int_equal(cmd.data[0], 0xE7);
  assert_int_equal(cmd.data[1], 0xFF);
  assert_int_equal(cmd.data[2], 0x3F);
  assert_int_equal(cmd.data[3], 0x80);
}

// An out-of-range speed on an otherwise-valid address fails safe to a stop
// rather than rejecting the whole loco.
void test_out_of_range_speed_falls_back_to_stop(void **state)
{
  const char *buffer = "t 3 200 1";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_true(loco.isValid());

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 0x03);
  assert_int_equal(cmd.data[2], 0x80);
}

// #11: speed -1 is DCC-EX's single-locomotive emergency stop, not a stray
// value that decodes into full speed. In the 128-step instruction that is
// value 1, addressed to one loco, with the direction bit preserved.
void test_estop_packet_encodes_emergency_stop(void **state)
{
  {
    const char *buffer = "t 3 -1 1";
    PicoDccExPacket packet((char *)buffer);
    PicoDccLoco loco(&packet);

    raw_dcc_cmd_t cmd = loco.getThrottleCommand();
    assert_int_equal(cmd.length, 3);
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[1], 0x3F);
    assert_int_equal(cmd.data[2], 0x81);
    assert_int_equal(cmd.repeats, 3);
  }
  {
    const char *buffer = "t 3 -1 0";
    PicoDccExPacket packet((char *)buffer);
    PicoDccLoco loco(&packet);

    raw_dcc_cmd_t cmd = loco.getThrottleCommand();
    assert_int_equal(cmd.data[0], 0x03);
    assert_int_equal(cmd.data[2], 0x01);
  }
}

// This is what Core 1's reminders actually read: the estop must not be a
// one-shot side effect of the update, it has to persist in the stored command.
void test_estop_survives_repeated_reads(void **state)
{
  const char *buffer = "t 3 -1 1";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  raw_dcc_cmd_t first = loco.getThrottleCommand();
  raw_dcc_cmd_t second = loco.getThrottleCommand();

  assert_int_equal(first.data[2], 0x81);
  assert_int_equal(second.data[2], 0x81);
}

void test_estop_long_address(void **state)
{
  PicoDccLoco loco(1000);
  loco.updateControl(true, DCC_SPEED_ESTOP);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 4);
  assert_int_equal(cmd.data[0], 0xC3);
  assert_int_equal(cmd.data[1], 0xE8);
  assert_int_equal(cmd.data[2], 0x3F);
  assert_int_equal(cmd.data[3], 0x81);
}

// <F cab func state> must not write the function number into speed and the
// function state into direction -- pressing F8 used to command speed 8 (D5).
void test_function_packet_creates_loco_at_stop(void **state)
{
  const char *buffer = "F 3 8 0";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 0x03);
  assert_int_equal(cmd.data[2], 0x80);  // not speed 8, not reverse
}

void test_function_packet_does_not_change_speed(void **state)
{
  PicoDccLoco loco(3);
  loco.updateControl(true, 20);
  raw_dcc_cmd_t before = loco.getThrottleCommand();
  assert_int_equal(before.data[2], 0x80 | 21);

  const char *buffer = "F 3 8 1";
  PicoDccExPacket packet((char *)buffer);
  bool updated = loco.update(&packet);

  assert_false(updated);
  raw_dcc_cmd_t after = loco.getThrottleCommand();
  assert_int_equal(after.data[2], 0x80 | 21);
}

void test_update_control_rejects_out_of_range_speed(void **state)
{
  PicoDccLoco loco(3, 20, true);
  raw_dcc_cmd_t before = loco.getThrottleCommand();

  bool updated = loco.updateControl(true, 200);

  assert_false(updated);
  raw_dcc_cmd_t after = loco.getThrottleCommand();
  assert_int_equal(after.data[2], before.data[2]);
}

void test_update_control_accepts_estop_sentinel(void **state)
{
  PicoDccLoco loco(3, 20, true);

  bool updated = loco.updateControl(true, DCC_SPEED_ESTOP);

  assert_true(updated);
  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.data[2], 0x81);
}

void test_out_of_range_address_emits_nothing(void **state)
{
  PicoDccLoco loco(20000);

  assert_false(loco.isValid());
  assert_int_equal(loco.getThrottleCommand().length, 0);
}

void test_update_control(void **state)
{
  PicoDccLoco loco(3, 128, true);
  bool updated = loco.updateControl(false, 64);

  assert_true(updated);
  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x3F);
  assert_int_equal(cmd.data[2], 65);  // reverse, wire speed 64 -> value 65
}

// PicoDccLoco::updateFunct() has an empty body and getFunctionCommand()
// returns a default-constructed command: function support is declared but not
// implemented (see the "in the tree but does not work" list in CLAUDE.md).
//
// These two tests previously asserted nothing at all, which read as coverage
// for a feature that does not exist. They now pin the unimplemented state
// down, so that when function support lands they fail and have to be
// replaced with real assertions rather than being quietly left behind.
void test_update_function(void **state)
{
  PicoDccLoco loco(3);
  raw_dcc_cmd_t before = loco.getThrottleCommand();

  loco.updateFunct(1, true);

  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.length, 0);  // no function packet is produced

  // updateFunct() must not disturb the throttle command while it is a stub.
  raw_dcc_cmd_t after = loco.getThrottleCommand();
  assert_int_equal(after.length, before.length);
  assert_int_equal(after.data[0], before.data[0]);
  assert_int_equal(after.data[1], before.data[1]);
}

// Emergency stop test removed - emergency stop is now handled as broadcast command in controller

void test_function_command(void **state)
{
  PicoDccLoco loco(3);
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);

  // Unimplemented: a zero-length command, which PicoDccController treats as
  // "nothing to queue". See the note above test_update_function.
  assert_int_equal(cmd.length, 0);
  assert_int_equal(cmd.repeats, 0);
}

// --------------------------------------------------------------------------
// Speed step mode (#8)
// --------------------------------------------------------------------------

// A loco follows the station default until it is named, and 128 steps is that
// default. Both facts matter to the orchestrator: it may push a roster of
// overrides, or push none at all and rely on this.
void test_default_speed_step_mode_is_128(void **state)
{
  PicoDccLoco loco(3);

  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_128);
  assert_false(loco.hasSpeedStepsOverride());
}

// Naming a loco re-encodes its stored command in place. Core 1 reads that
// command for reminders, so a mode change that did not regenerate it would
// leave the old encoding on the rails until the next throttle command.
void test_setting_28_steps_re_encodes_the_stored_command(void **state)
{
  PicoDccLoco loco(3, 60, true);
  assert_int_equal(loco.getThrottleCommand().length, 3);

  assert_true(loco.setSpeedSteps(DCC_SPEED_STEPS_28));

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_28);
  assert_true(loco.hasSpeedStepsOverride());
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x68);
}

// Neither 28 nor 128: refused outright rather than rounded to whichever is
// nearer. There is no third mode to fall back to.
void test_invalid_speed_step_mode_is_refused(void **state)
{
  PicoDccLoco loco(3, 60, true);

  assert_false(loco.setSpeedSteps(14));
  assert_false(loco.setSpeedSteps(0));
  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_128);
  assert_false(loco.hasSpeedStepsOverride());
}

// A step count is not a speed, and the two sit next to each other in the
// constructor's argument list.
void test_constructor_rejects_a_bogus_step_count(void **state)
{
  PicoDccLoco loco(3, 60, true, 42);

  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_128);
}

// An override survives a station-wide default change: that is the whole point
// of it, since the loco with the old decoder is the reason it was set.
void test_override_is_not_moved_by_the_station_default(void **state)
{
  PicoDccLoco loco(3, 60, true);
  assert_true(loco.setSpeedSteps(DCC_SPEED_STEPS_28));

  assert_false(loco.applyDefaultSpeedSteps(DCC_SPEED_STEPS_128));
  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_28);
}

// Re-asserting the mode a loco already has is not an error, and does not clear
// the override -- the orchestrator re-sends the whole roster on every boot
// banner, so this is the common case, not the odd one.
void test_re_asserting_the_same_mode_still_marks_it_overridden(void **state)
{
  PicoDccLoco loco(3, 60, true);

  assert_false(loco.setSpeedSteps(DCC_SPEED_STEPS_128));  // nothing changed
  assert_true(loco.hasSpeedStepsOverride());              // but it is named
  assert_false(loco.applyDefaultSpeedSteps(DCC_SPEED_STEPS_28));
  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_128);
}

// A loco that is still following the default moves with it, and the stored
// command is re-encoded.
void test_default_change_moves_an_unnamed_loco(void **state)
{
  PicoDccLoco loco(3, 60, true);

  assert_true(loco.applyDefaultSpeedSteps(DCC_SPEED_STEPS_28));
  assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_28);
  assert_int_equal(loco.getThrottleCommand().data[1], 0x68);
}

// PicoDccLocos stores locos by value, so every vector insertion and every
// reallocation runs the copy constructor -- which lists its members by hand.
void test_copy_constructor_carries_the_mode(void **state)
{
  PicoDccLoco loco(3, 60, true);
  assert_true(loco.setSpeedSteps(DCC_SPEED_STEPS_28));

  PicoDccLoco copy(loco);

  assert_int_equal(copy.getSpeedSteps(), DCC_SPEED_STEPS_28);
  assert_true(copy.hasSpeedStepsOverride());
  assert_int_equal(copy.getThrottleCommand().length, 2);
  assert_int_equal(copy.getThrottleCommand().data[1], 0x68);
}

int main(int argc, char *argv[])
{
  printf("Running Tests\n");

  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_create_from_packet),
      cmocka_unit_test(test_non_throttle_packet_yields_inert_loco),
      cmocka_unit_test(test_cab_zero_yields_inert_loco),
      cmocka_unit_test(test_cab_above_14_bits_yields_inert_loco),
      cmocka_unit_test(test_highest_legal_cab_is_encoded),
      cmocka_unit_test(test_out_of_range_speed_falls_back_to_stop),
      cmocka_unit_test(test_estop_packet_encodes_emergency_stop),
      cmocka_unit_test(test_estop_survives_repeated_reads),
      cmocka_unit_test(test_estop_long_address),
      cmocka_unit_test(test_function_packet_creates_loco_at_stop),
      cmocka_unit_test(test_function_packet_does_not_change_speed),
      cmocka_unit_test(test_update_control_rejects_out_of_range_speed),
      cmocka_unit_test(test_update_control_accepts_estop_sentinel),
      cmocka_unit_test(test_out_of_range_address_emits_nothing),
      cmocka_unit_test(test_create_from_address),
      cmocka_unit_test(test_create_from_address_speed_direction),
      cmocka_unit_test(test_create_from_address_rejects_speed_above_max),
      cmocka_unit_test(test_update_control),
      cmocka_unit_test(test_update_function),
      cmocka_unit_test(test_function_command),
      cmocka_unit_test(test_default_speed_step_mode_is_128),
      cmocka_unit_test(test_setting_28_steps_re_encodes_the_stored_command),
      cmocka_unit_test(test_invalid_speed_step_mode_is_refused),
      cmocka_unit_test(test_constructor_rejects_a_bogus_step_count),
      cmocka_unit_test(test_override_is_not_moved_by_the_station_default),
      cmocka_unit_test(test_re_asserting_the_same_mode_still_marks_it_overridden),
      cmocka_unit_test(test_default_change_moves_an_unnamed_loco),
      cmocka_unit_test(test_copy_constructor_carries_the_mode)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
