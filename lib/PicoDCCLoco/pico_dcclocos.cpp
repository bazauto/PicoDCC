#include <string.h>
#include <stdexcept>

#include "pico_dcclocos.h"
#include "../dccex_communication.h"

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
    sem_acquire_blocking(&locos_lock);

    if (locos.empty())
    {
        sem_release(&locos_lock);
        return false; // No locos to remind
    }

    // Find the index of the next loco
    size_t nextIndex = 0;
    if (last_loco_reminder != INVALID_LOCO_ADDR)
    { // If we have a last reminded loco
        // Find the current index of last_loco_reminder
        bool found = false;
        for (size_t i = 0; i < locos.size(); ++i)
        {
            if (locos[i].getAddress() == last_loco_reminder)
            {
                nextIndex = (i + 1) % locos.size(); // Move to the next index, looping back to 0 if necessary
                found = true;
                break;
            }
        }
        // If last_loco_reminder is not found, start from index 0
        if (!found) {
            nextIndex = 0;
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

    // Reminder commands should NOT use the repeat mechanism
    // They will be sent once and getNextReminder() will be called again on the next cycle
    cmd.repeats = 0;

    sem_release(&locos_lock);

    return true;
}

void PicoDccLocos::addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    PicoDccLoco newLoco(packet);

    sem_acquire_blocking(&locos_lock);

    locos.push_back(newLoco);
    cmd = newLoco.getThrottleCommand();

    sem_release(&locos_lock);
}

bool PicoDccLocos::updateLocoThrottle(uint16_t address, PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    sem_acquire_blocking(&locos_lock);

    // Find the loco and update it while holding the lock
    bool found = false;
    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            // Update throttle while holding the lock to prevent race conditions
            locos[i].update(packet);
            cmd = locos[i].getThrottleCommand();
            found = true;
            break;
        }
    }

    sem_release(&locos_lock);
    return found;
}



void PicoDccLocos::forgetLoco(uint16_t address)
{
    sem_acquire_blocking(&locos_lock);

    for (std::vector<PicoDccLoco>::iterator it = locos.begin(); it != locos.end();)
    {
        if (it->getAddress() == address)
        {
            it = locos.erase(it);  // erase returns iterator to next element
            break;
        }
        else
        {
            ++it;  // increment iterator only when not erasing
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

size_t PicoDccLocos::getLocoCount()
{
    sem_acquire_blocking(&locos_lock);
    size_t count = locos.size();
    sem_release(&locos_lock);
    return count;
}

void PicoDccLocos::sendEmergencyStopResponses()
{
    sem_acquire_blocking(&locos_lock);

    // Send locomotive status response for each active locomotive
    // This is required by DCC-EX specification for emergency stop
    for (size_t i = 0; i < locos.size(); ++i) {
        if (locos[i].isValid()) {
            // Create emergency stop status update packet
            pico_dccex_packet emergency_status = {
                't',                    // throttle command opcode
                (int)locos[i].getAddress(),  // locomotive address
                0,                      // speed = 0 (emergency stop)
                1,                      // direction = forward (maintain current direction)
                false,                  // power_on (not used for throttle)
                DCCEX_TRACK_MAIN       // track (not used for throttle)
            };

            PicoDccExPacket response_packet(emergency_status);
            DCCEX_RESPONSE(response_packet.getDccExCabUpdate());
        }
    }

    sem_release(&locos_lock);
}
