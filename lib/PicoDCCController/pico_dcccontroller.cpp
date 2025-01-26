#include "pico_dcccontroller.h"

PicoDccController::PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s)
{
    // Some things that should never be
    assert(main_track_s.signal_pin != prog_track_s.signal_pin);
    assert(main_track_s.signal_pin != UNUSED_PIN);
    assert(main_track_s.ctrl_pin != prog_track_s.ctrl_pin);
    assert(main_track_s.ctrl_pin != UNUSED_PIN);
    assert(main_track_s.adc_num != prog_track_s.adc_num);
    assert(prog_track_s.ctrl_pin != UNUSED_PIN);

    // Setup the queues to tranfer actions between CPU cores
    queue_init(&dcc_cmd_queue, sizeof(pico_dccex_packet), CMD_QUEUE_LENGTH);
    queue_init(&dccex_cmd_queue, sizeof(pico_dccex_packet), CMD_QUEUE_LENGTH);

    // Setup the tracks
    main_track = new PicoDccTrack(false, main_track_s);
    prog_track = new PicoDccTrack(true, prog_track_s);

    // Setup DCCEX Packate processing
    pico_dccex = new PicoDccEx(MAX_LOCO);

    // Setup our loco store
    pico_locos = new PicoDccLocos(&dccex_cmd_queue);
}

// This is the Core 0 loop
void PicoDccController::dccexLoop()
{
    pico_dccex->loop(&dcc_cmd_queue, &dccex_cmd_queue);
}

// This is the Core 1 loop
void PicoDccController::dccLoop()
{
    processDccExFromJMRI();
    processReminders();

    main_track->loop();
    prog_track->loop();
}

void PicoDccController::processReminders()
{
    // Reminders are only sent to the main track so don't need to check for that
    raw_dcc_cmd_t cmd = {};
    bool foundLoco = pico_locos->getNextReminder(cmd);

    if (foundLoco)
        main_track->queueCommand(&cmd);
    else
        main_track->sendIdle();
}

void PicoDccController::processDccExFromJMRI()
{
    // Process any incoming message from JMRI queue to be sent to the track
    pico_dccex_packet packetData;
    if (!queue_try_remove(&dcc_cmd_queue, &packetData))
    {
        return;
    }

    PicoDccExPacket packet(packetData);
    if (packet.isValid()) 
    {    
        if (packet.isPowerCommand())
        {
            if (packet.getTrack() == DCCEX_TRACK_ALL || packet.getTrack() == DCCEX_TRACK_PROG)
                prog_track->setPower(packet.getPowerOn());

            if (packet.getTrack() == DCCEX_TRACK_ALL || packet.getTrack() == DCCEX_TRACK_MAIN)
                main_track->setPower(packet.getPowerOn());

            queue_add_blocking(&dccex_cmd_queue, packet.getPacketData());
        }

        if (packet.isEmergencyStopCommand())
        {
            std::list<raw_dcc_cmd_t> stopCmds = pico_locos->getEmergencyStopCommands();
            for (std::list<raw_dcc_cmd_t>::iterator it = stopCmds.begin(); it != stopCmds.end();)
            {
                raw_dcc_cmd_t cmd = *it;
                main_track->queueCommand(&cmd);
            }
        }

        if (packet.isThrottleCommand() || packet.isFunctionCommand())
        {
            raw_dcc_cmd_t cmd = { false, 0 };
            PicoDccLoco *loco = pico_locos->findLoco(packet.getCab());

            if (loco == nullptr)
            {
                pico_locos->addLoco(&packet, cmd);
            }
            else
            {
                pico_locos->updateLoco(loco->getAddress(), &packet, cmd);
            }
            
            if (cmd.length > 0)
                main_track->queueCommand(&cmd);
        }

        if (packet.isAccesoryCommand())
        {
            main_track->queueCommand(packet.getRawDccAccessoryCmd());
        }
    }
}