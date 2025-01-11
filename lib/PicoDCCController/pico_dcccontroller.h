#ifndef PICO_DCCCONTROLLER_H
#define PICO_DCCCONTROLLER_H

#include <stdio.h>
#include <pico/util/queue.h>
#include <pico/sem.h>
#include <vector>
#include <list>
#include "../PicoDCCEX/pico_dccex.h"
#include "../PicoDCCLoco/pico_dccloco.h"
#include "../PicoDCCLoco/pico_dcclocos.h"
#include "../PicoDCCTrack/pico_dcctrack.h"

#define CMD_QUEUE_LENGTH 5
#define MAX_LOCO 50
#define INVALID_LOCO_ADDR 65535

class PicoDccController
{
private:
    // We support 1 main and 1 prog track in the controller
    PicoDccTrack *main_track;
    PicoDccTrack *prog_track;

    PicoDccEx *pico_dccex;

    PicoDccLocos *pico_locos;

    queue_t dcc_cmd_queue;
    queue_t dccex_cmd_queue;

    void processDccExFromJMRI();

public:
    PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s);

    void dccLoop();
    void dccexLoop();
};

#endif