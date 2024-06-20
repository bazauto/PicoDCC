#ifndef PICO_DCCCONTROLLER_H
#define PICO_DCCCONTROLLER_H

#include <stdio.h>
#include <pico/util/queue.h>
#include <pico/sem.h>
#include <vector>
#include "../PicoDCCEX/pico_dccex.h"
#include "../PicoDCCTrack/pico_dcctrack.h"

#define CMD_QUEUE_LENGTH 5
#define MAX_LOCO 50
#define INVALID_LOCO_ADDR 65535

typedef struct
{
    uint16_t addr;
    bool forward;
    uint8_t speed;
} loco_t;


class PicoDccController
{
private:
    // We support 1 main and 1 prog track in the controller
    PicoDccTrack *main_track;
    PicoDccTrack *prog_track;

    PicoDccEx *pico_dccex;

    queue_t cmd_queue;
    std::vector<loco_t> locos;
    uint16_t last_loco_reminder;
    semaphore_t locos_lock;

    void processDccExPacket(const PicoDccExPacket *packet);

public:
    PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s);

    void dccLoop();
    void dccexLoop();

    void repeatLocoOrIdle();
    void forgetLoco(uint16_t addr);
    void forgetAllLocos();

    queue_t* getCommandQueue() { return &cmd_queue; }
};

#endif