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
    assert_int_equal(cmd1.length, 3);
    assert_int_equal(cmd1.data[0], 3);
    assert_int_equal(cmd1.data[1], 0x3F);
    assert_int_equal(cmd1.data[2], 0x00);  // controlled stop, reverse (#48)
}

void test_update_loco(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 3 0 0";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);

    PicoDccLoco *loco = locos.findLoco(3);
    const char *bufferUpdate = "t 3 126 1";
    PicoDccExPacket packetUpdate((char *)bufferUpdate);
    loco->update(&packetUpdate);

    raw_dcc_cmd_t cmdUpdated = loco->getThrottleCommand();
    assert_int_equal(cmdUpdated.length, 3);
    assert_int_equal(cmdUpdated.data[0], 3);
    assert_int_equal(cmdUpdated.data[1], 0x3F);
    assert_int_equal(cmdUpdated.data[2], 0x80 | 127);
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

// ---------------------------------------------------------------------------
// Rejection at the collection level (#2, #11, #12, #16)
// ---------------------------------------------------------------------------

void test_add_loco_returns_true_for_valid_packet(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 3 10 1";
    PicoDccExPacket packet((char *)buffer);

    assert_true(locos.addLoco(&packet, cmd));
    assert_int_equal(locos.getLocoCount(), 1);
}

void test_add_loco_rejects_cab_zero(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 0 10 1";
    PicoDccExPacket packet((char *)buffer);

    assert_false(locos.addLoco(&packet, cmd));
    assert_int_equal(locos.getLocoCount(), 0);
    assert_int_equal(cmd.length, 0);
}

void test_add_loco_rejects_address_above_14_bits(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 65535 10 1";
    PicoDccExPacket packet((char *)buffer);

    assert_false(locos.addLoco(&packet, cmd));
    assert_int_equal(locos.getLocoCount(), 0);
}

// This is #2's crash path: before the fix, constructing a PicoDccLoco from a
// non-throttle/function packet reached std::terminate and aborted the test
// binary. Reaching the assertions below at all is part of what this proves.
void test_add_loco_rejects_non_throttle_packet(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "s";
    PicoDccExPacket packet((char *)buffer);

    assert_false(locos.addLoco(&packet, cmd));
    assert_int_equal(locos.getLocoCount(), 0);
}

void test_add_loco_caps_at_max_loco(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    for (uint16_t addr = 1; addr <= MAX_LOCO; addr++) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "t %u 10 1", addr);
        PicoDccExPacket packet(buffer);
        assert_true(locos.addLoco(&packet, cmd));
    }
    assert_int_equal(locos.getLocoCount(), MAX_LOCO);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "t %u 10 1", MAX_LOCO + 1);
    PicoDccExPacket packet(buffer);
    assert_false(locos.addLoco(&packet, cmd));
    assert_int_equal(locos.getLocoCount(), MAX_LOCO);
}

void test_update_loco_rejects_out_of_range_speed(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 3 10 1";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);
    assert_int_equal(cmd.data[2], 0x80 | 11);

    const char *bufferUpdate = "t 3 128 1";
    PicoDccExPacket packetUpdate((char *)bufferUpdate);
    raw_dcc_cmd_t updated;
    bool found = locos.updateLocoThrottle(3, &packetUpdate, updated);

    assert_true(found);
    assert_false(locos.findLoco(3) == nullptr);
    assert_int_equal(updated.data[2], 0x80 | 11);  // unchanged: rejected
}

void test_update_loco_estop_and_reminder(void **state) {
    PicoDccLocos locos;
    raw_dcc_cmd_t cmd;

    const char *buffer = "t 3 20 1";
    PicoDccExPacket packet((char *)buffer);
    locos.addLoco(&packet, cmd);

    const char *bufferUpdate = "t 3 -1 1";
    PicoDccExPacket packetUpdate((char *)bufferUpdate);
    raw_dcc_cmd_t updated;
    assert_true(locos.updateLocoThrottle(3, &packetUpdate, updated));
    assert_int_equal(updated.length, 3);
    assert_int_equal(updated.data[0], 0x03);
    assert_int_equal(updated.data[1], 0x3F);
    assert_int_equal(updated.data[2], 0x81);

    raw_dcc_cmd_t reminder;
    assert_true(locos.getNextReminder(reminder));
    assert_int_equal(reminder.data[0], 0x03);
    assert_int_equal(reminder.data[1], 0x3F);
    assert_int_equal(reminder.data[2], 0x81);
    assert_int_equal(reminder.repeats, 0);
}

// --------------------------------------------------------------------------
// Speed step modes (#8)
// --------------------------------------------------------------------------

void test_station_speed_steps_default_to_128(void **state) {
    PicoDccLocos locos;

    assert_int_equal(locos.getStationSpeedSteps(), DCC_SPEED_STEPS_128);
}

// A loco created after the default changes inherits it -- the ordinary case
// when the orchestrator sets the station mode at startup and then drives.
void test_new_loco_inherits_the_station_default(void **state) {
    PicoDccLocos locos;
    assert_true(locos.setStationSpeedSteps(DCC_SPEED_STEPS_28));

    const char *buffer = "t 3 10 1";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    assert_int_equal(locos.getLocoSpeedSteps(3), DCC_SPEED_STEPS_28);
    assert_int_equal(cmd.length, 2);
    assert_int_equal(cmd.data[1], 0x72);
}

// A loco created *before* the default changes moves with it, and its stored
// command is re-encoded. Core 1 reads that command for reminders, so a stale
// one would keep the old encoding on the rails indefinitely.
void test_existing_locos_follow_a_later_default_change(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 10 1";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);
    assert_int_equal(locos.getLocoSpeedSteps(3), DCC_SPEED_STEPS_128);

    assert_true(locos.setStationSpeedSteps(DCC_SPEED_STEPS_28));

    assert_int_equal(locos.getLocoSpeedSteps(3), DCC_SPEED_STEPS_28);
    raw_dcc_cmd_t reminder;
    assert_true(locos.getNextReminder(reminder));
    assert_int_equal(reminder.length, 2);
    assert_int_equal(reminder.data[1], 0x72);
}

// A named loco stays where it was put. It is named because its decoder cannot
// do 128 steps, so a later station-wide command must not drag it back.
void test_named_loco_ignores_a_later_default_change(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 10 1";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    assert_true(locos.setLocoSpeedSteps(3, DCC_SPEED_STEPS_28));
    assert_true(locos.setStationSpeedSteps(DCC_SPEED_STEPS_128));

    assert_int_equal(locos.getLocoSpeedSteps(3), DCC_SPEED_STEPS_28);
    assert_int_equal(locos.getStationSpeedSteps(), DCC_SPEED_STEPS_128);
}

// Naming a cab that is not in the collection creates it, stopped and forward,
// so the mode is already right when the first throttle command arrives. The
// orchestrator re-asserts the roster on seeing the boot banner, which is
// before it drives anything.
void test_naming_an_unknown_cab_creates_it_stopped(void **state) {
    PicoDccLocos locos;

    assert_true(locos.setLocoSpeedSteps(7, DCC_SPEED_STEPS_28));

    assert_int_equal(locos.getLocoCount(), 1);
    PicoDccLoco *loco = locos.findLoco(7);
    assert_non_null(loco);
    assert_int_equal(loco->getSpeed(), 0);
    assert_true(loco->isForward());
    assert_int_equal(loco->getSpeedSteps(), DCC_SPEED_STEPS_28);

    // The reminder it now generates is a stop, which is the safe direction to
    // be wrong in: it can only ever hold a locomotive still.
    raw_dcc_cmd_t reminder;
    assert_true(locos.getNextReminder(reminder));
    assert_int_equal(reminder.length, 2);
    assert_int_equal(reminder.data[0], 0x07);
    assert_int_equal(reminder.data[1], 0x60);
}

// The created loco keeps its override, rather than being treated as a fresh
// loco following the default.
void test_cab_created_by_naming_keeps_its_override(void **state) {
    PicoDccLocos locos;

    assert_true(locos.setLocoSpeedSteps(7, DCC_SPEED_STEPS_28));
    assert_true(locos.setStationSpeedSteps(DCC_SPEED_STEPS_128));

    assert_int_equal(locos.getLocoSpeedSteps(7), DCC_SPEED_STEPS_28);
}

void test_speed_steps_reject_bad_input(void **state) {
    PicoDccLocos locos;

    assert_false(locos.setStationSpeedSteps(14));
    assert_int_equal(locos.getStationSpeedSteps(), DCC_SPEED_STEPS_128);

    assert_false(locos.setLocoSpeedSteps(0, DCC_SPEED_STEPS_28));      // broadcast
    assert_false(locos.setLocoSpeedSteps(10240, DCC_SPEED_STEPS_28));  // too high
    assert_false(locos.setLocoSpeedSteps(3, 14));                      // no such mode
    assert_int_equal(locos.getLocoCount(), 0);
}

// A full collection cannot take the phantom entry, and must say so rather than
// silently reporting the mode as set.
void test_naming_an_unknown_cab_refuses_when_full(void **state) {
    PicoDccLocos locos;

    for (int cab = 1; cab <= MAX_LOCO; cab++) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "t %d 0 1", cab);
        PicoDccExPacket packet(buffer);
        raw_dcc_cmd_t cmd;
        assert_true(locos.addLoco(&packet, cmd));
    }

    assert_false(locos.setLocoSpeedSteps(MAX_LOCO + 1, DCC_SPEED_STEPS_28));
    assert_int_equal(locos.getLocoCount(), MAX_LOCO);

    // A cab already in the collection is still fine -- no new entry is needed.
    assert_true(locos.setLocoSpeedSteps(1, DCC_SPEED_STEPS_28));
    assert_int_equal(locos.getLocoSpeedSteps(1), DCC_SPEED_STEPS_28);
}

// An unknown cab reports the station default rather than a made-up value.
void test_unknown_cab_reports_the_station_default(void **state) {
    PicoDccLocos locos;
    assert_true(locos.setStationSpeedSteps(DCC_SPEED_STEPS_28));

    assert_int_equal(locos.getLocoSpeedSteps(99), DCC_SPEED_STEPS_28);
    assert_int_equal(locos.getLocoCount(), 0);  // asking did not create it
}

// --------------------------------------------------------------------------
// stopAllLocos(): the <!> broadcast holds the layout stopped (#3)
// --------------------------------------------------------------------------

// The locos stay in the collection, at speed 0. Emptying it left Core 1's
// reminder generator with nothing to repeat, so a loco that missed the single
// broadcast kept its previous speed with nothing ever contradicting it.
void test_stop_all_locos_keeps_them_at_speed_zero(void **state) {
    PicoDccLocos locos;

    for (int cab = 1; cab <= 3; cab++) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "t %d 100 1", cab);
        PicoDccExPacket packet(buffer);
        raw_dcc_cmd_t cmd;
        assert_true(locos.addLoco(&packet, cmd));
    }

    assert_int_equal(locos.stopAllLocos(), 3);  // all three were moving
    assert_int_equal(locos.getLocoCount(), 3);  // and all three are still here

    for (int cab = 1; cab <= 3; cab++) {
        PicoDccLoco *loco = locos.findLoco((uint16_t)cab);
        assert_non_null(loco);
        assert_int_equal(loco->getSpeed(), 0);
    }
}

// The reminder stream is what actually holds the layout: it must emit a stop
// for the loco, not the speed it was doing before.
void test_reminders_after_stop_all_assert_a_stop(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 100 1";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    locos.stopAllLocos();

    raw_dcc_cmd_t reminder;
    assert_true(locos.getNextReminder(reminder));
    assert_int_equal(reminder.data[0], 0x03);
    assert_int_equal(reminder.data[1], 0x3F);
    assert_int_equal(reminder.data[2], 0x80);  // 128-step controlled stop, forward
}

// Direction is preserved: the decoder still needs to know which way to go when
// an operator resumes.
void test_stop_all_locos_preserves_direction(void **state) {
    PicoDccLocos locos;

    const char *forward = "t 3 100 1";
    PicoDccExPacket p1((char *)forward);
    raw_dcc_cmd_t c1;
    locos.addLoco(&p1, c1);

    const char *reverse = "t 4 100 0";
    PicoDccExPacket p2((char *)reverse);
    raw_dcc_cmd_t c2;
    locos.addLoco(&p2, c2);

    locos.stopAllLocos();

    assert_true(locos.findLoco(3)->isForward());
    assert_false(locos.findLoco(4)->isForward());
}

// Already-stopped locos are not counted as having been stopped, and are left
// exactly as they were.
void test_stop_all_locos_counts_only_the_moving(void **state) {
    PicoDccLocos locos;

    const char *stopped = "t 3 0 1";
    PicoDccExPacket p1((char *)stopped);
    raw_dcc_cmd_t c1;
    locos.addLoco(&p1, c1);

    const char *moving = "t 4 100 1";
    PicoDccExPacket p2((char *)moving);
    raw_dcc_cmd_t c2;
    locos.addLoco(&p2, c2);

    assert_int_equal(locos.stopAllLocos(), 1);
    assert_int_equal(locos.getLocoCount(), 2);
}

// A per-loco speed step override survives, because the loco does (#8).
void test_stop_all_locos_keeps_speed_step_overrides(void **state) {
    PicoDccLocos locos;

    const char *buffer = "t 3 100 1";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);
    assert_true(locos.setLocoSpeedSteps(3, DCC_SPEED_STEPS_28));

    locos.stopAllLocos();

    assert_int_equal(locos.getLocoSpeedSteps(3), DCC_SPEED_STEPS_28);

    raw_dcc_cmd_t reminder;
    assert_true(locos.getNextReminder(reminder));
    assert_int_equal(reminder.length, 2);
    assert_int_equal(reminder.data[1], 0x60);  // 28-step controlled stop, forward
}

// An empty collection is not an error, and does not fabricate a stop.
void test_stop_all_locos_on_an_empty_collection(void **state) {
    PicoDccLocos locos;

    assert_int_equal(locos.stopAllLocos(), 0);
    assert_int_equal(locos.getLocoCount(), 0);
}

// Merge with existing tests
// ---------------------------------------------------------------------------
// #17: the emergency-stop echo must not hold locos_lock across the UART writes
// ---------------------------------------------------------------------------
//
// sendEmergencyStopResponses() used to hold locos_lock for the whole emit loop.
// DCCEX_RESPONSE is a blocking uart_puts, so with a full table that was 70-90 ms of
// Core 1 parked in getNextReminder() emitting no DCC at all -- inside the station's own
// 100 ms timing-violation cutoff.
//
// mock_uart_puts_hook fires from inside uart_puts, standing in for the window when the
// real one is still transmitting. If the lock is held there, a reminder cannot be taken.

static PicoDccLocos *g_hook_locos = nullptr;
static int g_hook_calls = 0;
static int g_hook_reminders_taken = 0;

static void reminder_during_uart_write(void) {
    g_hook_calls++;
    raw_dcc_cmd_t cmd;
    if (g_hook_locos && g_hook_locos->getNextReminder(cmd)) {
        g_hook_reminders_taken++;
    }
}

void test_estop_echo_does_not_hold_the_lock_across_uart(void **state) {
    (void)state;

    PicoDccLocos locos;
    const char *a = "t 3 40 1";
    const char *b = "t 7 20 0";
    PicoDccExPacket pa((char *)a);
    PicoDccExPacket pb((char *)b);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&pa, cmd);
    locos.addLoco(&pb, cmd);

    uart_output_log.clear();
    mock_reset_sem_stats();
    g_hook_locos = &locos;
    g_hook_calls = 0;
    g_hook_reminders_taken = 0;
    mock_uart_puts_hook = reminder_during_uart_write;

    locos.sendEmergencyStopResponses();

    mock_uart_puts_hook = nullptr;
    g_hook_locos = nullptr;

    // One echo per loco, so the hook ran during a real write.
    assert_int_equal(g_hook_calls, 2);

    // The point of the test: Core 1 could take a reminder during every one of those
    // writes. Holding the lock across the emit loop makes this zero.
    assert_int_equal(g_hook_reminders_taken, 2);

    // And nothing anywhere in that path wanted to block.
    assert_int_equal(mock_sem_would_block, 0);
}

// The refactor that fixed the above must not change what goes on the wire: one cab
// update per valid loco, each carrying that loco's own direction, speed 0.
void test_estop_echo_reports_each_loco_own_direction(void **state) {
    (void)state;

    PicoDccLocos locos;
    const char *fwd = "t 3 40 1";
    const char *rev = "t 7 20 0";
    PicoDccExPacket pf((char *)fwd);
    PicoDccExPacket pr((char *)rev);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&pf, cmd);
    locos.addLoco(&pr, cmd);

    uart_output_log.clear();
    locos.sendEmergencyStopResponses();

    assert_int_equal(uart_output_log.size(), 2);

    // <l cab reg speedByte functMap>, speedByte bit 7 = direction, low bits = speed
    // where 0 is stop. So a stopped loco is 128 forward and 0 reverse -- the direction
    // is the loco's own, not a blanket "forward" (#3), and the speed is 0 either way.
    assert_string_equal(uart_output_log[0].c_str(), "<l 3 0 128 0>");
    assert_string_equal(uart_output_log[1].c_str(), "<l 7 0 0 0>");

    // The echo reports, it does not forget: #3 keeps the table so reminders can go on
    // asserting "stopped" at a loco that missed the broadcast.
    assert_int_equal(locos.getLocoCount(), 2);
}

// getNextReminder runs on Core 1 every pass and must never wait (#17).
void test_get_next_reminder_never_blocks(void **state) {
    (void)state;

    PicoDccLocos locos;
    const char *buffer = "t 3 40 1";
    PicoDccExPacket packet((char *)buffer);
    raw_dcc_cmd_t cmd;
    locos.addLoco(&packet, cmd);

    mock_reset_sem_stats();
    for (int i = 0; i < 10; ++i) {
        locos.getNextReminder(cmd);
    }
    assert_int_equal(mock_sem_would_block, 0);
}

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
        cmocka_unit_test(test_add_loco_returns_true_for_valid_packet),
        cmocka_unit_test(test_add_loco_rejects_cab_zero),
        cmocka_unit_test(test_add_loco_rejects_address_above_14_bits),
        cmocka_unit_test(test_add_loco_rejects_non_throttle_packet),
        cmocka_unit_test(test_add_loco_caps_at_max_loco),
        cmocka_unit_test(test_update_loco_rejects_out_of_range_speed),
        cmocka_unit_test(test_update_loco_estop_and_reminder),
        cmocka_unit_test(test_station_speed_steps_default_to_128),
        cmocka_unit_test(test_new_loco_inherits_the_station_default),
        cmocka_unit_test(test_existing_locos_follow_a_later_default_change),
        cmocka_unit_test(test_named_loco_ignores_a_later_default_change),
        cmocka_unit_test(test_naming_an_unknown_cab_creates_it_stopped),
        cmocka_unit_test(test_cab_created_by_naming_keeps_its_override),
        cmocka_unit_test(test_speed_steps_reject_bad_input),
        cmocka_unit_test(test_naming_an_unknown_cab_refuses_when_full),
        cmocka_unit_test(test_unknown_cab_reports_the_station_default),
        cmocka_unit_test(test_stop_all_locos_keeps_them_at_speed_zero),
        cmocka_unit_test(test_reminders_after_stop_all_assert_a_stop),
        cmocka_unit_test(test_stop_all_locos_preserves_direction),
        cmocka_unit_test(test_stop_all_locos_counts_only_the_moving),
        cmocka_unit_test(test_stop_all_locos_keeps_speed_step_overrides),
        cmocka_unit_test(test_stop_all_locos_on_an_empty_collection),
        cmocka_unit_test(test_estop_echo_does_not_hold_the_lock_across_uart),
        cmocka_unit_test(test_estop_echo_reports_each_loco_own_direction),
        cmocka_unit_test(test_get_next_reminder_never_blocks),
    };

    return cmocka_run_group_tests(all_tests, NULL, NULL);
}