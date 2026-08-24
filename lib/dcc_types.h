#ifndef PICO_DCC_TYPES_H
#define PICO_DCC_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Payload bytes a command may carry, excluding the checksum the station appends.
#define DCC_MAX_DATA_BYTES 5

// Where the payload starts inside the 64-bit word handed to the PIO.
//
//   byte  7        6       5    4    3    2    1    0
//       [preamble][len+1][ d0 ][ d1 ][ d2 ][ d3 ][ d4 ][xor]
//
// The two header bytes occupy 7 and 6, so the payload begins at byte 5 and the
// checksum follows the last payload byte. This is a property of that layout, not
// of how data[] is sized -- conflating the two is what caused #31, where changing
// DCC_MAX_DATA_BYTES from 6 to 5 silently shifted every packet one byte down,
// prepending a phantom 0x00 and pushing the checksum out of the transmitted range.
#define DCC_PACKET_FIRST_BYTE 5

// Payload plus checksum must fit between the header and the bottom of the word.
static_assert(DCC_MAX_DATA_BYTES <= DCC_PACKET_FIRST_BYTE,
              "payload plus checksum overflows the 64-bit PIO packet word");

// DCC address and throttle-speed limits. These used to be duplicated (and, in
// one case, missing entirely) across PicoDCCLoco, PicoDCCLocos and
// PicoDCCTrack; they live here once so every component checks the same
// numbers (#11, #12, #16).
#define DCC_MIN_LOCO_ADDR 1
#define DCC_MAX_LOCO_ADDR 10239   // top of the 14-bit long-address space; first packet byte 0xE7
#define HIGHEST_SHORT_ADDR 127    // short/long address boundary
#define INVALID_LOCO_ADDR 65535   // sentinel: no such loco / not yet assigned
#define DCC_MAX_THROTTLE_SPEED 126  // highest speed value a DCC-EX host sends
#define DCC_SPEED_ESTOP 255         // internal sentinel: never a wire value

static_assert(DCC_SPEED_ESTOP > DCC_MAX_THROTTLE_SPEED,
              "estop sentinel collides with a legal speed");

// Speed step modes (#8). The value *is* the step count, so the wire form of
// <D SPEED28> / <D SPEED128> and the internal representation are the same
// number and cannot drift apart.
//
// 128 is the default: every decoder on Westgate Hollow supports it, and the
// 28-step conversion loses roughly one distinct speed per 4.6 units of the
// 0..126 value a host sends, which is coarser than the orchestrator's braking
// model assumes. 28 is retained per loco for any older decoder.
#define DCC_SPEED_STEPS_28 28
#define DCC_SPEED_STEPS_128 128
#define DCC_DEFAULT_SPEED_STEPS DCC_SPEED_STEPS_128

static inline bool dcc_is_valid_speed_step_mode(int steps)
{
    return steps == DCC_SPEED_STEPS_28 || steps == DCC_SPEED_STEPS_128;
}

// 1..10239: the DCC address range this firmware accepts. Address 0 is the
// broadcast address and must be rejected, not silently retargeted (#12);
// anything above the 14-bit long-address space must be rejected rather than
// emitting an idle/reserved packet (#16).
static inline bool dcc_is_valid_loco_address(int addr)
{
    return addr >= DCC_MIN_LOCO_ADDR && addr <= DCC_MAX_LOCO_ADDR;
}

// -1 (emergency stop), or 0..126: the full range a DCC-EX throttle sends.
// 127 and above are rejected outright rather than being masked into range (#11).
static inline bool dcc_is_valid_throttle_speed(int speed)
{
    return speed == -1 || (speed >= 0 && speed <= DCC_MAX_THROTTLE_SPEED);
}

// Only meaningful for a speed that has already passed
// dcc_is_valid_throttle_speed(): -1 becomes the DCC_SPEED_ESTOP sentinel,
// anything else is returned unchanged.
static inline uint8_t dcc_speed_code(int speed)
{
    return speed == -1 ? DCC_SPEED_ESTOP : (uint8_t)speed;
}

typedef struct
{
    bool is_prog;
    uint8_t length;
    uint8_t data[DCC_MAX_DATA_BYTES];
    uint64_t cmd_data;
    uint8_t repeats;
} raw_dcc_cmd_t;

#endif // PICO_DCC_TYPES_H