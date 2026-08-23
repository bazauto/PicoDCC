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

// dcc_program_init() calls sm_config_set_fifo_join(PIO_FIFO_JOIN_TX), so the TX
// FIFO is 8 words deep rather than the unjoined 4. A packet is one or two words
// -- sendCommand() pushes a second only when length > 1 -- and both words must
// be written together, so the room check is for a whole packet.
#define DCC_PIO_TX_FIFO_DEPTH 8
#define DCC_PIO_WORDS_PER_PACKET 2

// loop() is paced by the main track's pio_sm_put_blocking, so it runs roughly
// once per transmitted main-track packet -- on the order of 7ms. 128 samples is
// therefore a window of about 0.9s: fast enough that the LCD follows a change in
// draw, slow enough to smooth packet-to-packet ripple. It was 2000 (~14 seconds),
// which made the displayed current useless for diagnosis -- see #36.
//
// The programming track runs at the same rate because both tracks are serviced
// from the same Core 1 pass; it just no longer contributes to setting that rate
// (#35).
#define TRACK_POWER_CURRENT_SAMPLES 128
#define TRACK_POWER_ADC_VREF 3.3
#define TRACK_POWER_ADC_RANGE (1 << 12)
#define TRACK_POWER_ADC_CONVERT (TRACK_POWER_ADC_VREF / (TRACK_POWER_ADC_RANGE - 1))

// Overcurrent trip point, as a percentage of ADC full scale.
//
// Multiply before dividing. The original expression was
// `TRACK_POWER_ADC_RANGE / 100 * 90`, where the integer division truncates 40.96
// to 40 and the trip lands at 3600 -- 87.9%, not the 90% the comment claimed
// (#36). Naming the threshold keeps the arithmetic in one place.
#define TRACK_POWER_TRIP_PERCENT 90
#define TRACK_POWER_TRIP_THRESHOLD ((TRACK_POWER_ADC_RANGE * TRACK_POWER_TRIP_PERCENT) / 100)

// HIGHEST_SHORT_ADDR lives in dcc_types.h now, alongside the other address
// and speed limits.

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

    // The ADC block is one shared peripheral, initialised by whichever track is
    // constructed first. Defined in pico_dcctrack.cpp.
    static bool adc_block_initialised;
    uint8_t short_led_pin = UNUSED_PIN;

    float average_current_reading = 0.0;
    uint current_sum = 0;
    uint current_cnt = 0;
    bool power_on = false;
    bool tripped = false;  // Set when overcurrent protection activates

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
    // Whether this call may park Core 1 waiting for room in the PIO TX FIFO.
    //
    // Exactly one track may block, and it must be the main track: that block is
    // what stops Core 1 outrunning the hardware, and pacing on the track that
    // carries locomotives is the point of the design. A blocking programming
    // track refills the main track at the *programming* track's slower rate --
    // its packets carry six more preamble bits -- so the main FIFO drains faster
    // than it fills, and an empty FIFO parks the signal pin high (#34), putting
    // DC on the rails between packets. That is #35.
    enum class Pacing {
        Blocking,     // main track: block for FIFO room, and pace Core 1
        NonBlocking,  // programming track: skip this pass instead of waiting
    };

    PicoDccTrack(bool is_prog, track_settings_t settings, PicoDccLocos *locos = nullptr);

    void loop(Pacing pacing = Pacing::Blocking);

    void queueCommand(raw_dcc_cmd_t *cmd);
    void sendCommand(raw_dcc_cmd_t *cmd);
    void sendIdle();

    // Power control
    void powerOn() { setPower(true); }
    void powerOff() { setPower(false); }
    void setPower(bool on);
    bool isTripped() { return tripped; }

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

    // Whether the TX FIFO can take a complete packet right now.
    bool hasRoomForPacket() const;

private:
    uint32_t last_command_time = 0;   // Time of last command sent
    uint32_t max_command_gap = 0;     // Maximum gap between commands
};

#endif