#include <cstddef>
#include <stdexcept>

#include "pico_dcclocos.h"
#include "../dccex_communication.h"

namespace {
void copy_status_string(char* dest, const char* src, size_t max_len) {
    if (!dest || max_len == 0) {
        return;
    }

    size_t i = 0;
    if (src != nullptr) {
        for (; i + 1 < max_len && src[i] != '\0'; ++i) {
            dest[i] = src[i];
        }
    }

    for (; i + 1 < max_len; ++i) {
        dest[i] = '\0';
    }

    dest[max_len - 1] = '\0';
}
}

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

bool PicoDccLocos::getLocoStatus(uint16_t address, char* status_buffer, size_t buffer_size)
{
    bool found = false;
    
    sem_acquire_blocking(&locos_lock);
    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            // Get status while holding the lock to prevent vector reallocation
            const char* status = locos[i].getDccExStatus();
            copy_status_string(status_buffer, status, buffer_size);
            found = true;
            break;
        }
    }
    sem_release(&locos_lock);
    
    return found;
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
    
    // Get the appropriate command based on packet type
    if (packet->isFunctionCommand()) {
        // For function commands, send the specific function group
        uint8_t fnGroup = 0;
        uint8_t fn = packet->getFunct();
        if (fn <= 4) fnGroup = 1;
        else if (fn <= 8) fnGroup = 2;
        else if (fn <= 12) fnGroup = 3;
        else if (fn <= 20) fnGroup = 4;
        else fnGroup = 5;
        
        cmd = newLoco.getFunctionCommand(fnGroup);
    } else {
        // For throttle commands, send throttle
        cmd = newLoco.getThrottleCommand();
    }

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
            // Update the loco state
            // Doesn't send anything, just updates internal state
            locos[i].update(packet);
            
            // Get the appropriate command based on packet type
            if (packet->isFunctionCommand()) {
                // For function commands, send the specific function group
                uint8_t fnGroup = 0;
                uint8_t fn = packet->getFunct();
                if (fn <= 4) fnGroup = 1;
                else if (fn <= 8) fnGroup = 2;
                else if (fn <= 12) fnGroup = 3;
                else if (fn <= 20) fnGroup = 4;
                else fnGroup = 5;
                
                cmd = locos[i].getFunctionCommand(fnGroup);
            } else {
                // For throttle commands, send throttle
                cmd = locos[i].getThrottleCommand();
            }
            
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

void PicoDccLocos::getLocoStatusOrCreate(uint16_t address, char* status_buffer, size_t buffer_size)
{
    sem_acquire_blocking(&locos_lock);
    
    bool found = false;
    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            // Get status while holding the lock
            const char* status = locos[i].getDccExStatus();
            copy_status_string(status_buffer, status, buffer_size);
            found = true;
            break;
        }
    }
    
    if (!found) {
        // Loco not found - create temporary one with default state
        PicoDccLoco tempLoco(address, 0, true);
        const char* status = tempLoco.getDccExStatus();
        copy_status_string(status_buffer, status, buffer_size);
    }
    
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
            const char* status = locos[i].getDccExStatus();
            DCCEX_RESPONSE(status);
        }
    }

    sem_release(&locos_lock);
}
