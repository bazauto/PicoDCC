#ifndef PICO_DCCLOCO_H
#define PICO_DCCLOCO_H

#include <stdio.h>
#include <string.h>
#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/sem.h>
#include <pico/util/queue.h>
#endif

#include "../PicoDCCEX/pico_dccexpacket.h"
#include "../PicoDCCTrack/pico_dcctrack.h"

// INVALID_LOCO_ADDR lives in dcc_types.h now (pulled in transitively via
// pico_dcctrack.h), alongside the other address and speed limits.

class PicoDccLoco
{
private:
    // These are values that are copied with the object, everything else is calculated as needed.
    uint16_t address;
    uint8_t speed;
    bool forward;

    // How this loco's speed is encoded on the rails: DCC_SPEED_STEPS_128 or
    // DCC_SPEED_STEPS_28 (#8). Held in RAM only, never persisted -- the
    // orchestrator owns the roster and re-asserts each loco's mode when it
    // sees the boot banner, so a copy in flash could only ever disagree
    // with it.
    uint8_t speed_steps;

    // False while this loco simply follows the station default, true once a
    // <D SPEED28|SPEED128 cab> has named it. A station-default change moves
    // every loco that is still following it, and leaves the overridden ones
    // where they are.
    bool speed_steps_overridden;



    // This is the command that will be sent to the track when needed.  It is initially zero length to avoid it being used.
    raw_dcc_cmd_t cmd;

public:
    PicoDccLoco(PicoDccExPacket *packet, uint8_t speed_steps = DCC_DEFAULT_SPEED_STEPS);
    PicoDccLoco(uint16_t address, uint8_t speed_steps = DCC_DEFAULT_SPEED_STEPS);
    PicoDccLoco(uint16_t address, uint8_t speed, bool forward, uint8_t speed_steps = DCC_DEFAULT_SPEED_STEPS);

    // Copy constructor. Every member is listed here explicitly, so a member
    // added without a line below is silently dropped on every copy -- and
    // PicoDccLocos stores locos by value in a std::vector, so that is every
    // insertion and every reallocation.
    PicoDccLoco(const PicoDccLoco &other)
        : address(other.address), speed(other.speed), forward(other.forward),
          speed_steps(other.speed_steps),
          speed_steps_overridden(other.speed_steps_overridden), cmd(other.cmd) {}

    // Copy assignment, listing every member for the same reason as the copy
    // constructor above. PicoDccLocos::getLoco() uses this to hand a caller a
    // snapshot taken under the lock, so a member missing from this list is a
    // field the caller silently never sees.
    //
    // Written out member by member rather than left implicit, and rather than
    // done with a struct assignment: rule 4 in CLAUDE.md forbids assigning
    // structs wholesale, and `cmd` is a raw_dcc_cmd_t.
    PicoDccLoco &operator=(const PicoDccLoco &other)
    {
        if (this != &other)
        {
            address = other.address;
            speed = other.speed;
            forward = other.forward;
            speed_steps = other.speed_steps;
            speed_steps_overridden = other.speed_steps_overridden;
            memcpy(&cmd, &other.cmd, sizeof(cmd));
        }
        return *this;
    }

    // Comparision operator for ease of comparing objects
    bool operator==(const PicoDccLoco &other) const {
        return address == other.address;
    }

    uint16_t getAddress() { return address; }
    uint8_t getSpeed() const { return speed; }
    bool isForward() const { return forward; }

    uint8_t getSpeedSteps() const { return speed_steps; }
    bool hasSpeedStepsOverride() const { return speed_steps_overridden; }

    // Names this loco's mode explicitly. Returns true when the encoding
    // actually moved, so the caller can tell a real change from a re-assert.
    bool setSpeedSteps(uint8_t steps);

    // Follows the station default. Ignored once setSpeedSteps() has named this
    // loco, which is what keeps a per-loco 28-step decoder from being dragged
    // back to 128 by a later station-wide command.
    bool applyDefaultSpeedSteps(uint8_t steps);

    bool update(PicoDccExPacket *packet);
    bool updateControl(bool _forward, uint8_t _speed);
    void updateFunct(uint8_t function, bool value);

    bool verifyCV(int8_t cvNumber, int8_t expectedByte);
    bool verifyCV(int8_t cvNumber, bool expectedBit);

    int8_t readCVByte(int8_t cvNumber);
    bool readCVBit(int8_t cvNumber, uint8_t bit);

    void writeCVBytes(int8_t cvNumber, int8_t newByte);
    void writeCVBit(int8_t cvNumber, bool newBit);

    raw_dcc_cmd_t getThrottleCommand();
    raw_dcc_cmd_t getFunctionCommand(uint8_t fnGroup);

    bool isValid() const;

private:
    void generateThrottleCommand();
};

#endif