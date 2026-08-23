// A cycle-accurate emulator for the subset of the RP2350 PIO instruction set
// that lib/PicoDCCTrack/dcc.pio uses, plus a decoder that turns the resulting
// pin waveform back into DCC bits and bytes.
//
// The point of this is to close the loop. Every other test asserts on
// raw_dcc_cmd_t.data[] or on cmd_data -- values on *this* side of the PIO. The
// bug in #31 lived in the step after that: the packing was wrong, so correct
// data[] still produced a corrupt waveform, and no test could see it.
//
// This emulator executes the *assembled* program (see dcc_pio_program.hex,
// produced by pioasm from dcc.pio) rather than a hand-written copy of it, so it
// cannot drift from the real thing. Anything the program does that is not
// modelled here is a hard failure, never a silent approximation.
#ifndef PICO_DCC_PIO_EMULATOR_H
#define PICO_DCC_PIO_EMULATOR_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

// dcc.pio drives a 116us bit cell over 16 instruction cycles, so one cycle is
// 7.25us. Held in nanoseconds so every comparison below is exact integer
// arithmetic -- a half cycle is 8 * 7250 = 58000ns with no rounding anywhere.
#define DCC_PIO_CYCLE_NS 7250u

// NMRA S-9.1 command station transmit windows, in nanoseconds.
#define DCC_ONE_HALF_MIN_NS    55000u
#define DCC_ONE_HALF_MAX_NS    61000u
#define DCC_ZERO_HALF_MIN_NS   95000u
#define DCC_ZERO_HALF_MAX_NS 9900000u

// A run of consecutive cycles at one pin level.
struct PioLevelRun {
    unsigned level;    // 0 or 1
    uint32_t cycles;
};

struct PioRunResult {
    std::vector<PioLevelRun> runs;
    bool     stalled_on_empty_fifo;  // stopped at a blocking pull with nothing to pull
    unsigned level_at_stall;         // pin level the program parked at
    uint32_t total_cycles;
    // Set when the program executed something this emulator does not model.
    // Never silently ignored -- the tests assert it is empty.
    const char *unsupported;
};

// Execute an assembled PIO program with `words` preloaded into the TX FIFO.
//
// Assumes the configuration dcc_program_init() applies: one side-set pin with
// `opt` (so the delay field is 3 bits), OUT shifting left (MSB first), autopull
// disabled, pull threshold 32.
//
// Runs until the program stalls at a blocking pull with an empty FIFO, or until
// max_cycles is reached.
PioRunResult pio_emulate(const uint16_t *program,
                         size_t          program_len,
                         unsigned        wrap_target,
                         unsigned        wrap,
                         const std::vector<uint32_t> &words,
                         uint32_t        max_cycles = 200000u);

// ---------------------------------------------------------------------------
// DCC decoding
// ---------------------------------------------------------------------------

// Framing and spec compliance are decided separately, and deliberately so.
//
// `kind` classifies by *shape* -- short halves are a one, long halves are a zero
// -- which is what a decoder on the layout does. `in_spec` is the strict S-9.1
// command station judgement. Keeping them apart matters: a bit can be readable
// but out of spec, and if a single out-of-spec bit aborted decoding then one
// timing defect would mask every content defect behind it. That is exactly what
// #33 did to #31 when these were conflated.
#define DCC_HALF_SHAPE_THRESHOLD_NS 80000u

enum DccBitKind {
    DCC_BIT_ONE,
    DCC_BIT_ZERO,
    DCC_BIT_INVALID   // the two halves disagree on shape
};

struct DccBit {
    DccBitKind kind;
    bool       in_spec;   // both halves inside the S-9.1 window for `kind`
    uint32_t   high_ns;
    uint32_t   low_ns;
};

struct DccPacket {
    // Every bit the waveform contains, including any that follow the packet's
    // end bit. The inter-packet gap is a '1' bit in its own right, and when the
    // FIFO runs dry mid-gap the PIO parks partway through it -- so the tail of
    // `bits` can hold a truncated bit that is not part of this packet and must
    // not be judged as if it were. Use packet_bits to bound anything that asks
    // "is this packet in spec".
    std::vector<DccBit>  bits;
    size_t               packet_bits;   // bits 0..packet_bits-1 are this packet,
                                        // ending with its end bit. 0 unless well_formed.
    unsigned             preamble_bits;
    std::vector<uint8_t> bytes;        // payload as transmitted, including checksum
    bool                 well_formed;  // framing decoded cleanly to a packet end bit
    const char          *error;        // why not, when well_formed is false
};

// Pair the waveform into bits and decode the DCC framing: preamble, then
// (0 separator + 8 bits) per byte, then the packet end bit.
DccPacket dcc_decode(const PioRunResult &result);

// XOR of every byte except the last, which is what the last byte must equal.
bool dcc_checksum_valid(const std::vector<uint8_t> &bytes);

#endif // PICO_DCC_PIO_EMULATOR_H
