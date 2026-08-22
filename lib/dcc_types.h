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

typedef struct
{
    bool is_prog;
    uint8_t length;
    uint8_t data[DCC_MAX_DATA_BYTES];
    uint64_t cmd_data;
    uint8_t repeats;
} raw_dcc_cmd_t;

#endif // PICO_DCC_TYPES_H