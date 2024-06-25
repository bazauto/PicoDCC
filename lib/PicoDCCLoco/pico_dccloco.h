#ifndef PICO_DCCLOCO_H
#define PICO_DCCLOCO_H

#include <stdio.h>
#include <pico/stdlib.h>

#include "../PicoDCCEX/pico_dccexpacket.h"
#include "../PicoDCCTrack/pico_dcctrack.h"

class PicoDccLoco
{
private:
    // These are values that are copied with the object, everything else is calculated as needed.
    uint16_t address;
    uint8_t speed;
    bool forward;

    bool support123Speeds = false;  // We don't yet but including the flag for when we do

    // Some cached values to avoid recalculation to send reminders
    uint8_t speedCode;
    uint8_t groupFlags;
    uint32_t functions;

    

public:
    PicoDccLoco(PicoDccExPacket *packet);
    PicoDccLoco(uint16_t address);
    PicoDccLoco(uint16_t address, uint8_t speed, bool forward);

    // Copy constructor
    PicoDccLoco(const PicoDccLoco &other) : address(other.address), speed(other.speed), forward(other.forward) {}

    // Comparision operator for easy of comparing objects
    bool operator==(const PicoDccLoco &other) const {
        return address == other.address;
    }

    uint16_t getAddress() { return address; }

    void update(PicoDccExPacket *packet);
    void updateControl(bool forward, uint8_t speed);
    void updateFunct(uint8_t function, bool value);

    bool verifyCV(int8_t cvNumber, int8_t expectedByte);
    bool verifyCV(int8_t cvNumber, bool expectedBit);

    int8_t readCVByte(int8_t cvNumber);
    bool readCVBit(int8_t cvNumber, uint8_t bit);

    void writeCVBytes(int8_t cvNumber, int8_t newByte);
    void writeCVBit(int8_t cvNumber, bool newBit);

    raw_dcc_cmd_t getEmergecyStopCommand();
    raw_dcc_cmd_t getThrottleCommand();
    raw_dcc_cmd_t getFunctionCommand(uint8_t fnGroup);
};

#endif