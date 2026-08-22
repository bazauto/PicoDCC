#include "pico_dcctrack.h"
#include "../pico_diagnostic.h"
#include "../dcc_time.h"
#include "../PicoDCCLoco/pico_dcclocos.h"

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#include <vector>
#else
#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/pio.h>
#include <pico/time.h>

#include "dcc.pio.h"
#endif

// The ADC block is a single shared peripheral. Both tracks want it initialised;
// exactly one of them should do it. See the constructor.
bool PicoDccTrack::adc_block_initialised = false;

PicoDccTrack::PicoDccTrack(bool is_prog_in, track_settings_t settings, PicoDccLocos *locos)
{
    is_prog = is_prog_in;
    locos_collection = locos;  // Store reference to locomotive collection
    signal_pin = settings.signal_pin;
    power_ctrl_pin = settings.ctrl_pin;
    if (settings.adc_num != UNUSED_PIN)
    {
        power_adc_number = settings.adc_num;
        power_adc_pin = BASE_ADC_PIN + settings.adc_num;
    }
    if (settings.short_pin != UNUSED_PIN)
    {
        short_led_pin = settings.short_pin;
        gpio_init(short_led_pin);
        gpio_set_dir(short_led_pin, GPIO_OUT);
        gpio_put(short_led_pin, 0);
    }

    // This is the queue of commands to be sent to the track
    queue_init(&cmd_queue, sizeof(raw_dcc_cmd_t), CMD_QUEUE_LENGTH);

    // Setup Track power control PIN, ensuring off initially
    gpio_init(power_ctrl_pin);
    gpio_set_dir(power_ctrl_pin, GPIO_OUT);
    gpio_put(power_ctrl_pin, 0);

    // Setup for current reading.
    //
    // adc_init() resets and enables the whole ADC block, so it belongs to the
    // system rather than to a track. Calling it per track meant the second track
    // constructed reset the peripheral the first had already configured (#14).
    // The per-pin work below is genuinely per track and stays here.
    //
    // No channel is selected here on purpose. The mux is shared, so a selection
    // made at construction says nothing about which channel is live by the time
    // loop() reads -- loop() selects immediately before each read instead.
    if (canReadCurrent())
    {
        if (!adc_block_initialised)
        {
            adc_init();
            adc_block_initialised = true;
        }
        adc_gpio_init(power_adc_pin);
    }

    // Setup the PIO to run the track signal
    // We use 0 for prog track and 1 for main track
    if (is_prog)
        pio = pio0;
    else
        pio = pio1;

    uint offset = pio_add_program((PIO)pio, &dcc_program);
    pio_sm = pio_claim_unused_sm((PIO)pio, true);
    assert(pio_sm != -1); // This should never happen

    dcc_program_init((PIO)pio, pio_sm, offset, signal_pin, (is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE));
    pio_sm_set_enabled((PIO)pio, pio_sm, true);
    
    // Initialize PIO health monitoring
    pio_health.last_activity_time = dcc_millis();
    pio_health.last_pio_check_time = pio_health.last_activity_time;
    pio_health.last_interrupt_time = pio_health.last_activity_time;
}

void PicoDccTrack::setPower(bool on)
{
    power_on = on;
    gpio_put(power_ctrl_pin, on);

    if (on) {
        tripped = false;  // Clear trip flag when powering on
        // Start a fresh averaging window rather than carrying samples across a
        // power cycle, which would mix two unrelated load conditions.
        current_sum = 0;
        current_cnt = 0;
        if (short_led_pin != UNUSED_PIN)
        {
            // If we have a short LED then turn it off
            gpio_put(short_led_pin, 0);
        }
    }
    else {
        // An unpowered track draws nothing. Leaving the last average in place
        // would show the LCD a current that is no longer flowing.
        average_current_reading = 0.0f;
    }
}

void PicoDccTrack::loop()
{
    // Check PIO health before processing commands
    checkPIOHealth();
    
    // Process current monitoring only if available, and only while the track is
    // energised. With the H-bridge disabled the sense input is not driving a
    // meaningful value; sampling it anyway could trip and latch `tripped` on a
    // track that was already off, showing a fault on the LCD with no fault
    // present (#36).
    if (canReadCurrent() && power_on)
    {
        // Select this track's channel immediately before reading. The ADC mux is
        // shared between both tracks, so a selection made anywhere else -- in the
        // constructor, or by the other track's own loop() -- does not survive to
        // here. Without this the main track sampled the programming track's sense
        // resistor and its overcurrent trip was inoperative (#14).
        adc_select_input(power_adc_number);
        uint reading = adc_read();

        // Check for overcurrent condition (short circuit protection)
        if (reading > TRACK_POWER_TRIP_THRESHOLD)
        {
            // If the current is too high then we need to stop the track
            setPower(false);
            tripped = true;  // Mark as tripped due to overcurrent
            LOG_CRITICAL(COMPONENT_TRACK, "Overcurrent protection activated");

            if (short_led_pin != UNUSED_PIN)
            {
                // If we have a short LED then turn it on
                gpio_put(short_led_pin, 1);
            }
        }

        // Update current averaging.
        //
        // Accumulate first, then test. The original tested first and discarded
        // the sample that closed the window, and divided a 2000-sample sum by a
        // count of 2001 (#36).
        current_sum += reading;
        if (++current_cnt >= TRACK_POWER_CURRENT_SAMPLES)
        {
            average_current_reading = (float)current_sum / (float)current_cnt;
            current_cnt = 0;
            current_sum = 0;
        }
    }

    // Process any incoming messages to be sent to the track
    // Priority: explicit commands from Core 0 > loco reminders > idle packets
    // Repeat logic is handled on Core 0 for explicit commands
    raw_dcc_cmd_t cmd;
    if (queue_try_remove(&cmd_queue, &cmd))
    {
        // Priority 1: Explicit command from Core 0
        sendCommand(&cmd);
        pio_health.commands_sent++;
        pio_health.last_activity_time = dcc_millis();
    }
    else if (locos_collection != nullptr && locos_collection->getNextReminder(cmd))
    {
        // Priority 2: Locomotive reminder (main track only, when no explicit commands waiting)
        sendCommand(&cmd);
        pio_health.commands_sent++;
        pio_health.last_activity_time = dcc_millis();
    }
    else
    {
        // Priority 3: Idle packet (when no commands or reminders available)
        sendIdle();
        pio_health.idle_packets_sent++;
        pio_health.last_activity_time = dcc_millis();
    }
}

void PicoDccTrack::queueCommand(raw_dcc_cmd_t *cmd)
{
    queue_add_blocking(&cmd_queue, cmd);
    pio_health.commands_queued++;
}

void PicoDccTrack::sendCommand(raw_dcc_cmd_t *cmd)
{
    last_command_time = dcc_millis();
    if (cmd->cmd_data == 0)
    {
        // build the data and checksum to send to the PIO
        cmd->cmd_data |= ((uint64_t)(is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE)) << 56;
        cmd->cmd_data |= ((uint64_t)cmd->length + 1) << 48;
        // The two header bytes occupy bits 63-48, so the payload starts at byte 5
        // (bits 47-40) and runs downwards. The shift names that byte directly --
        // it must not be derived from the size of data[], because those are two
        // different quantities that happened to coincide (see #31).
        uint8_t cmd_xor = 0x0;
        for (uint8_t i = 0; i < cmd->length; i++)
        {
            uint8_t shift = DCC_PACKET_FIRST_BYTE - i;
            cmd->cmd_data |= ((uint64_t)cmd->data[i] << (shift * 8));
            cmd_xor ^= cmd->data[i];
        }
        // Add the checksum, immediately after the last payload byte
        cmd->cmd_data |= ((uint64_t)cmd_xor << ((DCC_PACKET_FIRST_BYTE - cmd->length) * 8));
    }

    // Send command to PIO and track successful transmission
    pio_sm_put_blocking((PIO)pio, 0, (cmd->cmd_data >> 32) & 0xFFFFFFFF);
    if (cmd->length > 1)
    {
        pio_sm_put_blocking((PIO)pio, 0, cmd->cmd_data & 0xFFFFFFFF);
    }
    
    // Option 4: Track PIO transmission activity (simulated interrupt)
    pio_health.last_interrupt_time = dcc_millis();
}

void PicoDccTrack::sendIdle()
{
    // This is the Idle packet
    raw_dcc_cmd_t idle_cmd;
    idle_cmd.is_prog = is_prog;
    idle_cmd.length = 3;
    idle_cmd.data[0] = 0xFF;  // Idle packet starts with 0xFF
    idle_cmd.data[1] = 0x00;  // No data
    idle_cmd.data[2] = 0xFF;  // Error detection byte
    idle_cmd.cmd_data = 0;    // Will be computed in sendCommand
    idle_cmd.repeats = 0;
    sendCommand(&idle_cmd);
}

// PIO Health Monitoring Implementation (Options 1, 3, 4)
void PicoDccTrack::checkPIOHealth()
{
    uint32_t now = dcc_millis();
    bool was_healthy = pio_health.is_healthy;
    

    
    // Option 3: Check transmission activity (every 50ms check)
    if (now - pio_health.last_pio_check_time >= 50)
    {
        pio_health.is_healthy = true;  // Assume healthy until proven otherwise
        // Check for transmission stalls
        if (pio_health.commands_queued > pio_health.commands_sent)
        {
            // Commands are queued but not being sent
            if (now - pio_health.last_activity_time > 100) // 100ms timeout
            {
                pio_health.is_healthy = false;
                LOG_CRITICAL(COMPONENT_TRACK, "PIO transmission stall detected");
            }
        }
        
        // Option 1: Alternative PIO health check using transmission rate
        // Monitor that we're successfully transmitting commands/idle packets
        uint32_t total_transmissions = pio_health.commands_sent + pio_health.idle_packets_sent;
        

        
        if (total_transmissions == pio_health.last_transmission_count)
        {
            // No transmissions in the last check period
            pio_health.pio_stall_count++;

            if (pio_health.pio_stall_count >= 3) // No activity for 150ms (3 * 50ms)
            {
                pio_health.is_healthy = false;
                LOG_CRITICAL(COMPONENT_TRACK, "PIO transmission completely stopped");

            }
        }
        else
        {
            pio_health.pio_stall_count = 0;
            pio_health.last_transmission_count = total_transmissions;

        }
        
        // Option 4: Check interrupt activity (if enabled)
        if (pio_health.interrupt_enabled)
        {
            if (now - pio_health.last_interrupt_time > 200) // 200ms timeout for interrupts
            {
                pio_health.is_healthy = false;
                LOG_CRITICAL(COMPONENT_TRACK, "PIO interrupt activity timeout");
            }
        }
        
        pio_health.last_pio_check_time = now;
    }
    
    // Track failure events
    if (!pio_health.is_healthy)
    {
        pio_health.failure_count++;
        if (was_healthy) // First failure detection
        {
            LOG_CRITICAL(COMPONENT_TRACK, "PIO health failure detected");
        }
    }
}

bool PicoDccTrack::isPIOHealthy()
{
    checkPIOHealth();
    return pio_health.is_healthy;
}
