#include <string.h>
#include <stdexcept>
#include <algorithm>

#include "pico_dcclocos.h"

PicoDccLocos::PicoDccLocos()
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

    // Get the command for the next loco that will be sent to the track
    cmd = locos[nextIndex].getThrottleCommand();

    sem_release(&locos_lock);

    // Return a pointer to the next loco
    return &locos[nextIndex];
}

std::list<raw_dcc_cmd_t> PicoDccLocos::getEmergencyStopCommands()
{
    std::list<raw_dcc_cmd_t> stopCmds;
    if (!locos.empty())
    {
        sem_acquire_blocking(&locos_lock);
        for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end();)
            stopCmds.push_back(it->getEmergecyStopCommand());
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
}

void PicoDccLocos::updateLoco(uint16_t address, PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    sem_acquire_blocking(&locos_lock);

    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            locos[i].update(packet);
            cmd = locos[i].getThrottleCommand();
            break;
        }
    }

    sem_release(&locos_lock);
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
