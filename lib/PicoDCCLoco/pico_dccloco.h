#ifndef PICO_DCCLOCO_H
#define PICO_DCCLOCO_H

#include <stdio.h>
#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/sem.h>
#include <pico/util/queue.h>
#endif

#include "../PicoDCCEX/pico_dccexpacket.h"
#include "../PicoDCCTrack/pico_dcctrack.h"

#define INVALID_LOCO_ADDR 65535

class PicoDccLoco
{
private:
    // These are values that are copied with the object, everything else is calculated as needed.
    uint16_t address;
    uint8_t speed;
    bool forward;
    bool functions[29];  // F0-F28 function states
    char dccex_status[128];  // Buffer for DCC-EX status response

    // This is the command that will be sent to the track when needed.  It is initially zero length to avoid it being used.
    raw_dcc_cmd_t cmd;

public:
    PicoDccLoco(PicoDccExPacket *packet);
    PicoDccLoco(uint16_t address);
    PicoDccLoco(uint16_t address, uint8_t speed, bool forward);

    // Copy constructor
    PicoDccLoco(const PicoDccLoco &other) : address(other.address), speed(other.speed), forward(other.forward), cmd(other.cmd) {
        memcpy(functions, other.functions, sizeof(functions));
    }

    // Comparision operator for ease of comparing objects
    bool operator==(const PicoDccLoco &other) const {
        return address == other.address;
    }

    uint16_t getAddress() { return address; }

    bool update(PicoDccExPacket *packet);
    bool updateControl(bool _forward, uint8_t _speed);
    uint8_t updateFunct(uint8_t function, bool value);  // Returns function group (1-5) or 0 if invalid

    bool verifyCV(int8_t cvNumber, int8_t expectedByte);
    bool verifyCV(int8_t cvNumber, bool expectedBit);

    int8_t readCVByte(int8_t cvNumber);
    bool readCVBit(int8_t cvNumber, uint8_t bit);

    void writeCVBytes(int8_t cvNumber, int8_t newByte);
    void writeCVBit(int8_t cvNumber, bool newBit);

    raw_dcc_cmd_t getThrottleCommand();
    raw_dcc_cmd_t getFunctionCommand(uint8_t fnGroup);
    
    const char* getDccExStatus();

    bool isValid() const;

private:
    void generateThrottleCommand();
};

#endif