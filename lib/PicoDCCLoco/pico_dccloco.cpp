#include <string.h>

#include "pico_dccloco.h"


PicoDccLoco::PicoDccLoco(int maxCab)
{
    
}

void PicoDccLoco::updateControl(bool forward, uint8_t speed)
{
    if (speed > 127) return;

    if (this->forward != forward || this->speed != speed) {
        // Notify 
    }

    this->forward = forward;
    this->speed = speed;


}

void PicoDccLoco::updateFunct(uint8_t function, bool value)
{
    
}
