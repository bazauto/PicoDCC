#ifndef DCC_TRACK_QUEUE_H
#define DCC_TRACK_QUEUE_H

#include <stdio.h>
#include <pico/util/queue.h>

#define MAX_LOCO 50

typedef struct
{
    uint8_t length;
    uint8_t data[7];
} queue_cmd_t;

typedef struct
{
    uint8_t id;
    uint8_t dir_speed;
} loco_t;


void dcc_track_init(queue_t *cmd_queue, bool is_main);
void dcc_track_runner();

#endif