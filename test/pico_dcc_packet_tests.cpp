#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

// Workaround for MSVC inline macro issue
#ifdef _MSC_VER
#pragma push_macro("inline")
#undef inline
#endif

extern "C" {
#include <cmocka.h>
}

#ifdef _MSC_VER
#pragma pop_macro("inline")
#endif

#include "../lib/PicoDCCEX/pico_dccexpacket.h"

void test_invalid_packet(void **state)
{
  char buffer[10] = "x 123";
  PicoDccExPacket packet(buffer);
  assert_false(packet.isValid());
}

void test_ver_packet(void **state)
{
  char buffer[10] = "s";
  PicoDccExPacket packet(buffer);
  assert_true(packet.isValid());

  assert_int_equal((int)packet.getOpcode(), (int)'s');
}
void test_num_cabs_packet(void **state)
{
  char buffer[10] = "#";
  PicoDccExPacket packet(buffer);
  assert_true(packet.isValid());

  assert_int_equal((int)packet.getOpcode(), (int)'#');
}

void test_power_off_packet(void **state)
{
  char buffer[10] = "0";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_true(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'0');

  pico_dccex_track_select track = packet.getTrack();
  assert_int_equal((int)track, (int)DCCEX_TRACK_ALL);

  assert_false(packet.getPowerOn());

  const char *cmd = packet.getDccExPowerUpdate();
  assert_string_equal(cmd, "<p0>");
}
void test_power_on_packet(void **state)
{
  char buffer[10] = "1";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_true(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'1');

  pico_dccex_track_select track = packet.getTrack();
  assert_int_equal((int)track, (int)DCCEX_TRACK_ALL);

  assert_true(packet.getPowerOn());

  const char *cmd = packet.getDccExPowerUpdate();
  assert_string_equal(cmd, "<p1>");
}
void test_power_off_main_packet(void **state)
{
  char buffer[10] = "0 MAIN";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_true(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'0');

  pico_dccex_track_select track = packet.getTrack();
  assert_int_equal((int)track, (int)DCCEX_TRACK_MAIN);

  assert_false(packet.getPowerOn());

  const char *cmd = packet.getDccExPowerUpdate();
  assert_string_equal(cmd, "<p0 MAIN>");
}
void test_power_on_main_packet(void **state)
{
  char buffer[10] = "1 MAIN";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_true(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'1');

  pico_dccex_track_select track = packet.getTrack();
  assert_int_equal((int)track, (int)DCCEX_TRACK_MAIN);

  assert_true(packet.getPowerOn());

  const char *cmd = packet.getDccExPowerUpdate();
  assert_string_equal(cmd, "<p1 MAIN>");
}
void test_power_off_prog_packet(void **state)
{
  char buffer[10] = "0 PROG";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_true(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'0');

  pico_dccex_track_select track = packet.getTrack();
  assert_int_equal((int)track, (int)DCCEX_TRACK_PROG);

  assert_false(packet.getPowerOn());

  const char *cmd = packet.getDccExPowerUpdate();
  assert_string_equal(cmd, "<p0 PROG>");
}
void test_power_on_prog_packet(void **state)
{
  char buffer[10] = "1 PROG";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_true(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'1');

  pico_dccex_track_select track = packet.getTrack();
  assert_int_equal((int)track, (int)DCCEX_TRACK_PROG);

  assert_true(packet.getPowerOn());

  const char *cmd = packet.getDccExPowerUpdate();
  assert_string_equal(cmd, "<p1 PROG>");
}

void test_acc_1_packet(void **state)
{
  char buffer[10] = "a 101 3 1";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_false(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_true(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'a');
  assert_int_equal(packet.getAccessoryAddr(), 101);
  assert_int_equal(packet.getAccessorySubAddr(), 3);
  assert_int_equal(packet.getAccessoryActivate(), 1);
  
  raw_dcc_cmd_t *cmd = packet.getRawDccAccessoryCmd();
  assert_int_equal(cmd->length, 2);
  assert_int_equal(cmd->data[0], 165);  // 0101 0101
  assert_int_equal(cmd->data[1], 159);  // 1001 1111
  assert_int_equal(cmd->repeats, 3);
}
void test_acc_2_packet(void **state)
{
  char buffer[10] = "a 101 1";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_false(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_true(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'a');
  assert_int_equal(packet.getAccessoryAddr(), 101);
  assert_int_equal(packet.getAccessorySubAddr(), 0);
  assert_int_equal(packet.getAccessoryActivate(), 1);
  
  raw_dcc_cmd_t *cmd = packet.getRawDccAccessoryCmd();
  assert_int_equal(cmd->length, 2);
  assert_int_equal(cmd->data[0], 165);  // 0101 0101
  assert_int_equal(cmd->data[1], 153);  // 1001 1001
  assert_int_equal(cmd->repeats, 3);
}

void test_throttle_packet(void **state)
{
  char buffer[11] = "t 101 25 0";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_false(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_true(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'t');
  assert_int_equal(packet.getCab(), 101);
  assert_int_equal(packet.getSpeed(), 25);
  assert_int_equal(packet.getDirection(), 0);
  
  const char *cmd = packet.getDccExCabUpdate();
  assert_string_equal(cmd, "<l 101 0 26 0>");
}
void test_function_packet(void **state)
{
  char buffer[11] = "F 101 25 0";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_false(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_true(packet.isFunctionCommand());
  assert_false(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'F');
  assert_int_equal(packet.getCab(), 101);
  assert_int_equal(packet.getSpeed(), 25);
  assert_int_equal(packet.getDirection(), 0);
  
  const char *cmd = packet.getDccExCabUpdate();
  assert_string_equal(cmd, "<l 101 0 26 0>");
}
void test_estop_packet(void **state)
{
  char buffer[10] = "!";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_false(packet.isPowerCommand());
  assert_false(packet.isVersionCommand());
  assert_false(packet.isNumCabsCommand());
  assert_false(packet.isThrottleCommand());
  assert_false(packet.isFunctionCommand());
  assert_true(packet.isEmergencyStopCommand());
  assert_false(packet.isAccesoryCommand());
  
  assert_int_equal((int)packet.getOpcode(), (int)'!');
  assert_int_equal(packet.getCab(), 0);
  assert_int_equal(packet.getSpeed(), 0);
  assert_int_equal(packet.getDirection(), 0);
}

// ---------------------------------------------------------------------------
// Throttle/function validation (#11, #12, #16)
// ---------------------------------------------------------------------------

void test_throttle_cab_zero_is_rejected(void **state)
{
  char buffer[16] = "t 0 126 1";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());
}

void test_throttle_cab_above_range_is_rejected(void **state)
{
  {
    char buffer[16] = "t 10240 20 1";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[16] = "t 65535 126 1";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
}

void test_throttle_cab_range_boundaries_accepted(void **state)
{
  {
    char buffer[16] = "t 1 0 1";
    PicoDccExPacket packet(buffer);
    assert_true(packet.isValid());
  }
  {
    char buffer[16] = "t 10239 126 1";
    PicoDccExPacket packet(buffer);
    assert_true(packet.isValid());
  }
}

void test_throttle_speed_above_max_is_rejected(void **state)
{
  const char *bad[] = {"t 3 127 1", "t 3 128 1", "t 3 256 1"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
  {
    char buffer[16];
    size_t j = 0;
    for (; bad[i][j] != '\0' && j < sizeof(buffer) - 1; j++) buffer[j] = bad[i][j];
    buffer[j] = '\0';
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
}

void test_throttle_speed_minus_one_is_accepted(void **state)
{
  char buffer[16] = "t 3 -1 1";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getSpeed(), -1);
}

void test_throttle_speed_below_minus_one_is_rejected(void **state)
{
  char buffer[16] = "t 3 -2 1";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());
}

// Pins the sscanf-failed sentinel path: a short command that cannot supply
// all three throttle fields must not be accepted.
void test_malformed_throttle_is_rejected(void **state)
{
  char buffer[16] = "t 3";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());
}

void test_function_cab_range_is_validated(void **state)
{
  {
    char buffer[16] = "F 0 1 1";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[16] = "F 10240 1 1";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[16] = "F 3 1 1";
    PicoDccExPacket packet(buffer);
    assert_true(packet.isValid());
  }
}

void test_cab_update_reports_estop(void **state)
{
  {
    char buffer[16] = "t 3 -1 1";
    PicoDccExPacket packet(buffer);
    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 129 0>");
  }
  {
    char buffer[16] = "t 3 -1 0";
    PicoDccExPacket packet(buffer);
    assert_string_equal(packet.getDccExCabUpdate(), "<l 3 0 1 0>");
  }
}

// The 16-byte dccex_cab_update buffer used to truncate this to
// "<l 10239 0 253 " with no closing '>' for any 4- or 5-digit address.
void test_cab_update_long_address_not_truncated(void **state)
{
  char buffer[16] = "t 10239 126 1";
  PicoDccExPacket packet(buffer);

  assert_string_equal(packet.getDccExCabUpdate(), "<l 10239 0 255 0>");
}

void test_config_ack_limit_valid(void **state)
{
  char buffer[32] = "D ACK LIMIT 60";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal((int)packet.getOpcode(), (int)'D');
  assert_int_equal(packet.getConfigSubcommand(), 1);  // ACK
  assert_int_equal(packet.getConfigParamType(), 1);   // LIMIT
  assert_int_equal(packet.getConfigValue(), 60);
}

void test_config_ack_limit_too_low(void **state)
{
  char buffer[32] = "D ACK LIMIT 20";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Below minimum of 30mA
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

void test_config_ack_limit_too_high(void **state)
{
  char buffer[32] = "D ACK LIMIT 150";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Above maximum of 100mA
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

void test_config_ack_min_valid(void **state)
{
  char buffer[32] = "D ACK MIN 5000";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal((int)packet.getOpcode(), (int)'D');
  assert_int_equal(packet.getConfigSubcommand(), 1);  // ACK
  assert_int_equal(packet.getConfigParamType(), 2);   // MIN
  assert_int_equal(packet.getConfigValue(), 5000);
}

void test_config_ack_min_too_low(void **state)
{
  char buffer[32] = "D ACK MIN 2000";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Below minimum of 3000µs
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

void test_config_ack_min_too_high(void **state)
{
  char buffer[32] = "D ACK MIN 9000";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Above maximum of 8000µs
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

void test_config_ack_max_valid(void **state)
{
  char buffer[32] = "D ACK MAX 7000";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal((int)packet.getOpcode(), (int)'D');
  assert_int_equal(packet.getConfigSubcommand(), 1);  // ACK
  assert_int_equal(packet.getConfigParamType(), 3);   // MAX
  assert_int_equal(packet.getConfigValue(), 7000);
}

void test_config_ack_max_too_low(void **state)
{
  char buffer[32] = "D ACK MAX 5000";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Below minimum of 6000µs
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

void test_config_ack_max_too_high(void **state)
{
  char buffer[32] = "D ACK MAX 15000";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Above maximum of 10000µs
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

void test_config_save_command(void **state)
{
  char buffer[10] = "E";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal((int)packet.getOpcode(), (int)'E');
}

void test_config_malformed_ack(void **state)
{
  char buffer[32] = "D ACK INVALID 60";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());  // Unknown parameter type
  assert_int_equal((int)packet.getOpcode(), (int)'D');
}

// --------------------------------------------------------------------------
// <D SPEED28|SPEED128 [cab]> -- speed step mode (#8)
//
// The no-argument form is DCC-EX's own, station-wide command; the trailing cab
// is this station's extension. JMRI only ever sends the no-argument form, so
// the DCC-EX form has to keep parsing exactly as it did.
// --------------------------------------------------------------------------

void test_config_speed128_no_cab(void **state)
{
  char buffer[32] = "D SPEED128";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getConfigSubcommand(), DCCEX_CONFIG_SPEED);
  assert_int_equal(packet.getSpeedStepMode(), DCC_SPEED_STEPS_128);
  assert_int_equal(packet.getSpeedStepCab(), 0);  // 0 == station-wide
}

void test_config_speed28_no_cab(void **state)
{
  char buffer[32] = "D SPEED28";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getConfigSubcommand(), DCCEX_CONFIG_SPEED);
  assert_int_equal(packet.getSpeedStepMode(), DCC_SPEED_STEPS_28);
  assert_int_equal(packet.getSpeedStepCab(), 0);
}

void test_config_speed28_with_cab(void **state)
{
  char buffer[32] = "D SPEED28 3";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getConfigSubcommand(), DCCEX_CONFIG_SPEED);
  assert_int_equal(packet.getSpeedStepMode(), DCC_SPEED_STEPS_28);
  assert_int_equal(packet.getSpeedStepCab(), 3);
}

void test_config_speed128_with_long_cab(void **state)
{
  char buffer[32] = "D SPEED128 10239";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getSpeedStepMode(), DCC_SPEED_STEPS_128);
  assert_int_equal(packet.getSpeedStepCab(), 10239);
}

// SPEED28 and SPEED128 share a prefix, so a sloppy match would read one as the
// other and silently halve every locomotive's resolution.
void test_config_speed_prefix_is_not_confused(void **state)
{
  {
    char buffer[32] = "D SPEED1280";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[32] = "D SPEED";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[32] = "D SPEED12";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
}

// An out-of-range cab has to reach the controller so it can answer <X>. A
// packet dropped here would leave the host with silence, unable to tell a
// rejected command from an applied one (#4).
void test_config_speed_out_of_range_cab_still_parses(void **state)
{
  char buffer[32] = "D SPEED28 99999";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getSpeedStepCab(), 99999);
}

// A trailing field that is not a bare cab number is refused outright rather
// than half-read -- the same rule as the four-field <t> form (#7). Getting this
// wrong changes the encoding for a locomotive nobody named.
void test_config_speed_rejects_trailing_junk(void **state)
{
  {
    char buffer[32] = "D SPEED28 3 1";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[32] = "D SPEED28 three";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
  {
    char buffer[32] = "D SPEED128 3x";
    PicoDccExPacket packet(buffer);
    assert_false(packet.isValid());
  }
}

// Trailing whitespace and the line endings a serial host may leave on are not
// a fourth field.
void test_config_speed_tolerates_trailing_whitespace(void **state)
{
  {
    char buffer[32] = "D SPEED128  ";
    PicoDccExPacket packet(buffer);
    assert_true(packet.isValid());
    assert_int_equal(packet.getSpeedStepCab(), 0);
  }
  {
    char buffer[32] = "D SPEED28 3\r\n";
    PicoDccExPacket packet(buffer);
    assert_true(packet.isValid());
    assert_int_equal(packet.getSpeedStepCab(), 3);
  }
}

// The <D ACK ...> commands must be untouched by the new branch: they are the
// only <D> subcommand this station accepted before, and they share the parse.
void test_config_ack_still_parses_alongside_speed(void **state)
{
  char buffer[32] = "D ACK LIMIT 60";
  PicoDccExPacket packet(buffer);

  assert_true(packet.isValid());
  assert_int_equal(packet.getConfigSubcommand(), DCCEX_CONFIG_ACK);
  assert_int_equal(packet.getConfigParamType(), 1);
  assert_int_equal(packet.getConfigValue(), 60);
}

// An unknown <D> subcommand is still refused, not swept into the speed branch.
void test_config_unknown_subcommand_is_rejected(void **state)
{
  char buffer[32] = "D WIBBLE 3";
  PicoDccExPacket packet(buffer);

  assert_false(packet.isValid());
}


int main(int argc, char *argv[])
{
  printf("Runing Tests\n");

  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_invalid_packet),
      cmocka_unit_test(test_ver_packet),
      cmocka_unit_test(test_num_cabs_packet),
      cmocka_unit_test(test_power_off_packet),
      cmocka_unit_test(test_power_on_packet),
      cmocka_unit_test(test_power_off_main_packet),
      cmocka_unit_test(test_power_on_main_packet),
      cmocka_unit_test(test_power_off_prog_packet),
      cmocka_unit_test(test_power_on_prog_packet),
      cmocka_unit_test(test_acc_1_packet),
      cmocka_unit_test(test_acc_2_packet),
      cmocka_unit_test(test_throttle_packet),
      cmocka_unit_test(test_function_packet),
      cmocka_unit_test(test_estop_packet),
      cmocka_unit_test(test_throttle_cab_zero_is_rejected),
      cmocka_unit_test(test_throttle_cab_above_range_is_rejected),
      cmocka_unit_test(test_throttle_cab_range_boundaries_accepted),
      cmocka_unit_test(test_throttle_speed_above_max_is_rejected),
      cmocka_unit_test(test_throttle_speed_minus_one_is_accepted),
      cmocka_unit_test(test_throttle_speed_below_minus_one_is_rejected),
      cmocka_unit_test(test_malformed_throttle_is_rejected),
      cmocka_unit_test(test_function_cab_range_is_validated),
      cmocka_unit_test(test_cab_update_reports_estop),
      cmocka_unit_test(test_cab_update_long_address_not_truncated),
      cmocka_unit_test(test_config_ack_limit_valid),
      cmocka_unit_test(test_config_ack_limit_too_low),
      cmocka_unit_test(test_config_ack_limit_too_high),
      cmocka_unit_test(test_config_ack_min_valid),
      cmocka_unit_test(test_config_ack_min_too_low),
      cmocka_unit_test(test_config_ack_min_too_high),
      cmocka_unit_test(test_config_ack_max_valid),
      cmocka_unit_test(test_config_ack_max_too_low),
      cmocka_unit_test(test_config_ack_max_too_high),
      cmocka_unit_test(test_config_save_command),
      cmocka_unit_test(test_config_malformed_ack),
      cmocka_unit_test(test_config_speed128_no_cab),
      cmocka_unit_test(test_config_speed28_no_cab),
      cmocka_unit_test(test_config_speed28_with_cab),
      cmocka_unit_test(test_config_speed128_with_long_cab),
      cmocka_unit_test(test_config_speed_prefix_is_not_confused),
      cmocka_unit_test(test_config_speed_out_of_range_cab_still_parses),
      cmocka_unit_test(test_config_speed_rejects_trailing_junk),
      cmocka_unit_test(test_config_speed_tolerates_trailing_whitespace),
      cmocka_unit_test(test_config_ack_still_parses_alongside_speed),
      cmocka_unit_test(test_config_unknown_subcommand_is_rejected)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}