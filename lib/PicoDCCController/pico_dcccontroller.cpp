#include "pico_dcccontroller.h"

PicoDccController::PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s)
{
    // Some things that should never be
    assert(main_track_s.ctrl_pin != prog_track_s.ctrl_pin);
    assert(main_track_s.ctrl_pin != UNUSED_PIN);
    assert(main_track_s.adc_num != prog_track_s.adc_num);
    assert(prog_track_s.ctrl_pin != UNUSED_PIN);

    // Setup the queue that core 0 will use to send us commands
    queue_init(&cmd_queue, sizeof(raw_dcc_cmd_t), CMD_QUEUE_LENGTH);

    // Setup the tracks
    main_track = new PicoDccTrack(false);
    main_track->init(main_track_s);

    prog_track = new PicoDccTrack(true);
    prog_track->init(prog_track_s);

    // Setup DCCEX Packate processing
    pico_dccex = new PicoDccEx(MAX_LOCO);

    // Setup our loco store
    pico_locos = new PicoDccLocos();
}

void PicoDccController::dccexLoop()
{
    pico_dccex->loop(&cmd_queue);
}

void PicoDccController::dccLoop()
{
    // Process any incoming messages to be sent to the track
    PicoDccExPacket *packet;
    raw_dcc_cmd_t cmd;
    if (queue_try_remove(&cmd_queue, packet))
    {
        if (packet->isPowerCommand() && packet->getTrack() == DCCEX_TRACK_ALL || packet->getTrack() == DCCEX_TRACK_PROG)
            prog_track->setPower(packet->getPowerOn());
            // TODO: Add bit to trigger DCCEX to send update

        if (packet->isPowerCommand() && packet->getTrack() == DCCEX_TRACK_ALL || packet->getTrack() == DCCEX_TRACK_MAIN)
            main_track->setPower(packet->getPowerOn());
            // TODO: Add bit to trigger DCCEX to send update

        if (packet->isEmergencyStopCommand())
            emergecyStop();
            // TODO: Add bit to trigger DCCEX to send update

        if (packet->isThrottleCommand() || packet->isFunctionCommand())
        {
            bool sendUpdate = pico_locos->updateLoco(packet, cmd);

            if (sendUpdate)
            {
                main_track->processCommand(&cmd);
                // TODO: Add bit to trigger DCCEX to send update
            }
        }
    }
    else
    {
        bool foundLoco = pico_locos->getNextReminder(cmd);

        if (foundLoco)
            main_track->processCommand(&cmd);
        else
            main_track->sendIdle();
    }

    main_track->loop();
    prog_track->loop();
}

void PicoDccController::emergecyStop()
{
    // sem_acquire_blocking(&locos_lock);
    // if (locos.empty())
    // {
    //     sem_release(&locos_lock);
    //     return;
    // }

    // for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end();)
    // {
    //     raw_dcc_cmd_t cmd = it->getEmergecyStopCommand();
    //     main_track->processCommand(&cmd);
    // }

    // sem_release(&locos_lock);
}
