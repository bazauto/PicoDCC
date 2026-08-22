// End-to-end wire-format tests: real packing -> real PIO program -> rails.
//
// Every other suite stops at raw_dcc_cmd_t.data[] or at cmd_data. Those are the
// values on *this* side of the PIO, and #31 lived on the other side: correct
// data[] packed into the 64-bit word one byte low, so the program transmitted a
// phantom 0x00 and never reached the checksum. Nothing could see it.
//
// These tests drive PicoDccTrack::sendCommand() for real, take the words it
// pushed to the FIFO out of the mock, run them through an emulator executing the
// *assembled* dcc.pio (test/dcc_pio_program.hex, checked against pioasm at
// configure time), and decode the resulting waveform back into bits and bytes.
//
// So an assertion here is a statement about what a decoder on the layout would
// actually receive. Unlike pico_dcc_wire_format_tests.cpp, these assert what is
// *correct* rather than what is current -- they were written against the
// unfixed code and observed to fail before #31 and #33 were fixed.

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

extern "C" {
#include <cmocka.h>
}

#include "../lib/PicoDCCTrack/pico_dcctrack.h"
#include "pio_emulator.h"
#include "dcc_pio_program.h"

extern std::vector<uint32_t> sent_track_words;
extern std::vector<uint64_t> sent_track_packets;
extern uint32_t mock_time_ms;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static track_settings_t main_settings()
{
    track_settings_t s;
    s.signal_pin = 17;
    s.ctrl_pin   = 18;
    s.adc_num    = 0;
    s.short_pin  = 16;
    return s;
}

static track_settings_t prog_settings()
{
    track_settings_t s;
    s.signal_pin = 20;
    s.ctrl_pin   = 21;
    s.adc_num    = 1;
    s.short_pin  = 19;
    return s;
}

// Push a command through the real sendCommand() and decode what reaches the rails.
static DccPacket transmit(PicoDccTrack &track, raw_dcc_cmd_t cmd)
{
    sent_track_words.clear();
    track.sendCommand(&cmd);

    const PioRunResult run = pio_emulate(dcc_program_instructions,
                                         dcc_program_length,
                                         dcc_wrap_target,
                                         dcc_wrap,
                                         sent_track_words);

    // The emulator refuses to guess. If dcc.pio grows an instruction it does not
    // model, that surfaces here rather than as a quietly wrong waveform.
    if (run.unsupported != nullptr) {
        fail_msg("PIO emulator: %s", run.unsupported);
    }
    return dcc_decode(run);
}

static raw_dcc_cmd_t make_cmd(bool is_prog, std::vector<uint8_t> data)
{
    raw_dcc_cmd_t cmd = {};
    cmd.is_prog = is_prog;
    cmd.length  = (uint8_t)data.size();
    for (size_t i = 0; i < data.size(); i++) cmd.data[i] = data[i];
    cmd.cmd_data = 0;
    cmd.repeats  = 0;
    return cmd;
}

static void assert_bytes(const DccPacket &p, std::vector<uint8_t> expected)
{
    assert_true(p.well_formed);
    assert_int_equal(p.bytes.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        if (p.bytes[i] != expected[i]) {
            fail_msg("byte %zu on the rails is 0x%02X, expected 0x%02X",
                     i, p.bytes[i], expected[i]);
        }
    }
}

// Every half cycle must sit inside the S-9.1 command station window for the bit
// it claims to be. This is what catches a timing regression in dcc.pio.
static void assert_all_bits_in_spec(const DccPacket &p)
{
    for (size_t i = 0; i < p.bits.size(); i++) {
        if (p.bits[i].kind == DCC_BIT_INVALID) {
            fail_msg("bit %zu of %zu is not a bit at all: %u ns high / %u ns low, "
                     "halves disagree on shape",
                     i, p.bits.size(), p.bits[i].high_ns, p.bits[i].low_ns);
        }
        if (!p.bits[i].in_spec) {
            const bool one = (p.bits[i].kind == DCC_BIT_ONE);
            fail_msg("bit %zu of %zu is a '%s' outside the S-9.1 command station "
                     "window: %u ns high / %u ns low, both halves must be %u-%u ns",
                     i, p.bits.size(), one ? "1" : "0",
                     p.bits[i].high_ns, p.bits[i].low_ns,
                     one ? DCC_ONE_HALF_MIN_NS  : DCC_ZERO_HALF_MIN_NS,
                     one ? DCC_ONE_HALF_MAX_NS  : DCC_ZERO_HALF_MAX_NS);
        }
    }
}

static int setup(void **state)
{
    (void)state;
    sent_track_words.clear();
    sent_track_packets.clear();
    mock_reset_pio();
    mock_adc_clear_channels();
    mock_time_ms = 1000;
    return 0;
}

// ---------------------------------------------------------------------------
// The emulator itself
// ---------------------------------------------------------------------------

// If this fails, every other test in the file is meaningless.
static void test_emulator_models_the_whole_program(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    sent_track_words.clear();
    raw_dcc_cmd_t cmd = make_cmd(false, {0x03, 0x40});
    track.sendCommand(&cmd);

    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, sent_track_words);
    assert_null(run.unsupported);
    assert_true(run.stalled_on_empty_fifo);   // ran a whole packet and parked
    assert_true(run.total_cycles > 0);
}

// ---------------------------------------------------------------------------
// Packet content
// ---------------------------------------------------------------------------

static void test_speed_packet_bytes_reach_the_rails(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    // Loco 3, 28-step speed instruction. Checksum is 0x03 ^ 0x40.
    const DccPacket p = transmit(track, make_cmd(false, {0x03, 0x40}));

    assert_bytes(p, {0x03, 0x40, 0x43});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);
}

static void test_long_address_packet_bytes_reach_the_rails(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    // Loco 1234: two address bytes plus an instruction byte.
    const DccPacket p = transmit(track, make_cmd(false, {0xC4, 0xD2, 0x7F}));

    assert_bytes(p, {0xC4, 0xD2, 0x7F, 0x69});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);
}

static void test_emergency_stop_broadcast_reaches_the_rails(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    // The broadcast emergency stop PicoDccController builds.
    const DccPacket p = transmit(track, make_cmd(false, {0x00, 0x41}));

    assert_bytes(p, {0x00, 0x41, 0x41});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);
}

static void test_idle_packet_reaches_the_rails(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    sent_track_words.clear();
    track.sendIdle();

    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, sent_track_words);
    assert_null(run.unsupported);
    const DccPacket p = dcc_decode(run);

    // S-9.2 idle packet: 0xFF 0x00, checksum 0xFF. sendIdle() supplies the
    // checksum byte as data, so the station appends its XOR (0x00) as well.
    assert_bytes(p, {0xFF, 0x00, 0xFF, 0x00});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);
}

// The checksum must be computed over what is actually transmitted. #31 dropped
// it entirely, which this catches independently of the byte assertions above.
static void test_checksum_is_transmitted_and_correct(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    const std::vector<std::vector<uint8_t>> cases = {
        {0x03, 0x40},
        {0x00, 0x41},
        {0xC4, 0xD2, 0x7F},
        {0x81, 0xF8},
    };

    for (size_t i = 0; i < cases.size(); i++) {
        const DccPacket p = transmit(track, make_cmd(false, cases[i]));
        assert_true(p.well_formed);
        // payload + one checksum byte
        assert_int_equal(p.bytes.size(), cases[i].size() + 1);
        if (!dcc_checksum_valid(p.bytes)) {
            fail_msg("case %zu: checksum on the rails is 0x%02X and does not match "
                     "the %zu bytes before it", i, p.bytes.back(), p.bytes.size() - 1);
        }
    }
}

// No packet may begin with a byte the caller did not ask for. This is the
// specific shape of #31 -- a phantom 0x00 ahead of the real payload.
static void test_no_phantom_leading_byte(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    const DccPacket p = transmit(track, make_cmd(false, {0x03, 0x40}));
    assert_true(p.well_formed);
    assert_true(p.bytes.size() >= 1);
    if (p.bytes[0] != 0x03) {
        fail_msg("first byte on the rails is 0x%02X, expected the caller's 0x03 "
                 "(a leading 0x00 here is #31)", p.bytes[0]);
    }
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

static void test_every_bit_is_within_nmra_windows(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());
    const DccPacket p = transmit(track, make_cmd(false, {0xC4, 0xD2, 0x7F}));
    assert_true(p.well_formed);
    assert_all_bits_in_spec(p);
}

// The packet end bit is the last bit before the program wraps, and is the one
// #33 got wrong -- it was 7 cycles high against 8 low.
static void test_packet_end_bit_halves_match(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());
    const DccPacket p = transmit(track, make_cmd(false, {0x03, 0x40}));
    assert_true(p.well_formed);
    assert_true(p.bits.size() > 0);

    const DccBit &end = p.bits.back();
    if (end.kind != DCC_BIT_ONE) {
        fail_msg("packet end bit is not shaped like a '1': %u ns high / %u ns low",
                 end.high_ns, end.low_ns);
    }
    if (!end.in_spec) {
        fail_msg("packet end bit is outside the S-9.1 command station window: "
                 "%u ns high / %u ns low, both halves must be %u-%u ns",
                 end.high_ns, end.low_ns, DCC_ONE_HALF_MIN_NS, DCC_ONE_HALF_MAX_NS);
    }
    // S-9.1 also caps the difference between the two halves of a '1' bit at 3us.
    const uint32_t skew = (end.high_ns > end.low_ns) ? end.high_ns - end.low_ns
                                                     : end.low_ns - end.high_ns;
    if (skew > 3000u) {
        fail_msg("packet end bit halves differ by %u ns (limit 3000 ns): "
                 "%u high / %u low", skew, end.high_ns, end.low_ns);
    }
}

static void test_data_bit_half_cycles_are_exact(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());
    const DccPacket p = transmit(track, make_cmd(false, {0xC4, 0xD2, 0x7F}));
    assert_true(p.well_formed);

    // Nominal DCC: 58us halves for a '1', and dcc.pio uses 116us for a '0'.
    for (size_t i = 0; i < p.bits.size(); i++) {
        const uint32_t want = (p.bits[i].kind == DCC_BIT_ONE) ? 58000u : 116000u;
        if (p.bits[i].kind == DCC_BIT_INVALID) continue;   // reported elsewhere
        if (p.bits[i].high_ns != want || p.bits[i].low_ns != want) {
            fail_msg("bit %zu (%s) is %u/%u ns, expected %u/%u", i,
                     p.bits[i].kind == DCC_BIT_ONE ? "one" : "zero",
                     p.bits[i].high_ns, p.bits[i].low_ns, want, want);
        }
    }
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

static void test_main_track_preamble_meets_minimum(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());
    const DccPacket p = transmit(track, make_cmd(false, {0x03, 0x40}));
    assert_true(p.well_formed);

    // S-9.1 requires at least 14 preamble bits from a command station.
    if (p.preamble_bits < 14) {
        fail_msg("main track sent %u preamble bits, minimum is 14", p.preamble_bits);
    }
}

static void test_prog_track_preamble_meets_service_mode_minimum(void **state)
{
    (void)state;
    PicoDccTrack track(true, prog_settings());
    const DccPacket p = transmit(track, make_cmd(true, {0x74, 0x05}));
    assert_true(p.well_formed);

    // Service mode requires a long preamble; DCC_PROG_PREAMBLE asks for 20.
    if (p.preamble_bits < 20) {
        fail_msg("programming track sent %u preamble bits, minimum is 20", p.preamble_bits);
    }
}

static void test_byte_count_matches_payload_plus_checksum(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    for (uint8_t len = 2; len <= 4; len++) {
        std::vector<uint8_t> data;
        for (uint8_t i = 0; i < len; i++) data.push_back((uint8_t)(0x10 + i));

        const DccPacket p = transmit(track, make_cmd(false, data));
        assert_true(p.well_formed);
        if (p.bytes.size() != (size_t)len + 1) {
            fail_msg("length %u produced %zu bytes on the rails, expected %u "
                     "(payload plus checksum)", len, p.bytes.size(), len + 1);
        }
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_emulator_models_the_whole_program, setup),

        cmocka_unit_test_setup(test_speed_packet_bytes_reach_the_rails, setup),
        cmocka_unit_test_setup(test_long_address_packet_bytes_reach_the_rails, setup),
        cmocka_unit_test_setup(test_emergency_stop_broadcast_reaches_the_rails, setup),
        cmocka_unit_test_setup(test_idle_packet_reaches_the_rails, setup),
        cmocka_unit_test_setup(test_checksum_is_transmitted_and_correct, setup),
        cmocka_unit_test_setup(test_no_phantom_leading_byte, setup),

        cmocka_unit_test_setup(test_every_bit_is_within_nmra_windows, setup),
        cmocka_unit_test_setup(test_packet_end_bit_halves_match, setup),
        cmocka_unit_test_setup(test_data_bit_half_cycles_are_exact, setup),

        cmocka_unit_test_setup(test_main_track_preamble_meets_minimum, setup),
        cmocka_unit_test_setup(test_prog_track_preamble_meets_service_mode_minimum, setup),
        cmocka_unit_test_setup(test_byte_count_matches_payload_plus_checksum, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
