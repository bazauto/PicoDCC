#include <string.h>
#include <stdexcept>
#include <algorithm>

#include "pico_dcclocos.h"

PicoDccLocos::PicoDccLocos(queue_t *_dccex_cmd_queue) : dccex_cmd_queue(_dccex_cmd_queue)
{
    // Reserve memory for our configured MAX
    sem_init(&locos_lock, 1, 1);
    locos.reserve(MAX_LOCO);
    last_loco_reminder = INVALID_LOCO_ADDR;
}

PicoDccLoco *PicoDccLocos::findLoco(uint16_t address)
{
    PicoDccLoco *loco = nullptr;

    size_t vectorSize = locos.size();

    sem_acquire_blocking(&locos_lock);
    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            loco = &locos[i];
            break;
        }
    }
    sem_release(&locos_lock);

    return loco;
}

bool PicoDccLocos::getNextReminder(raw_dcc_cmd_t &cmd)
{
    if (locos.empty())
    {
        return false; // No locos to remind
    }

    sem_acquire_blocking(&locos_lock);

    // Find the index of the next loco
    size_t nextIndex = 0;
    if (last_loco_reminder != INVALID_LOCO_ADDR)
    { // If we have a last reminded loco
        // Find the current index of last_loco_reminder
        for (size_t i = 0; i < locos.size(); ++i)
        {
            if (locos[i].getAddress() == last_loco_reminder)
            {
                nextIndex = (i + 1) % locos.size(); // Move to the next index, looping back to 0 if necessary
                break;
            }
        }
    }

    // Check if the loco is valid before returning it
    if (!locos[nextIndex].isValid()) {
        // Remove invalid loco from the collection
        locos.erase(locos.begin() + nextIndex);
        sem_release(&locos_lock);
        return false;
    }

    // Get the command for the next loco that will be sent to the track
    cmd = locos[nextIndex].getThrottleCommand();
    last_loco_reminder = locos[nextIndex].getAddress();

    sem_release(&locos_lock);

    return true;
}

std::list<raw_dcc_cmd_t> PicoDccLocos::getEmergencyStopCommands()
{
    std::list<raw_dcc_cmd_t> stopCmds;
    if (!locos.empty())
    {
        sem_acquire_blocking(&locos_lock);

        for (PicoDccLoco& loco : locos)
        {
            stopCmds.push_back(loco.getEmergecyStopCommand());

            // Notify that this loco has been processed
            pico_dccex_packet packet = {'t', loco.getAddress(), 0, 0, false, DCCEX_TRACK_ALL};
            queue_add_blocking(dccex_cmd_queue, &packet );
        }

        sem_release(&locos_lock);
    }

    return stopCmds;
}

void PicoDccLocos::addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    PicoDccLoco newLoco(packet);
    
    sem_acquire_blocking(&locos_lock);

    locos.push_back(newLoco);
    cmd = newLoco.getThrottleCommand();

    sem_release(&locos_lock);

    queue_add_blocking(dccex_cmd_queue, packet->getPacketData());
}

void PicoDccLocos::updateLoco(PicoDccLoco *loco, PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    if (loco->update(packet))
    {
        queue_add_blocking(dccex_cmd_queue, packet->getPacketData());
    }
    cmd = loco->getThrottleCommand();
}

void PicoDccLocos::forgetLoco(uint16_t address)
{
    sem_acquire_blocking(&locos_lock);

    for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end();)
    {
        if (it->getAddress() == address)
        {
            locos.erase(it);
            break;
        }
    }

    sem_release(&locos_lock);
}

void PicoDccLocos::forgetAllLocos()
{
    sem_acquire_blocking(&locos_lock);
    last_loco_reminder = INVALID_LOCO_ADDR;
    locos.clear();

    sem_release(&locos_lock);
}
