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

bool PicoDccLocos::findLoco(uint16_t address, PicoDccLoco &loco)
{
    sem_acquire_blocking(&locos_lock);

    auto it = std::find_if(locos.begin(),
                           locos.end(),
                           [address](PicoDccLoco &obj)
                           { return obj.getAddress() == address; });

    bool found = false;
    if (it != locos.end())
    {
        loco = *it;
        found = true;
    }
    sem_release(&locos_lock);

    return found;
}

bool PicoDccLocos::getNextReminder(raw_dcc_cmd_t &cmd)
{
    if (locos.empty())
    {
        return false;
    }
    else
    {
        sem_acquire_blocking(&locos_lock);

        // Pick the next loco and re-send its speed packet
        bool foundLoco = false;
        std::vector<PicoDccLoco>::iterator nextLoco = locos.end();
        for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end(); it++)
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
        cmd = nextLoco->getThrottleCommand();
        sem_release(&locos_lock);

        return true;
    }
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

bool PicoDccLocos::updateLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    PicoDccLoco *loco;
    if (findLoco(packet->getCab(), *loco))
    {
        loco->update(packet);
        return true;
    }
    else
    {
        loco = new PicoDccLoco(packet);
        locos.push_back(*loco);
        return true;
    }

    return false;
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
