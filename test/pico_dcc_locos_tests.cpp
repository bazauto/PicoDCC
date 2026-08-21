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
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);

    // Both locos must be present before the clear, otherwise the assertions
    // below pass whether forgetAllLocos() works or not.
    assert_int_equal(locos.getLocoCount(), 2);

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
    cab1cmd.repeats = 0;  // getNextReminder() sets repeats to 0

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);
    assert_int_equal(cmd.data[0], 4);
    raw_dcc_cmd_t cab2cmd;
    memcpy(&cab2cmd, &cmd, sizeof(raw_dcc_cmd_t));
    cab2cmd.repeats = 0;  // getNextReminder() sets repeats to 0

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_memory_equal(&cab1cmd, &cmd, sizeof(raw_dcc_cmd_t));
    assert_int_equal(cmd.data[0], 3);
    assert_int_equal(cmd.repeats, 0);  // Verify reminders have no repeats

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_memory_equal(&cab2cmd, &cmd, sizeof(raw_dcc_cmd_t));
    assert_int_equal(cab2cmd.data[0], 4);
    assert_int_equal(cmd.repeats, 0);  // Verify reminders have no repeats

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_memory_equal(&cab1cmd, &cmd, sizeof(raw_dcc_cmd_t));
    assert_int_equal(cmd.data[0], 3);
    assert_int_equal(cmd.repeats, 0);  // Verify reminders have no repeats
}

void test_get_emergency_stop_commands(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;
    PicoDccLoco *loco = nullptr;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);
    loco = locos.findLoco(3);
    assert_non_null(loco);

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);
    loco = locos.findLoco(4);
    assert_non_null(loco);

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

// Test getNextReminder after removing the last reminded loco
void test_get_next_reminder_after_loco_removal(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    // Add three locomotives
    const char *buffer1 = "t 3 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);

    const char *buffer2 = "t 4 0 0";
    PicoDccExPacket packet2((char *)buffer2);
    locos.addLoco(&packet2, cmd);

    const char *buffer3 = "t 5 0 0";
    PicoDccExPacket packet3((char *)buffer3);
    locos.addLoco(&packet3, cmd);

    // Get first reminder (should be loco 3)
    bool reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 3);

    // Get second reminder (should be loco 4)
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 4);

    // Now remove loco 4 (the last reminded loco)
    locos.forgetLoco(4);
    assert_int_equal(locos.getLocoCount(), 2);

    // Next reminder should start from beginning (loco 3), not crash or skip
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 3);  // Should return to beginning of list

    // Following reminder should be loco 5
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 5);
}

// Test getNextReminder with single loco removal and re-addition
void test_get_next_reminder_single_loco_cycle(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    // Add single locomotive
    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);

    // Get reminder (should be loco 3)
    bool reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 3);

    // Remove the only loco
    locos.forgetLoco(3);
    assert_int_equal(locos.getLocoCount(), 0);

    // Should return false when no locos exist
    reminder = locos.getNextReminder(cmd);
    assert_false(reminder);

    // Re-add locomotive
    locos.addLoco(&packet, cmd);
    assert_int_equal(locos.getLocoCount(), 1);

    // Should work again
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 3);
}

// Test getNextReminder with middle loco removal
void test_get_next_reminder_middle_loco_removal(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    // Add three locomotives
    const char *buffer1 = "t 10 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    locos.addLoco(&packet1, cmd);

    const char *buffer2 = "t 20 0 0";
    PicoDccExPacket packet2((char *)buffer2);
    locos.addLoco(&packet2, cmd);

    const char *buffer3 = "t 30 0 0";
    PicoDccExPacket packet3((char *)buffer3);
    locos.addLoco(&packet3, cmd);

    // Cycle through all locos to establish order
    bool reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 10);

    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 20);

    // Remove the middle loco (20) after it was the last reminded
    locos.forgetLoco(20);
    assert_int_equal(locos.getLocoCount(), 2);

    // Next reminder should restart from beginning (loco 10)
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 10);

    // Next should be loco 30
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 30);

    // Should cycle back to loco 10
    reminder = locos.getNextReminder(cmd);
    assert_true(reminder);
    assert_int_equal(cmd.data[0], 10);
}

// Test thread-safe getLocoCount
void test_thread_safe_loco_count(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    // Initially should be 0
    assert_int_equal(locos.getLocoCount(), 0);

    // Add locomotive
    const char *buffer = "t 42 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);

    // Should now be 1 (this was the failing case)
    assert_int_equal(locos.getLocoCount(), 1);

    // Add another locomotive  
    const char *buffer2 = "t 43 0 0";
    PicoDccExPacket packet2((char *)buffer2);
    locos.addLoco(&packet2, cmd);

    // Should now be 2
    assert_int_equal(locos.getLocoCount(), 2);

    // Remove one locomotive
    locos.forgetLoco(42);
    
    // Should be back to 1
    assert_int_equal(locos.getLocoCount(), 1);

    // Remove all locomotives
    locos.forgetAllLocos();
    
    // Should be 0
    assert_int_equal(locos.getLocoCount(), 0);
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
        cmocka_unit_test(test_get_next_reminder_after_loco_removal),
        cmocka_unit_test(test_get_next_reminder_single_loco_cycle),
        cmocka_unit_test(test_get_next_reminder_middle_loco_removal),
        cmocka_unit_test(test_thread_safe_loco_count),
    };

    return cmocka_run_group_tests(all_tests, NULL, NULL);
}