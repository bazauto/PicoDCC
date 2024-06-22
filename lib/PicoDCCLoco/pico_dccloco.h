#ifndef PICO_DCCLOCO_H
#define PICO_DCCLOCO_H

#include <stdio.h>
#include <pico/stdlib.h>

#include "../PicoDCCTrack/pico_dcctrack.h"

class PicoDccLoco
{
private:
    uint16_t address;
    bool support123Speeds = false;  // We don't yet but including the flag for when we do
    uint8_t speed;
    bool forward;

    // Some cached values to avoid recalculation to send reminders
    uint8_t speedCode;
    uint8_t groupFlags;
    uint32_t functions;

    

public:
    PicoDccLoco(int maxCab);

    uint16_t getAddress() { return address; }

    void updateControl(bool forward, uint8_t speed);
    void updateFunct(uint8_t function, bool value);

    bool verifyCV(int8_t cvNumber, int8_t expectedByte);
    bool verifyCV(int8_t cvNumber, bool expectedBit);

    int8_t readCVByte(int8_t cvNumber);
    bool readCVBit(int8_t cvNumber, uint8_t bit);

    void writeCVBytes(int8_t cvNumber, int8_t newByte);
    void writeCVBit(int8_t cvNumber, bool newBit);

    raw_dcc_cmd_t getThrottleCommand();
    raw_dcc_cmd_t getFunctionCommand(uint8_t fnGroup);
};

#endif