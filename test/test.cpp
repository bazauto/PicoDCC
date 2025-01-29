#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCEX/pico_dccexpacket.h"

void test_true(void **state)
{
  assert_int_equal(1, 1);
}


void test_invalid_packet(void **state)
{
  char buffer[10] = "x 123";
  PicoDccExPacket packet(buffer);
  assert_false(packet.isValid());
}


int main(int argc, char *argv[])
{
  printf("Runing Tests\n");

  void *state;

  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_true),
      cmocka_unit_test(test_invalid_packet)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}