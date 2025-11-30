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

void test_create_from_function_command(void **state)
{
  // Test creating a loco from a function command
  const char *buffer = "F 3 5 1";  // Function 5 ON
  PicoDccExPacket packet((char *)buffer);
  PicoDccLoco loco(&packet);

  assert_int_equal(loco.getAddress(), 3);
  
  // Function command should generate F5-F8 group command with F5 on
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(2);  // Group 2 = F5-F8
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);      // Address
  assert_int_equal(cmd.data[1], 0xB1);   // 0xB0 | 0x01 (F5 bit set)
}

void test_update_function(void **state)
{
  PicoDccLoco loco(3);
  
  // Update F1 to ON
  loco.updateFunct(1, true);
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);      // Address
  assert_int_equal(cmd.data[1], 0x81);   // 0x80 | 0x01 (F1 bit set)
  
  // Update F0 to ON (headlight)
  loco.updateFunct(0, true);
  cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.data[1], 0x91);   // 0x80 | 0x10 | 0x01 (F0 and F1 set)
}

void test_update_via_packet(void **state)
{
  // Create loco with throttle command
  const char *throttle_buffer = "t 3 50 1";
  PicoDccExPacket throttle_packet((char *)throttle_buffer);
  PicoDccLoco loco(&throttle_packet);
  
  // Update with function command
  const char *function_buffer = "F 3 12 1";  // F12 ON
  PicoDccExPacket function_packet((char *)function_buffer);
  bool updated = loco.update(&function_packet);
  
  assert_true(updated);
  
  // Check F12 is set in group 3 (F9-F12)
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(3);
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0xA8);   // 0xA0 | 0x08 (F12 bit set)
}

void test_function_group_1_f0_f4(void **state)
{
  PicoDccLoco loco(3);
  
  // Set F0, F2, F4
  loco.updateFunct(0, true);  // F0 (headlight)
  loco.updateFunct(2, true);  // F2
  loco.updateFunct(4, true);  // F4
  
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x9A);   // 0x80 | 0x10 | 0x02 | 0x08
}

void test_function_group_2_f5_f8(void **state)
{
  PicoDccLoco loco(3);
  
  // Set F5, F7
  loco.updateFunct(5, true);
  loco.updateFunct(7, true);
  
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(2);
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0xB5);   // 0xB0 | 0x01 | 0x04
}

void test_function_group_3_f9_f12(void **state)
{
  PicoDccLoco loco(3);
  
  // Set F9, F11
  loco.updateFunct(9, true);
  loco.updateFunct(11, true);
  
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(3);
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0xA5);   // 0xA0 | 0x01 | 0x04
}

void test_function_group_4_f13_f20(void **state)
{
  PicoDccLoco loco(3);
  
  // Set F13, F15, F20
  loco.updateFunct(13, true);
  loco.updateFunct(15, true);
  loco.updateFunct(20, true);
  
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(4);
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0xDE);   // Instruction byte for F13-F20
  assert_int_equal(cmd.data[2], 0x85);   // 0x01 | 0x04 | 0x80
}

void test_function_group_5_f21_f28(void **state)
{
  PicoDccLoco loco(3);
  
  // Set F21, F25, F28
  loco.updateFunct(21, true);
  loco.updateFunct(25, true);
  loco.updateFunct(28, true);
  
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(5);
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0xDF);   // Instruction byte for F21-F28
  assert_int_equal(cmd.data[2], 0x91);   // 0x01 | 0x10 | 0x80
}

void test_function_long_address(void **state)
{
  // Test with long address (>127)
  PicoDccLoco loco(1234);
  
  // Set F8
  loco.updateFunct(8, true);
  
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(2);
  assert_int_equal(cmd.length, 3);
  assert_int_equal(cmd.data[0], 0xC4);   // 0xC0 | (1234 >> 8)
  assert_int_equal(cmd.data[1], 0xD2);   // 1234 & 0xFF
  assert_int_equal(cmd.data[2], 0xB8);   // 0xB0 | 0x08 (F8 set)
}

void test_function_invalid_number(void **state)
{
  PicoDccLoco loco(3);
  
  // Try to set function 29 (invalid, only 0-28 supported)
  loco.updateFunct(29, true);
  
  // Should not crash, function is ignored
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.data[1], 0x80);   // No functions set
}

void test_function_toggle_on_off(void **state)
{
  PicoDccLoco loco(3);
  
  // Turn F3 on
  loco.updateFunct(3, true);
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.data[1], 0x84);   // 0x80 | 0x04
  
  // Turn F3 off
  loco.updateFunct(3, false);
  cmd = loco.getFunctionCommand(1);
  assert_int_equal(cmd.data[1], 0x80);   // Just base instruction
}

// Emergency stop test removed - emergency stop is now handled as broadcast command in controller

void test_function_command(void **state)
{
  PicoDccLoco loco(3);
  raw_dcc_cmd_t cmd = loco.getFunctionCommand(1);

  // Default state - all functions off
  assert_int_equal(cmd.length, 2);
  assert_int_equal(cmd.data[0], 3);
  assert_int_equal(cmd.data[1], 0x80);  // Group 1 base instruction
}

void test_dccex_status_response(void **state)
{
  // Test that getDccExStatus includes function states
  PicoDccLoco loco(3, 50, true);  // Address 3, speed 50, forward
  
  // Turn on some functions
  loco.updateFunct(0, true);   // F0
  loco.updateFunct(13, true);  // F13
  loco.updateFunct(28, true);  // F28
  
  const char* status = loco.getDccExStatus();
  
  // Should be: <l 3 0 177 1000000000000100000000000000001>
  // Speed 50-1 = 49, with forward bit (128) = 177
  // Function bits: F0=1 (pos 0), F13=1 (pos 13), F28=1 (pos 28)
  assert_string_equal(status, "<l 3 0 177 10000000000001000000000000001>");
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
      cmocka_unit_test(test_create_from_function_command),
      cmocka_unit_test(test_update_function),
      cmocka_unit_test(test_update_via_packet),
      cmocka_unit_test(test_function_group_1_f0_f4),
      cmocka_unit_test(test_function_group_2_f5_f8),
      cmocka_unit_test(test_function_group_3_f9_f12),
      cmocka_unit_test(test_function_group_4_f13_f20),
      cmocka_unit_test(test_function_group_5_f21_f28),
      cmocka_unit_test(test_function_long_address),
      cmocka_unit_test(test_function_invalid_number),
      cmocka_unit_test(test_function_toggle_on_off),
      cmocka_unit_test(test_function_command),
      cmocka_unit_test(test_dccex_status_response)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}