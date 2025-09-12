#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdexcept>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCLoco/pico_dcclocos.h"
#include "../lib/PicoDCCLoco/pico_dccloco.h"
#include "../lib/PicoDCCEX/pico_dccexpacket.h"

extern "C" {
bool __wrap_queue_add_blocking(queue_t *queue, const void *item);
bool __wrap_queue_add_blocking(queue_t *queue, const void *item) {

    check_expected_ptr(item);

    return true;
}
}

void test_add_loco(void **state) {
    queue_t dccex_cmd_queue;
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
    PicoDccLocos locos(&dccex_cmd_queue);

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);

    expect_memory(__wrap_queue_add_blocking, item, packet.getPacketData(), sizeof(pico_dccex_packet));

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
    queue_t dccex_cmd_queue;
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
    PicoDccLocos locos(&dccex_cmd_queue);
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    expect_memory(__wrap_queue_add_blocking, item, packet.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet, cmd);

    
    PicoDccLoco *loco = locos.findLoco(3);
    const char *bufferUpdate = "t 3 128 1";
    PicoDccExPacket packetUpdate((char *)bufferUpdate);
    expect_memory(__wrap_queue_add_blocking, item, packetUpdate.getPacketData(), sizeof(pico_dccex_packet));
    locos.updateLoco(loco, &packetUpdate, cmd);

    raw_dcc_cmd_t cmdUpdated = loco->getThrottleCommand();
    assert_int_equal(cmdUpdated.length, 2);
    assert_int_equal(cmdUpdated.data[0], 3);
    assert_int_equal(cmdUpdated.data[1], 113);
}
void test_forget_loco(void **state) {
    queue_t dccex_cmd_queue;
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
    PicoDccLocos locos(&dccex_cmd_queue);

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    expect_memory(__wrap_queue_add_blocking, item, packet.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet, cmd);

    locos.forgetLoco(3);
    assert_null(locos.findLoco(3));
}
void test_forget_all_locos(void **state) {
    queue_t dccex_cmd_queue;
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
    PicoDccLocos locos(&dccex_cmd_queue);

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    expect_memory(__wrap_queue_add_blocking, item, packet.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet, cmd);

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer);
    expect_memory(__wrap_queue_add_blocking, item, packet1.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet1, cmd);

    locos.forgetAllLocos();
    assert_int_equal(locos.getLocoCount(), 0);
    assert_null(locos.findLoco(3));
    assert_null(locos.findLoco(4));
}
void test_get_next_reminder(void **state) {
    queue_t dccex_cmd_queue;
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
    PicoDccLocos locos(&dccex_cmd_queue);
    raw_dcc_cmd_t cmd;
    bool reminder = false;

    reminder = locos.getNextReminder(cmd);
    assert_false(reminder);

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    expect_memory(__wrap_queue_add_blocking, item, packet.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet, cmd);
    assert_int_equal(cmd.data[0], 3);
    raw_dcc_cmd_t cab1cmd;
    memcpy(&cab1cmd, &cmd, sizeof(raw_dcc_cmd_t));

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    expect_memory(__wrap_queue_add_blocking, item, packet1.getPacketData(), sizeof(pico_dccex_packet));
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
    queue_t dccex_cmd_queue;
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), 10);
    PicoDccLocos locos(&dccex_cmd_queue);
    raw_dcc_cmd_t cmd;
    PicoDccLoco *loco = nullptr;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    expect_memory(__wrap_queue_add_blocking, item, packet.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet, cmd);
    loco = locos.findLoco(3);
    raw_dcc_cmd_t cab1cmd(loco->getEmergecyStopCommand());

    const char *buffer1 = "t 4 0 0";
    PicoDccExPacket packet1((char *)buffer1);
    expect_memory(__wrap_queue_add_blocking, item, packet1.getPacketData(), sizeof(pico_dccex_packet));
    locos.addLoco(&packet1, cmd);
    loco = locos.findLoco(4);
    raw_dcc_cmd_t cab2cmd(loco->getEmergecyStopCommand());

    uint16_t addr1 = 3; // Need to use this to ensure all bytes written
    pico_dccex_packet eStopPacket1 = {'t', addr1, 0, 0, false, DCCEX_TRACK_ALL};
    expect_memory_count(__wrap_queue_add_blocking, item, &eStopPacket1, sizeof(pico_dccex_packet), 1);

    uint16_t addr2 = 4; // Need to use this to ensure all bytes written
    pico_dccex_packet eStopPacket2 = {'t', addr2, 0, 0, false, DCCEX_TRACK_ALL};
    expect_memory(__wrap_queue_add_blocking, item, &eStopPacket2, sizeof(pico_dccex_packet));

    auto stopCmds = locos.getEmergencyStopCommands();

    assert_false(stopCmds.empty());
    assert_int_equal(stopCmds.size(), 2);

    raw_dcc_cmd_t cmd1 = stopCmds.front();
    raw_dcc_cmd_t cmd2 = stopCmds.back();
    assert_memory_equal(&cmd1, &cab1cmd, sizeof(raw_dcc_cmd_t));
    assert_memory_equal(&cmd2, &cab2cmd, sizeof(raw_dcc_cmd_t));
}

int main(int argc, char *argv[]) {
    printf("Running Tests\n");

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_add_loco),
        cmocka_unit_test(test_update_loco),
        cmocka_unit_test(test_forget_loco),
        cmocka_unit_test(test_forget_all_locos),
        cmocka_unit_test(test_get_next_reminder),
        cmocka_unit_test(test_get_emergency_stop_commands)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}