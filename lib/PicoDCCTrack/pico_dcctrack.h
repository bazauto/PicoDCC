/*
    This class deals with running a single track.
    It can be either a mainline track or a programming track.
    All tracks will run from the dcc controller main loop on core 1.
*/
#ifndef PICO_DCCTRACK_H
#define PICO_DCCTRACK_H

#include <stdio.h>
#include <cstdint>

#define UNUSED_PIN 255
#define BASE_ADC_PIN 26

#define DCC_MAX_DATA_BYTES 6    // This is the max specified in the MNRA spec.  5 + CRC.
#define DCC_MAIN_PREAMBLE 14
#define DCC_PROG_PREAMBLE 20

#define TRACK_POWER_CURRENT_SAMPLES 2000
#define TRACK_POWER_ADC_VREF 3.3
#define TRACK_POWER_ADC_RANGE (1 << 12)
#define TRACK_POWER_ADC_CONVERT (TRACK_POWER_ADC_VREF / (TRACK_POWER_ADC_RANGE - 1))

#define HIGHEST_SHORT_ADDR 127

typedef unsigned int uint;

typedef struct {
    uint8_t signal_pin = UNUSED_PIN;
    uint8_t ctrl_pin = UNUSED_PIN;
    uint8_t adc_num = UNUSED_PIN;
} track_settings_t;

typedef struct
{
    bool is_prog;
    uint8_t length;
    uint8_t data[DCC_MAX_DATA_BYTES];
} raw_dcc_cmd_t;

enum TRACK_STATE : uint {
    TRACK_UNINIT,
    TRACK_IDLE
};

class PicoDccTrack {

private:
    bool is_prog;
    TRACK_STATE state = TRACK_UNINIT;

    void *pio;

    uint8_t power_signal_pin = UNUSED_PIN;
    uint8_t power_ctrl_pin = UNUSED_PIN;
    uint8_t power_adc_pin = UNUSED_PIN;
    uint8_t power_adc_number;

    float average_current_reading = 0.0;
    uint current_sum = 0;
    uint current_cnt = 0;

public:
    PicoDccTrack(bool is_prog);

    // These must be called before init and never after
    void setSignalCtrlPin(uint8_t pin);
    void setPowerCtrlPin(uint8_t pin);
    void setPowerAdcNumber(uint8_t adc);   // this is optional, if not set then no current reading is availbale

    // Setup the PINs and current ADC
    void init();
    void init(track_settings_t settings);

    void loop();

    void processCommand(raw_dcc_cmd_t *cmd);
    void sendIdle();

    // Power control
    void powerOn() { setPower(true); }
    void powerOff() { setPower(false); }
    void setPower(bool power_on);

    float getAverageCurrent() { return average_current_reading; }

    bool getIsProg() { return is_prog; }

    uint8_t getPowerCtrlPin() {  return power_ctrl_pin; }
    uint8_t getPowerAdcPin() {  return power_adc_pin; }
    uint8_t getPowerAdcNumber() {  return power_adc_number; }

    bool canReadCurrent() { return power_adc_pin != UNUSED_PIN; }
};

#endif