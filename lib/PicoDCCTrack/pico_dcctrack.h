/*
    This class deals with running a single track.
    It can be either a mainline track or a programming track.
    All tracks will run from the dcc controller main loop on core 1.
*/
#ifndef PICO_DCCTRACK_H
#define PICO_DCCTRACK_H

#include <stdio.h>
#include <cstdint>

#include "../dcc_types.h"

// Forward declaration to avoid circular dependency
class PicoDccLocos;

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <pico/time.h>
#endif

#define UNUSED_PIN 255
#define BASE_ADC_PIN 26

#define DCC_MAIN_PREAMBLE 14
#define DCC_PROG_PREAMBLE 20

#define TRACK_POWER_CURRENT_SAMPLES 200
#define TRACK_POWER_ADC_VREF 3.3
#define TRACK_POWER_ADC_RANGE (1 << 12)
#define TRACK_POWER_ADC_CONVERT (TRACK_POWER_ADC_VREF / (TRACK_POWER_ADC_RANGE - 1))

#define HIGHEST_SHORT_ADDR 127

#define CMD_QUEUE_LENGTH 5

typedef unsigned int uint;

typedef struct track_settings {
    uint8_t signal_pin = UNUSED_PIN;
    uint8_t ctrl_pin = UNUSED_PIN;
    uint8_t adc_num = UNUSED_PIN;
    uint8_t short_pin = UNUSED_PIN;
} track_settings_t;

class PicoDccTrack {

private:
    bool is_prog;
    queue_t cmd_queue;
    
    // Reference to locomotive collection for reminder generation (Core 1)
    // Only main track uses this; prog track leaves it nullptr
    PicoDccLocos *locos_collection;

    void *pio;
    uint pio_sm;  // State machine number for PIO monitoring

    uint8_t signal_pin = UNUSED_PIN;
    uint8_t power_ctrl_pin = UNUSED_PIN;
    uint8_t power_adc_pin = UNUSED_PIN;
    uint8_t power_adc_number;
    uint8_t short_led_pin = UNUSED_PIN;

    float average_current_reading = 0.0;
    uint current_sum = 0;
    uint current_cnt = 0;
    bool power_on = false;
    bool send_idle_packets = true;  // Flag to control idle packet transmission

    // PIO Health Monitoring (Options 1, 3, 4)
    struct {
        // Option 3: Transmission Counter Monitoring
        uint32_t commands_queued = 0;
        uint32_t commands_sent = 0;
        uint32_t idle_packets_sent = 0;
        uint32_t last_activity_time = 0;
        
        // Option 1: State Machine Status Monitoring  
        uint32_t last_pio_pc = 0;
        uint32_t pio_stall_count = 0;
        uint32_t last_pio_check_time = 0;
        uint32_t last_transmission_count = 0;
        
        // Option 4: Interrupt Activity Detection
        volatile uint32_t last_interrupt_time = 0;
        bool interrupt_enabled = false;
        
        // Health status
        bool is_healthy = true;
        uint32_t failure_count = 0;
    } pio_health;

public:
    PicoDccTrack(bool is_prog, track_settings_t settings, PicoDccLocos *locos = nullptr);

    void loop();

    void queueCommand(raw_dcc_cmd_t *cmd);
    void sendCommand(raw_dcc_cmd_t *cmd);
    void sendIdle();

    // Power control
    void powerOn() { setPower(true); }
    void powerOff() { setPower(false); }
    void setPower(bool on);
    
    // Idle packet control (for programming/testing)
    void enableIdlePackets() { send_idle_packets = true; }
    void disableIdlePackets() { send_idle_packets = false; }
    bool getIdlePacketsEnabled() { return send_idle_packets; }

    float getAverageCurrent() { return average_current_reading; }

    bool getIsProg() { return is_prog; }
    bool getPower() { return power_on; }

    uint8_t getPowerCtrlPin() {  return power_ctrl_pin; }
    uint8_t getPowerAdcPin() {  return power_adc_pin; }
    uint8_t getPowerAdcNumber() {  return power_adc_number; }

    bool canReadCurrent() { return power_adc_pin != UNUSED_PIN; }

    // Safety monitoring
    uint32_t getLastCommandTime() { return last_command_time; }
    uint32_t getMaxCommandGap() { return max_command_gap; }
    void resetMaxCommandGap() { max_command_gap = 0; }
    
    // PIO Health Monitoring
    bool isPIOHealthy();
    void checkPIOHealth();
    uint32_t getCommandsQueued() { return pio_health.commands_queued; }
    uint32_t getCommandsSent() { return pio_health.commands_sent; }
    uint32_t getIdlePacketsSent() { return pio_health.idle_packets_sent; }
    bool getPIOHealthStatus() { return pio_health.is_healthy; }

private:
    uint32_t last_command_time = 0;   // Time of last command sent
    uint32_t max_command_gap = 0;     // Maximum gap between commands
};

#endif