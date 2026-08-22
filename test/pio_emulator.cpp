#include "pio_emulator.h"

#include <deque>

namespace {

// ---------------------------------------------------------------------------
// Instruction encoding (RP2040/RP2350 PIO, datasheet section 3.4)
//
//   15:13  opcode
//   12:8   delay / side-set. dcc.pio declares `.side_set 1 opt`, so this field
//          is [4] side-set enable, [3] side-set value, [2:0] delay.
// ---------------------------------------------------------------------------

constexpr unsigned OP_JMP = 0, OP_WAIT = 1, OP_IN = 2, OP_OUT = 3;
constexpr unsigned OP_PUSHPULL = 4, OP_MOV = 5, OP_IRQ = 6, OP_SET = 7;

// JMP conditions
constexpr unsigned JMP_ALWAYS = 0, JMP_NOT_X = 1, JMP_X_DEC = 2;
constexpr unsigned JMP_NOT_Y = 3, JMP_Y_DEC = 4, JMP_X_NE_Y = 5;

// OUT / MOV / SET destinations and MOV sources
constexpr unsigned DST_X = 1, DST_Y = 2, DST_ISR = 6;
constexpr unsigned SRC_X = 1, SRC_Y = 2, SRC_ISR = 6;

struct Decoded {
    unsigned opcode;
    bool     side_set_enabled;
    unsigned side_set_value;
    unsigned delay;
    unsigned field;   // bits 7:0, meaning depends on opcode
};

Decoded decode(uint16_t instr)
{
    Decoded d;
    d.opcode = (instr >> 13) & 0x7;

    const unsigned ds = (instr >> 8) & 0x1F;
    d.side_set_enabled = (ds >> 4) & 0x1;
    d.side_set_value   = (ds >> 3) & 0x1;
    d.delay            = ds & 0x7;

    d.field = instr & 0xFF;
    return d;
}

} // namespace

PioRunResult pio_emulate(const uint16_t *program,
                         size_t          program_len,
                         unsigned        wrap_target,
                         unsigned        wrap,
                         const std::vector<uint32_t> &words,
                         uint32_t        max_cycles)
{
    PioRunResult out;
    out.stalled_on_empty_fifo = false;
    out.level_at_stall = 0;
    out.total_cycles = 0;
    out.unsupported = nullptr;

    std::deque<uint32_t> fifo(words.begin(), words.end());

    unsigned pc = wrap_target;
    uint32_t x = 0, y = 0, isr = 0, osr = 0;
    unsigned osr_count = 32;   // shift counter; 32 means "empty" against a 32-bit threshold
    unsigned pin = 0;

    auto emit = [&out](unsigned level, uint32_t cycles) {
        if (!out.runs.empty() && out.runs.back().level == level) {
            out.runs.back().cycles += cycles;
        } else {
            out.runs.push_back({level, cycles});
        }
        out.total_cycles += cycles;
    };

    while (out.total_cycles < max_cycles) {
        if (pc >= program_len) { out.unsupported = "pc ran past the end of the program"; break; }

        const Decoded d = decode(program[pc]);

        // A stalling instruction is re-issued every cycle. Side-set is applied on
        // every issue, including a stalling one, but the delay cycles are not
        // executed until the instruction actually completes.
        const bool is_pull = (d.opcode == OP_PUSHPULL) && ((d.field >> 7) & 0x1);
        if (is_pull) {
            const bool if_empty = (d.field >> 6) & 0x1;
            const bool block    = (d.field >> 5) & 0x1;
            const bool want     = !if_empty || (osr_count >= 32);

            if (want && fifo.empty()) {
                if (block) {
                    // This is where the program parks between packets. Record the
                    // level it parks at -- an idle DCC line held at one level is DC
                    // on the rails, which is the whole point of #34.
                    if (d.side_set_enabled) pin = d.side_set_value;
                    out.stalled_on_empty_fifo = true;
                    out.level_at_stall = pin;
                    break;
                }
                // Non-blocking pull on an empty FIFO copies X into the OSR.
                osr = x;
                osr_count = 0;
            }
        }

        if (d.side_set_enabled) pin = d.side_set_value;

        unsigned next_pc = pc + 1;
        bool     jumped  = false;

        switch (d.opcode) {
        case OP_JMP: {
            const unsigned cond = (d.field >> 5) & 0x7;
            const unsigned addr = d.field & 0x1F;
            bool take = false;
            switch (cond) {
            case JMP_ALWAYS: take = true;                       break;
            case JMP_NOT_X:  take = (x == 0);                    break;
            case JMP_X_DEC:  take = (x != 0); x--;               break;
            case JMP_NOT_Y:  take = (y == 0);                    break;
            case JMP_Y_DEC:  take = (y != 0); y--;               break;
            case JMP_X_NE_Y: take = (x != y);                    break;
            default: out.unsupported = "jmp condition not modelled (PIN or !OSRE)"; break;
            }
            if (out.unsupported) break;
            if (take) { next_pc = addr; jumped = true; }
            break;
        }

        case OP_OUT: {
            const unsigned dst   = (d.field >> 5) & 0x7;
            unsigned       count = d.field & 0x1F;
            if (count == 0) count = 32;

            // Shifting left: OUT takes from the MSB end. dcc_program_init()
            // configures sm_config_set_out_shift(&c, false, ...) -- false is
            // shift_right, so this is MSB first, which is what DCC needs.
            const uint32_t value = (count == 32) ? osr : (osr >> (32 - count));
            osr = (count == 32) ? 0 : (uint32_t)(osr << count);
            osr_count += count;
            if (osr_count > 32) osr_count = 32;

            switch (dst) {
            case DST_X: x = value; break;
            case DST_Y: y = value; break;
            default: out.unsupported = "out destination not modelled"; break;
            }
            break;
        }

        case OP_PUSHPULL: {
            if (!is_pull) { out.unsupported = "push not modelled"; break; }
            const bool if_empty = (d.field >> 6) & 0x1;
            const bool want     = !if_empty || (osr_count >= 32);
            if (want && !fifo.empty()) {
                osr = fifo.front();
                fifo.pop_front();
                osr_count = 0;
            }
            break;
        }

        case OP_MOV: {
            const unsigned dst = (d.field >> 5) & 0x7;
            const unsigned op  = (d.field >> 3) & 0x3;
            const unsigned src = d.field & 0x7;
            if (op != 0) { out.unsupported = "mov with invert/bit-reverse not modelled"; break; }

            uint32_t value = 0;
            switch (src) {
            case SRC_X:   value = x;   break;
            case SRC_Y:   value = y;   break;
            case SRC_ISR: value = isr; break;
            default: out.unsupported = "mov source not modelled"; break;
            }
            if (out.unsupported) break;

            switch (dst) {
            case DST_X:   x   = value; break;
            case DST_Y:   y   = value; break;  // `nop` assembles to mov y, y
            case DST_ISR: isr = value; break;
            default: out.unsupported = "mov destination not modelled"; break;
            }
            break;
        }

        case OP_SET: {
            const unsigned dst  = (d.field >> 5) & 0x7;
            const unsigned data = d.field & 0x1F;
            switch (dst) {
            case DST_X: x = data; break;
            case DST_Y: y = data; break;
            default: out.unsupported = "set destination not modelled"; break;
            }
            break;
        }

        case OP_WAIT: out.unsupported = "wait not modelled"; break;
        case OP_IN:   out.unsupported = "in not modelled";   break;
        case OP_IRQ:  out.unsupported = "irq not modelled";  break;
        default:      out.unsupported = "unknown opcode";    break;
        }

        if (out.unsupported) break;

        emit(pin, 1 + d.delay);

        if (!jumped && pc == wrap) next_pc = wrap_target;
        pc = next_pc;
    }

    return out;
}

// ---------------------------------------------------------------------------
// DCC decoding
// ---------------------------------------------------------------------------

namespace {

// Shape only: what a decoder watching the rails would make of this bit.
DccBitKind classify_shape(uint32_t high_ns, uint32_t low_ns)
{
    const bool high_short = high_ns < DCC_HALF_SHAPE_THRESHOLD_NS;
    const bool low_short  = low_ns  < DCC_HALF_SHAPE_THRESHOLD_NS;

    if (high_short && low_short)   return DCC_BIT_ONE;
    if (!high_short && !low_short) return DCC_BIT_ZERO;
    return DCC_BIT_INVALID;   // halves disagree, so not a bit at all
}

// Strict S-9.1 command station windows for the shape already decided.
bool half_cycles_in_spec(DccBitKind kind, uint32_t high_ns, uint32_t low_ns)
{
    if (kind == DCC_BIT_ONE) {
        return high_ns >= DCC_ONE_HALF_MIN_NS && high_ns <= DCC_ONE_HALF_MAX_NS
            && low_ns  >= DCC_ONE_HALF_MIN_NS && low_ns  <= DCC_ONE_HALF_MAX_NS;
    }
    if (kind == DCC_BIT_ZERO) {
        return high_ns >= DCC_ZERO_HALF_MIN_NS && high_ns <= DCC_ZERO_HALF_MAX_NS
            && low_ns  >= DCC_ZERO_HALF_MIN_NS && low_ns  <= DCC_ZERO_HALF_MAX_NS;
    }
    return false;
}

} // namespace

DccPacket dcc_decode(const PioRunResult &result)
{
    DccPacket packet;
    packet.preamble_bits = 0;
    packet.well_formed = false;
    packet.error = nullptr;

    // Pair the runs into bits. The waveform always starts high, so a leading low
    // run (there is none in practice, but be explicit) is skipped.
    size_t i = 0;
    while (i + 1 < result.runs.size()) {
        if (result.runs[i].level != 1) { i++; continue; }
        const uint32_t high_ns = result.runs[i].cycles     * DCC_PIO_CYCLE_NS;
        const uint32_t low_ns  = result.runs[i + 1].cycles * DCC_PIO_CYCLE_NS;
        const DccBitKind kind  = classify_shape(high_ns, low_ns);
        packet.bits.push_back({kind, half_cycles_in_spec(kind, high_ns, low_ns),
                               high_ns, low_ns});
        i += 2;
    }

    size_t b = 0;
    while (b < packet.bits.size() && packet.bits[b].kind == DCC_BIT_ONE) b++;
    packet.preamble_bits = (unsigned)b;

    if (b == 0) { packet.error = "no preamble"; return packet; }

    // Each byte is a 0 separator followed by 8 data bits, MSB first. A 1 where a
    // separator is expected is the packet end bit.
    while (b < packet.bits.size()) {
        if (packet.bits[b].kind == DCC_BIT_ONE) {
            packet.well_formed = true;   // packet end bit
            return packet;
        }
        if (packet.bits[b].kind != DCC_BIT_ZERO) {
            packet.error = "invalid bit where a byte separator was expected";
            return packet;
        }
        b++;

        if (b + 8 > packet.bits.size()) { packet.error = "packet truncated mid-byte"; return packet; }

        uint8_t value = 0;
        for (unsigned k = 0; k < 8; k++, b++) {
            if (packet.bits[b].kind == DCC_BIT_INVALID) {
                packet.error = "invalid bit inside a data byte";
                return packet;
            }
            value = (uint8_t)((value << 1) | (packet.bits[b].kind == DCC_BIT_ONE ? 1u : 0u));
        }
        packet.bytes.push_back(value);
    }

    packet.error = "ran out of bits before the packet end bit";
    return packet;
}

bool dcc_checksum_valid(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() < 2) return false;
    uint8_t x = 0;
    for (size_t i = 0; i + 1 < bytes.size(); i++) x ^= bytes[i];
    return x == bytes.back();
}
