#ifndef PICO_DCCEX_H
#define PICO_DCCEX_H

#include <stdio.h>
#include <pico/stdlib.h>

#ifndef COMMAND_BUFFER_SIZE
 #define COMMAND_BUFFER_SIZE 100
#endif

enum pico_dccex_state
{
    IDLE = 0,
    IN_PACKET = 1,
    PACKET_WAITING = 2
};

class PicoDccExPacket
{
private:
    int maxSupportedCabs = 0;

    enum pico_dccex_state processState;
    int bufferLength = 0;
    char buffer[COMMAND_BUFFER_SIZE] = "";

    char opcode = '\0';
    int cab = 0;
    int speed_funct = 0;
    int direction_state = 0;

public:
    PicoDccExPacket(int maxCab);

    void reset();
    void processInput(char chr);
    void decodePacket();

    enum pico_dccex_state getProcessState() { return processState; }
    char getOpcode() { return opcode; }
    int getCab() { return cab; }
    int getSpeed() { return speed_funct; }
    int getFunct() { return speed_funct; }
    int getDirection() { return direction_state; }
    int getState() { return direction_state; }
};

#endif