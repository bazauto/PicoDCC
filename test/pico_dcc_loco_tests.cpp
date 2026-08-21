#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdexcept>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCLoco/pico_dccloco.h"

void test_create_from_packet(void **state)
{
  const char *buffer = "t 3 0 0";
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 81);
}
void test_create_from_address(void **state)
{
  PicoDccLoco loco(3);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 113);
}
void test_create_from_address_speed_direction(void **state)
{
  PicoDccLoco loco(3, 128, false);

  assert_int_equal(loco.getAddress(), 3);

  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 81);
}

void test_invalid_packet_opcode(void **state)
{
  const char *buffer = "s";
  PicoDccExPacket packet((char *)buffer);
  try {
    PicoDccLoco loco(&packet);
  } catch (const std::invalid_argument& e) {
    assert_string_equal(e.what(), "Only throttle or function commands can be used to create a loco.");
  }
}
void test_invalid_packet_lowaddr(void **state)
{
  const char *buffer = "t -1 0 0";
  PicoDccExPacket packet((char *)buffer);
  try {
    PicoDccLoco loco(&packet);
  } catch (const std::invalid_argument& e) {
    assert_string_equal(e.what(), "Loco address outside allowed range.");
  }
}
void test_invalid_packet_highadd(void **state)
{
  const char *buffer = "t 65536 0 0";
  PicoDccExPacket packet((char *)buffer);
  try {
    PicoDccLoco loco(&packet);
  } catch (const std::invalid_argument& e) {
    assert_string_equal(e.what(), "Loco address outside allowed range.");
  }
}
void test_invalid_packet_lowspeed(void **state)
{
  const char *buffer = "t 3 -1 0";
  PicoDccExPacket packet((char *)buffer);
  try {
    PicoDccLoco loco(&packet);
  } catch (const std::invalid_argument& e) {
    assert_string_equal(e.what(), "Loco speed outside allowed range.");
  }
}
void test_invalid_packet_highspeed(void **state)
{
  const char *buffer = "t 3 256 0";
  PicoDccExPacket packet((char *)buffer);
  try {
    PicoDccLoco loco(&packet);
  } catch (const std::invalid_argument& e) {
    assert_string_equal(e.what(), "Loco speed outside allowed range.");
  }
}

void test_update_control(void **state)
{
  PicoDccLoco loco(3, 128, true);
  bool updated = loco.updateControl(false, 64);

  assert_true(updated);
  raw_dcc_cmd_t cmd = loco.getThrottleCommand();
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 88);
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

int main(int argc, char *argv[])
{
  printf("Running Tests\n");

  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_create_from_packet),
      cmocka_unit_test(test_invalid_packet_opcode),
      cmocka_unit_test(test_invalid_packet_lowaddr),
      cmocka_unit_test(test_invalid_packet_highadd),
      cmocka_unit_test(test_invalid_packet_lowspeed),
      cmocka_unit_test(test_invalid_packet_highspeed),
      cmocka_unit_test(test_create_from_address),
      cmocka_unit_test(test_create_from_address_speed_direction),
      cmocka_unit_test(test_update_control),
      cmocka_unit_test(test_update_function),
      cmocka_unit_test(test_function_command)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}