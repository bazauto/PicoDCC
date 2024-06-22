#include "pico_dcccontroller.h"

PicoDccController::PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s)
{
    // Some things that should never be
    assert(main_track_s.ctrl_pin != prog_track_s.ctrl_pin);
    assert(main_track_s.ctrl_pin != UNUSED_PIN);
    assert(main_track_s.adc_num != prog_track_s.adc_num);
    assert(prog_track_s.ctrl_pin != UNUSED_PIN);

    // Reserve memory for our configured MAX
    sem_init(&locos_lock, 0, 1);
    locos.reserve(MAX_LOCO);
    last_loco_reminder = INVALID_LOCO_ADDR;

    // Setup the queue that core 0 will use to send us commands
    queue_init(&cmd_queue, sizeof(raw_dcc_cmd_t), CMD_QUEUE_LENGTH);

    // Setup the tracks
    main_track = new PicoDccTrack(false);
    main_track->init(main_track_s);

    prog_track = new PicoDccTrack(true);
    prog_track->init(prog_track_s);

    // Setup DCCEX Packate processing
    pico_dccex = new PicoDccEx(MAX_LOCO);
}

void PicoDccController::dccexLoop()
{
    pico_dccex->loop(&cmd_queue);
}

void PicoDccController::dccLoop()
{
    // Process any incoming messages to be sent to the track
    PicoDccExPacket *cmd;
    bool gotCmd = queue_try_remove(&cmd_queue, cmd);

    if (gotCmd)
    {
            if (cmd->isPowerCommand() && cmd->getTrack() == DCCEX_TRACK_ALL || cmd->getTrack() == DCCEX_TRACK_PROG)
                prog_track->setPower(cmd->getPowerOn());

            if (cmd->isPowerCommand() && cmd->getTrack() == DCCEX_TRACK_ALL || cmd->getTrack() == DCCEX_TRACK_MAIN)
                main_track->setPower(cmd->getPowerOn());

            if (cmd->isThrottleCommand())
            {
                main_track->processCommand(cmd->getRawDccThrottleCmd());
            }

            if (cmd->isFunctionCommand())
            {
                main_track->processCommand(cmd->getRawDccFunctionCmd());
            }
    }
    else
    {
        this->repeatLocoOrIdle();
    }

    main_track->loop();
    prog_track->loop();
}

void PicoDccController::repeatLocoOrIdle()
{
    sem_acquire_blocking(&locos_lock);
    if (locos.empty())
    {
        sem_release(&locos_lock);
        main_track->sendIdle();
    }
    else
    {
        // Pick the next loco and re-send its speed packet
        bool foundLoco = false;
        std::vector<PicoDccLoco>::iterator nextLoco = locos.end();
        for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end();)
        {
            // This means the last loco was the last one to be sent
            if (foundLoco)
            {
                nextLoco = it;
                break;
            }

            // If this was the last sent, note it to switch to the next one in the loop
            if (it->getAddress() == last_loco_reminder)
                foundLoco = true;
        }
        // note the details from the loco so we can unlock before signalling to the track as this might block
        raw_dcc_cmd_t cmd = nextLoco->getThrottleCommand();
        sem_release(&locos_lock);

        main_track->processCommand(&cmd);
    }
}

void PicoDccController::forgetLoco(uint16_t addr)
{
    sem_acquire_blocking(&locos_lock);

    for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end();)
    {
        if (it->getAddress() == addr)
        {
            locos.erase(it);
            break;
        }
    }

    sem_release(&locos_lock);
}

void PicoDccController::forgetAllLocos()
{
    sem_acquire_blocking(&locos_lock);
    last_loco_reminder = INVALID_LOCO_ADDR;
    locos.clear();

    sem_release(&locos_lock);
}