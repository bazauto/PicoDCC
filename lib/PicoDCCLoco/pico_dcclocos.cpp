#include <string.h>

#include "pico_dcclocos.h"
#include "../dccex_communication.h"
#include "../pico_diagnostic.h"

PicoDccLocos::PicoDccLocos()
{
    // Reserve memory for our configured MAX
    sem_init(&locos_lock, 1, 1);
    locos.reserve(MAX_LOCO);
    last_loco_reminder = INVALID_LOCO_ADDR;
    station_speed_steps = (uint8_t)DCC_DEFAULT_SPEED_STEPS;
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

    // Check if the loco is valid before returning it. PicoDccTrack::sendCommand()
    // has no guard against cmd.length == 0 -- it would transmit a 1-byte
    // garbage packet -- so a zero-length command must never be returned as a
    // reminder either. Every valid address currently produces at least two
    // bytes, so this cannot fire today; it is defence in depth against that
    // invariant being broken later.
    if (!locos[nextIndex].isValid() || locos[nextIndex].getThrottleCommand().length == 0) {
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

bool PicoDccLocos::addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)
{
    // Construction and validation touch no shared state, so they happen
    // outside the lock.
    PicoDccLoco newLoco(packet, station_speed_steps);
    if (!newLoco.isValid())
    {
        memset(&cmd, 0, sizeof(cmd));
        LOG_WARNING(COMPONENT_DCCEX, "Loco rejected: invalid throttle command");
        return false;
    }

    sem_acquire_blocking(&locos_lock);

    // size() is read inside the lock -- per CLAUDE.md rule 5, checking
    // container state outside the lock is the race, not just mutating it.
    if (locos.size() >= MAX_LOCO)
    {
        sem_release(&locos_lock);
        memset(&cmd, 0, sizeof(cmd));
        LOG_WARNING(COMPONENT_DCCEX, "Loco rejected: collection full");
        return false;
    }

    locos.push_back(newLoco);
    cmd = newLoco.getThrottleCommand();

    sem_release(&locos_lock);
    return true;
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

bool PicoDccLocos::setStationSpeedSteps(uint8_t steps)
{
    if (!dcc_is_valid_speed_step_mode(steps))
    {
        LOG_WARNING(COMPONENT_DCCEX, "Station speed step mode rejected: not 28 or 128");
        return false;
    }

    sem_acquire_blocking(&locos_lock);

    station_speed_steps = steps;

    // Locos already following the default move with it; ones named by a
    // <D SPEED28|SPEED128 cab> stay where they were put.
    for (size_t i = 0; i < locos.size(); ++i)
    {
        locos[i].applyDefaultSpeedSteps(steps);
    }

    sem_release(&locos_lock);
    return true;
}

uint8_t PicoDccLocos::getStationSpeedSteps()
{
    sem_acquire_blocking(&locos_lock);
    uint8_t steps = station_speed_steps;
    sem_release(&locos_lock);
    return steps;
}

bool PicoDccLocos::setLocoSpeedSteps(uint16_t address, uint8_t steps)
{
    if (!dcc_is_valid_loco_address(address))
    {
        LOG_WARNING(COMPONENT_DCCEX, "Speed step mode rejected: cab out of range");
        return false;
    }

    if (!dcc_is_valid_speed_step_mode(steps))
    {
        LOG_WARNING(COMPONENT_DCCEX, "Speed step mode rejected: not 28 or 128");
        return false;
    }

    // Construction happens outside the lock, as in addLoco(); it touches no
    // shared state. It is discarded unnamed if the loco already exists.
    PicoDccLoco newLoco(address, steps);
    newLoco.setSpeedSteps(steps);

    sem_acquire_blocking(&locos_lock);

    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            locos[i].setSpeedSteps(steps);
            sem_release(&locos_lock);
            return true;
        }
    }

    // size() read inside the lock, per CLAUDE.md rule 5.
    if (locos.size() >= MAX_LOCO)
    {
        sem_release(&locos_lock);
        LOG_WARNING(COMPONENT_DCCEX, "Speed step mode rejected: collection full");
        return false;
    }

    // Unknown cab: create it stopped and forward. The reminder stream will now
    // assert "speed 0" for this address, which is the safe direction to be
    // wrong in -- it can only ever hold a locomotive still.
    locos.push_back(newLoco);

    sem_release(&locos_lock);
    return true;
}

uint8_t PicoDccLocos::getLocoSpeedSteps(uint16_t address)
{
    sem_acquire_blocking(&locos_lock);

    uint8_t steps = station_speed_steps;
    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            steps = locos[i].getSpeedSteps();
            break;
        }
    }

    sem_release(&locos_lock);
    return steps;
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
