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

void test_update_function(void **state)
{
  PicoDccLoco loco(3);
  loco.updateFunct(1, true);

  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);
  // Add assertions based on expected function command
}

// Emergency stop test removed - emergency stop is now handled as broadcast command in controller

void test_function_command(void **state)
{
  PicoDccLoco loco(3);
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);

  // Add assertions based on expected function command for group 1
}

int main(int argc, char *argv[])
{
  printf("Running Tests\n");

  void *state;

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