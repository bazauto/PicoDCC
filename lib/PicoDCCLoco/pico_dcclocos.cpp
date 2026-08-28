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

bool PicoDccLocos::getLoco(uint16_t address, PicoDccLoco &out)
{
    bool found = false;

    sem_acquire_blocking(&locos_lock);
    for (size_t i = 0; i < locos.size(); ++i)
    {
        if (locos[i].getAddress() == address)
        {
            // Copied here, inside the lock, so the caller never holds anything
            // that points into the vector. This replaces findLoco(), which
            // returned &locos[i] *after* releasing the lock (#37): addLoco()'s
            // push_back could reallocate the buffer, and the erase in
            // forgetLoco() and getNextReminder() -- the latter on Core 1 --
            // shifts the elements down, so the pointer silently began
            // referring to a different locomotive.
            out = locos[i];
            found = true;
            break;
        }
    }
    sem_release(&locos_lock);

    return found;
}

bool PicoDccLocos::getNextReminder(raw_dcc_cmd_t &cmd)
{
    // Core 1 calls this every pass of PicoDccTrack::loop(), so it must never wait on a
    // lock Core 0 holds (rule 4). Core 0 takes the same lock for addLoco(),
    // updateLocoThrottle(), forgetAllLocos() and getLocoCount() -- and the display path
    // calls getLocoCount() at 10 Hz, so contention is routine rather than exceptional.
    //
    // Failing to acquire is not an error and is not logged: the caller falls through to
    // sendIdle(), which keeps the DCC waveform alternating, and `last_loco_reminder` is
    // left untouched so the round-robin resumes at the same loco on the next pass. No
    // loco is skipped -- one is deferred by a single pass, which no decoder can observe.
    //
    // The alternative is what this replaces: Core 1 parked here emitting no packets at
    // all, closing on the 100 ms timing-violation cutoff that powers both tracks down.
    if (!sem_try_acquire(&locos_lock))
    {
        return false;
    }
    
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

size_t PicoDccLocos::stopAllLocos()
{
    sem_acquire_blocking(&locos_lock);

    size_t stopped = 0;
    for (size_t i = 0; i < locos.size(); ++i)
    {
        // Direction is preserved: the decoder still needs to know which way to
        // go when an operator resumes, and changing it here would reverse a
        // locomotive the moment the throttle came back.
        //
        // Speed 0, not the estop sentinel. The broadcast has already slammed
        // everything to a halt; what the reminders have to do from now on is
        // keep saying "stopped", and a controlled stop is the packet that says
        // that. Repeating an emergency stop forever would also mean an operator
        // resuming had to clear it first.
        if (locos[i].updateControl(locos[i].isForward(), 0))
        {
            stopped++;
        }
    }

    sem_release(&locos_lock);
    return stopped;
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
    // Snapshot under the lock, emit outside it (#17).
    //
    // This used to hold locos_lock across the whole emit loop. DCCEX_RESPONSE is
    // uart_puts(uart0, ...), which blocks until the bytes are clear; each `<l ...>` is
    // ~16 bytes, so a full table of MAX_LOCO at 115200 baud held the lock for 70-90 ms.
    // For all of that, Core 1 was parked in getNextReminder() emitting no DCC at all --
    // within ~10-30 ms of the station's own 100 ms timing-violation cutoff, which powers
    // both tracks down. During an emergency stop, of all moments.
    //
    // The margin degraded with table size, so it was invisible on a bench with two locos
    // and worst on a busy layout. getNextReminder() is now non-blocking as well, so
    // neither half of that interaction survives; this half is fixed independently
    // because a 90 ms lock hold is wrong whether or not the other side waits on it.
    //
    // Static rather than stack: rule 4 bars large stack allocations, and Core 0's
    // dccexLoop() is the only caller, so there is no reentrancy to guard against.
    struct EStopEcho {
        uint16_t address;
        bool     forward;
    };
    static EStopEcho echoes[MAX_LOCO] __attribute__((aligned(8)));
    size_t echo_count = 0;

    sem_acquire_blocking(&locos_lock);
    for (size_t i = 0; i < locos.size() && echo_count < MAX_LOCO; ++i) {
        if (locos[i].isValid()) {
            // The direction is the loco's own: this said "forward" for every loco while
            // claiming in a comment to maintain the current direction, which it could
            // not do while the collection was being emptied a line later (#3). With the
            // locos retained, it can.
            echoes[echo_count].address = locos[i].getAddress();
            echoes[echo_count].forward = locos[i].isForward();
            echo_count++;
        }
    }
    sem_release(&locos_lock);

    // Send locomotive status response for each active locomotive
    // This is required by DCC-EX specification for emergency stop
    for (size_t i = 0; i < echo_count; ++i) {
        pico_dccex_packet emergency_status = {
            't',                    // throttle command opcode
            (int)echoes[i].address, // locomotive address
            0,                      // speed = 0, matching the held state
            echoes[i].forward ? 1 : 0,
            false,                  // power_on (not used for throttle)
            DCCEX_TRACK_MAIN       // track (not used for throttle)
        };

        PicoDccExPacket response_packet(emergency_status);
        DCCEX_RESPONSE(response_packet.getDccExCabUpdate());
    }
}
