#ifndef PICO_DCC_TYPES_H
#define PICO_DCC_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define DCC_MAX_DATA_BYTES 5

typedef struct 
{
    bool is_prog;
    uint8_t length;
    uint8_t data[DCC_MAX_DATA_BYTES];
    uint64_t cmd_data;
    uint8_t repeats;
} raw_dcc_cmd_t;

#endif // PICO_DCC_TYPES_H