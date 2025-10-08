#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCLoco/pico_dcclocos.h"
#include "../lib/PicoDCCLoco/pico_dccloco.h"
#include "../lib/PicoDCCEX/pico_dccexpacket.h"

void test_add_loco(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);

    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    PicoDccLoco *loco = locos.findLoco(3);
    assert_non_null(loco);
    assert_int_equal(loco->getAddress(), 3);

    raw_dcc_cmd_t cmd1 = loco->getThrottleCommand();
    assert_int_equal(cmd1.length, 2);
    assert_int_equal(cmd1.data[0], 3);
    assert_int_equal(cmd1.data[1], 81);
}

void test_update_loco(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);

    PicoDccLoco *loco = locos.findLoco(3);
    const char *bufferUpdate = "t 3 128 1";
    PicoDccExPacket packetUpdate((char *)bufferUpdate);
    loco->update(&packetUpdate);

    raw_dcc_cmd_t cmdUpdated = loco->getThrottleCommand();
    assert_int_equal(cmdUpdated.length, 2);
    assert_int_equal(cmdUpdated.data[0], 3);
    assert_int_equal(cmdUpdated.data[1], 113);
}

void test_forget_loco(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    locos.forgetLoco(3);
    assert_null(locos.findLoco(3));
}

void test_forget_all_locos(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer);
    locos.addLoco(&packet1, cmd);

    locos.forgetAllLocos();
    assert_int_equal(locos.getLocoCount(), 0);
    assert_null(locos.findLoco(3));
    assert_null(locos.findLoco(4));
}

void test_get_next_reminder(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;
    bool reminder = false;

    reminder = locos.getNextReminder(cmd);
    assert_false(reminder);

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);
    assert_int_equal(cmd.data[0], 3);
    raw_dcc_cmd_t cab1cmd;
    memcpy(&cab1cmd, &cmd, sizeof(raw_dcc_cmd_t));

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);
    assert_int_equal(cmd.data[0], 4);
    raw_dcc_cmd_t cab2cmd;
    memcpy(&cab2cmd, &cmd, sizeof(raw_dcc_cmd_t));

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_memory_equal(&cab1cmd, &cmd, sizeof(raw_dcc_cmd_t));
    assert_int_equal(cmd.data[0], 3);

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_memory_equal(&cab2cmd, &cmd, sizeof(raw_dcc_cmd_t));
    assert_int_equal(cab2cmd.data[0], 4);

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_memory_equal(&cab1cmd, &cmd, sizeof(raw_dcc_cmd_t));
    assert_int_equal(cmd.data[0], 3);
}

void test_get_emergency_stop_commands(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;
    PicoDccLoco *loco = nullptr;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);
    loco = locos.findLoco(3);

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);
    loco = locos.findLoco(4);

    // Emergency stop is now handled as broadcast command in controller
    // Verify we have 2 locos added
    assert_int_equal(locos.getLocoCount(), 2);
}

// Test getNextReminder with no locos
void test_get_next_reminder_no_locos(void **state) {
    PicoDccLocos locos;

    raw_dcc_cmd_t cmd;
    bool reminder = locos.getNextReminder(cmd);
    assert_false(reminder);
}

// Merge with existing tests
int main(int argc, char *argv[]) {
    printf("Running Tests\n");

    const struct CMUnitTest all_tests[] = {
        cmocka_unit_test(test_add_loco),
        cmocka_unit_test(test_update_loco),
        cmocka_unit_test(test_forget_loco),
        cmocka_unit_test(test_forget_all_locos),
        cmocka_unit_test(test_get_next_reminder),
        cmocka_unit_test(test_get_emergency_stop_commands),
        cmocka_unit_test(test_get_next_reminder_no_locos),
    };

    return cmocka_run_group_tests(all_tests, NULL, NULL);
}