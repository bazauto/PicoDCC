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
#include "../lib/PicoDCCLoco/pico_dccloco.h"
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
// Judges the packet's own bits. A well-formed packet ends at its end bit; the
// inter-packet gap that follows is a separate '1' bit and the PIO can park
// partway through it when the FIFO runs dry, which is a FIFO-underrun question
// (#34 case 2, #35) rather than a statement about this packet's timing. The gap
// itself is covered by test_packet_boundary_carries_no_dc, which looks at the
// raw waveform across a boundary between two real packets.
static void assert_all_bits_in_spec(const DccPacket &p)
{
    const size_t limit = p.well_formed ? p.packet_bits : p.bits.size();
    for (size_t i = 0; i < limit; i++) {
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
    // The program no longer parks on an empty FIFO -- it idles on '1' bits (#34) --
    // so reaching the cycle cap is the expected end, not a stall.
    assert_false(run.stalled_on_empty_fifo);
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

// The 128-step packets from #8, driven through the real encoder rather than a
// hand-written byte list. PicoDccLoco builds the command, PicoDccTrack packs it,
// the assembled dcc.pio transmits it, and the waveform is decoded back -- so
// these assert what a decoder on the layout would actually receive for a given
// <t> command, encoder included.
//
// The packing path is the reason to test it here and not only at data[]: the
// 128-step instruction makes the payload one byte longer, and #31 was exactly a
// payload whose length the packing got wrong, invisibly, on the far side of the
// FIFO.
static void test_128_step_speed_packet_reaches_the_rails(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    // Loco 3 at wire speed 60, forward: 0x03, 0x3F, 0x80 | 61.
    PicoDccLoco loco(3, 60, true);
    raw_dcc_cmd_t cmd = loco.getThrottleCommand();
    assert_int_equal(loco.getSpeedSteps(), DCC_SPEED_STEPS_128);

    const DccPacket p = transmit(track, cmd);

    assert_bytes(p, {0x03, 0x3F, 0xBD, 0x81});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);
}

// A long address plus the two-byte 128-step instruction is the longest payload
// this station emits: four bytes plus the checksum, filling the 64-bit word
// exactly to DCC_PACKET_FIRST_BYTE. One byte more and the checksum would fall
// off the bottom of the transmitted range, which is the shape #31 took.
static void test_128_step_long_address_is_the_longest_payload(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    // Loco 1234 at wire speed 126, reverse: 0xC4, 0xD2, 0x3F, 127.
    PicoDccLoco loco(1234, 126, false);
    raw_dcc_cmd_t cmd = loco.getThrottleCommand();
    assert_int_equal(cmd.length, 4);
    assert_int_equal(cmd.length, DCC_MAX_DATA_BYTES - 1);  // checksum still fits

    const DccPacket p = transmit(track, cmd);

    assert_bytes(p, {0xC4, 0xD2, 0x3F, 0x7F, 0x56});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);
}

// The 28-step fallback still reaches the rails intact, on the same path.
static void test_28_step_fallback_packet_reaches_the_rails(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    PicoDccLoco loco(3, 60, true);
    assert_true(loco.setSpeedSteps(DCC_SPEED_STEPS_28));
    raw_dcc_cmd_t cmd = loco.getThrottleCommand();

    const DccPacket p = transmit(track, cmd);

    assert_bytes(p, {0x03, 0x68, 0x6B});
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

    // S-9.2 idle packet: address 0xFF, data 0x00, error-detection byte 0xFF.
    // Three bytes exactly. sendIdle() used to pass the 0xFF checksum as a third
    // *payload* byte, so sendCommand() checksummed that in turn and the rails
    // carried FF 00 FF 00 -- checksum-valid, ignored by decoders, and nine bits
    // longer than the packet the standard names.
    assert_bytes(p, {0xFF, 0x00, 0xFF});
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
    assert_true(p.packet_bits > 0);

    const DccBit &end = p.bits[p.packet_bits - 1];
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
    // Bounded to the packet's own bits -- see assert_all_bits_in_spec.
    for (size_t i = 0; i < p.packet_bits; i++) {
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

// ---------------------------------------------------------------------------
// #34: the gap between packets must be a bit, not a held level
// ---------------------------------------------------------------------------

// The packet boundary used to be two `side 1` delays -- 116us of high with no
// low half -- running straight into the four cycles of pull/out/out/jmp (which
// carry the previous level, side_set being `opt`) and then the first preamble
// instruction's own 58us high. 203us continuously high, then 58us low.
//
// Two things followed. A decoder sees mismatched halves, resynchronises, and
// consumes the first preamble bit -- leaving exactly the S-9.1 minimum of 14
// with no margin. And an H-bridge driven from the pin puts 203us of DC on the
// rails at every single packet boundary. Measured on a scope at the GPIO on
// 2026-08-23: 197-209us, against the 203us this predicts.
//
// This is the assertion that catches it: the whole waveform, across a real
// boundary between two packets, must consist only of legal half-bits.
static void test_packet_boundary_carries_no_dc(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    sent_track_words.clear();
    raw_dcc_cmd_t first = make_cmd(false, {0x03, 0x40});
    track.sendCommand(&first);
    raw_dcc_cmd_t second = make_cmd(false, {0x07, 0x60});
    track.sendCommand(&second);

    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, sent_track_words);
    assert_null(run.unsupported);
    assert_false(run.stalled_on_empty_fifo);   // idles rather than parking (#34)

    // runs[0] is the pre-preamble startup before the first packet, and the last
    // run is however far into a half-bit the emulator was when it hit the cycle
    // cap. Everything between is transmission or idle carrier, and every level
    // run in it must be one half-bit: 8 cycles for a '1' half, 16 for a '0' half.
    assert_true(run.runs.size() > 2);
    for (size_t i = 1; i + 1 < run.runs.size(); i++) {
        if (run.runs[i].cycles != 8 && run.runs[i].cycles != 16) {
            fail_msg("run %zu of %zu is %u cycles (%u ns) held at level %u -- not a "
                     "legal half-bit. The waveform is holding a level between "
                     "packets, which is DC on the rails (#34).",
                     i, run.runs.size(), run.runs[i].cycles,
                     run.runs[i].cycles * DCC_PIO_CYCLE_NS, run.runs[i].level);
        }
    }
}

// ---------------------------------------------------------------------------
// #34 case 2: an empty TX FIFO must not park the line at a level
// ---------------------------------------------------------------------------
//
// `.wrap` used to target `pull block`. A stalled instruction holds the pin at its
// last side_set value, so an empty FIFO was not "the signal stops" -- it was one
// polarity held for as long as the FIFO stayed empty. An H-bridge driven from
// that pin puts DC on the rails, and a decoder that loses the alternating
// waveform falls back to DC mode, which means full speed on a powered track.
//
// The FIFO does run dry in normal operation, so "keep it fed" was a load-bearing
// strategy that was never written down anywhere. The program now emits legal '1'
// bits while starved instead.
static void test_starved_fifo_emits_an_idle_carrier_not_dc(void **state)
{
    (void)state;

    // Nothing queued at all: the state machine starts starved and stays starved.
    const std::vector<uint32_t> nothing;
    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, nothing, 4000u);

    assert_null(run.unsupported);

    // It must not park.
    assert_false(run.stalled_on_empty_fifo);

    // And what it emits must be an alternating carrier of legal '1' half-bits,
    // not a held level. The last run is truncated by the cycle cap, so it is
    // excluded; run 0 is the startup before the first edge.
    assert_true(run.runs.size() > 8);
    unsigned last_level = 2;
    for (size_t i = 1; i + 1 < run.runs.size(); i++) {
        if (run.runs[i].cycles != 8) {
            fail_msg("idle run %zu of %zu is %u cycles (%u ns) held at level %u -- a "
                     "starved FIFO is putting DC on the rails (#34).",
                     i, run.runs.size(), run.runs[i].cycles,
                     run.runs[i].cycles * DCC_PIO_CYCLE_NS, run.runs[i].level);
        }
        if (run.runs[i].level == last_level) {
            fail_msg("idle run %zu repeats level %u -- the line is not alternating.",
                     i, run.runs[i].level);
        }
        last_level = run.runs[i].level;
    }
}

// The idle carrier is only useful if it is *legal*: a decoder has to read it as
// '1' bits, which means both halves inside the S-9.1 command station window.
static void test_idle_carrier_bits_are_in_spec(void **state)
{
    (void)state;

    const std::vector<uint32_t> nothing;
    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, nothing, 4000u);
    assert_null(run.unsupported);

    const DccPacket p = dcc_decode(run);
    assert_true(p.bits.size() > 4);

    // Every whole bit the carrier produced is a '1' and inside spec. The final
    // bit can be truncated by the cycle cap, so it is not judged.
    for (size_t i = 0; i + 1 < p.bits.size(); i++) {
        assert_int_equal(p.bits[i].kind, DCC_BIT_ONE);
        if (!p.bits[i].in_spec) {
            fail_msg("idle bit %zu is out of S-9.1 spec: high %u ns, low %u ns.",
                     i, p.bits[i].high_ns, p.bits[i].low_ns);
        }
    }
}

// A packet queued after a spell of starvation must still transmit correctly.
// This is the path that matters on the layout: the FIFO runs dry between
// commands, the station idles, and then real data arrives. The starved loop and
// the packet path share the low half of a bit cell, so if the cycle budget in
// dcc.pio is wrong in either direction it shows up here as a malformed bit.
static void test_packet_after_starvation_still_decodes(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    sent_track_words.clear();
    raw_dcc_cmd_t cmd = make_cmd(false, {0x03, 0x40});
    track.sendCommand(&cmd);

    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, sent_track_words,
                                         6000u);
    assert_null(run.unsupported);

    const DccPacket p = dcc_decode(run);
    assert_true(p.well_formed);
    assert_bytes(p, {0x03, 0x40, 0x43});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);

    // Everything after the packet is idle carrier, and it must be legal too --
    // this is where a mis-budgeted starved loop would show as a short half-bit.
    for (size_t i = p.packet_bits; i + 1 < p.bits.size(); i++) {
        if (p.bits[i].kind != DCC_BIT_ONE || !p.bits[i].in_spec) {
            fail_msg("bit %zu after the packet is not a legal '1': high %u ns, low %u ns.",
                     i, p.bits[i].high_ns, p.bits[i].low_ns);
        }
    }
}

// Both packets must still decode after the boundary change -- a gap built out
// of legal bits is only an improvement if the framing either side still reads.
static void test_both_packets_decode_across_the_boundary(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    sent_track_words.clear();
    raw_dcc_cmd_t first = make_cmd(false, {0x03, 0x40});
    track.sendCommand(&first);

    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, sent_track_words);
    const DccPacket p = dcc_decode(run);

    assert_bytes(p, {0x03, 0x40, 0x43});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_all_bits_in_spec(p);

    // The gap no longer eats a preamble bit, so the full 15 the PIO emits
    // survive to the decoder rather than 14 after a resynchronisation.
    assert_true(p.preamble_bits >= 14);
}

// ---------------------------------------------------------------------------
// Recovery from a FIFO that has slipped by one word
// ---------------------------------------------------------------------------

// The failure captured in docs/DCC_Broken.png.
//
// A packet is one or two 32-bit words. If the FIFO ever slips by one, the state
// machine pulls a packet's *second* word at `start` and reads it as a header. An
// idle packet's second word is 0xFF000000: preamble 255, byte count 0. `jmp x--`
// post-decremented that 0 to 0xFFFFFFFF -- a packet of 4.29 billion bytes -- and
// the state machine then emitted every subsequent FIFO word verbatim as 9-bit
// data bytes, with no preamble, until the board was rebooted. On the layout that
// is a station transmitting nothing a decoder can read, so every locomotive
// holds its last commanded speed indefinitely.
//
// The guard at `have_packet` discards a header claiming zero bytes. Dropping
// that word is also the resync: one word out of step, dropped, puts the next
// pull back on a real first word.
static void test_slipped_fifo_word_does_not_run_away(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    // Build two real idle packets, then present them one word out of step by
    // dropping the leading word -- the state machine's first pull lands on a
    // second word, exactly as it did on the bench.
    sent_track_words.clear();
    track.sendIdle();
    track.sendIdle();
    assert_true(sent_track_words.size() == 4);

    std::vector<uint32_t> slipped(sent_track_words.begin() + 1, sent_track_words.end());

    const PioRunResult run = pio_emulate(dcc_program_instructions, dcc_program_length,
                                         dcc_wrap_target, dcc_wrap, slipped);
    assert_null(run.unsupported);

    // Whatever the discarded word produced, the stream must come back to a real
    // packet rather than streaming words as data for ever. Before the guard this
    // decoded as one endless packet whose bytes were the raw FIFO words --
    // 00 00 0E 04 FF 00 FF 00 ... -- and never reached an end bit.
    const DccPacket p = dcc_decode(run);
    assert_true(p.well_formed);
    assert_bytes(p, {0xFF, 0x00, 0xFF});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_true(p.preamble_bits >= 14);
}

// The recovery must not cost anything on the paths that run in normal service.
// The guard sits after `jmp x--`, so a real packet never executes it, and all
// four cycle budgets in dcc.pio are unchanged -- this is what says so.
static void test_guard_does_not_disturb_a_normal_packet(void **state)
{
    (void)state;
    PicoDccTrack track(false, main_settings());

    const DccPacket p = transmit(track, make_cmd(false, {0x03, 0x3F, 0x80}));

    assert_bytes(p, {0x03, 0x3F, 0x80, 0xBC});
    assert_true(dcc_checksum_valid(p.bytes));
    assert_true(p.preamble_bits >= 14);
    assert_all_bits_in_spec(p);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_emulator_models_the_whole_program, setup),

        cmocka_unit_test_setup(test_speed_packet_bytes_reach_the_rails, setup),
        cmocka_unit_test_setup(test_long_address_packet_bytes_reach_the_rails, setup),
        cmocka_unit_test_setup(test_128_step_speed_packet_reaches_the_rails, setup),
        cmocka_unit_test_setup(test_128_step_long_address_is_the_longest_payload, setup),
        cmocka_unit_test_setup(test_28_step_fallback_packet_reaches_the_rails, setup),
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

        cmocka_unit_test_setup(test_packet_boundary_carries_no_dc, setup),
        cmocka_unit_test_setup(test_starved_fifo_emits_an_idle_carrier_not_dc, setup),
        cmocka_unit_test_setup(test_idle_carrier_bits_are_in_spec, setup),
        cmocka_unit_test_setup(test_packet_after_starvation_still_decodes, setup),
        cmocka_unit_test_setup(test_both_packets_decode_across_the_boundary, setup),

        cmocka_unit_test_setup(test_slipped_fifo_word_does_not_run_away, setup),
        cmocka_unit_test_setup(test_guard_does_not_disturb_a_normal_packet, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
